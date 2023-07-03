/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2001-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2001-2008 Marcus Meissner <marcus@jet.franken.de>
    SPDX-FileCopyrightText: 2012 Marcus Meissner <marcus@jet.franken.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// remove comment to enable debugging
// #undef QT_NO_DEBUG
#include "kamera.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QStandardPaths>

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KProtocolInfo>

#include "kameralist.h"
#include <kio_kamera_log.h>

#define tocstr(x) ((x).toLocal8Bit())

#define MAXIDLETIME 30 /* seconds */

using namespace KIO;

// Pseudo plugin class to embed meta data
class KIOPluginForMetaData : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.kde.kio.slave.camera" FILE "camera.json")
};

extern "C" {
Q_DECL_EXPORT int kdemain(int argc, char **argv);

static void frontendCameraStatus(GPContext *context, const char *status, void *data);
static unsigned int frontendProgressStart(GPContext *context, float totalsize, const char *status, void *data);
static void frontendProgressUpdate(GPContext *context, unsigned int id, float current, void *data);
}

int kdemain(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("kio_kamera"));
    KLocalizedString::setApplicationDomain("kio_kamera");

#ifdef DEBUG_KAMERA_KIO
    QLoggingCategory::setFilterRules(QStringLiteral("kf.kio.workers.camera.debug = true"));
#endif

    if (argc != 4) {
        qCDebug(KIO_KAMERA_LOG) << "Usage: kio_kamera protocol "
                                   "domain-socket1 domain-socket2";
        exit(-1);
    }

    KameraProtocol slave(argv[2], argv[3]);

    slave.dispatchLoop();

    return 0;
}

static QString path_quote(QString path)
{
    return path.replace(QStringLiteral("/"), QStringLiteral("%2F")).replace(QStringLiteral(" "), QStringLiteral("%20"));
}
static QString path_unquote(QString path)
{
    return path.replace(QStringLiteral("%2F"), QStringLiteral("/")).replace(QStringLiteral("%20"), QStringLiteral(" "));
}

KameraProtocol::KameraProtocol(const QByteArray &pool, const QByteArray &app)
    : WorkerBase("camera", pool, app)
    , m_camera(nullptr)
{
    // attempt to initialize libgphoto2 and chosen camera (requires locking)
    // (will init m_camera, since the m_camera's configuration is empty)
    m_camera = nullptr;
    m_file = nullptr;
    m_config = new KConfig(KProtocolInfo::config(QStringLiteral("camera")), KConfig::SimpleConfig);
    m_context = gp_context_new();
    actiondone = true;
    cameraopen = false;
    m_lockfile = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/kamera";
    idletime = 0;
}

// This handler is getting called every second. We use it to do the
// delayed close of the camera.
// Logic is:
// 	- No more requests in the queue (signaled by actiondone) AND
//		- We are MAXIDLETIME seconds idle OR
//		- Another slave wants to have access to the camera.
//
// The existence of a lockfile is used to signify "please give up camera".
//
KIO::WorkerResult KameraProtocol::special(const QByteArray &)
{
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::special() at " << getpid() << ". idletime: " << idletime;

    if (!actiondone && cameraopen) {
        struct stat stbuf;
        if ((-1 != ::stat(m_lockfile.toUtf8(), &stbuf)) || (idletime++ >= MAXIDLETIME)) {
            qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::special() closing camera.";
            closeCamera();
            setTimeoutSpecialCommand(-1);
        } else {
            // continue to wait
            setTimeoutSpecialCommand(1);
        }
    } else {
        // We let it run until the slave gets no actions anymore.
        setTimeoutSpecialCommand(1);
    }
    actiondone = false;
    return KIO::WorkerResult::pass();
}

KameraProtocol::~KameraProtocol()
{
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::~KameraProtocol()";
    delete m_config;
    if (m_camera) {
        closeCamera();
        gp_camera_free(m_camera);
        m_camera = nullptr;
    }
}

// initializes the camera for usage -
// should be done before operations over the wire
bool KameraProtocol::openCamera(QString &str)
{
    idletime = 0;
    actiondone = true;
    if (!m_camera) {
        reparseConfiguration();
    } else {
        if (!cameraopen) {
            int ret, tries = 15;
            qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::openCamera at " << getpid();
            // Gets this far.
            while (tries--) {
                ret = gp_camera_init(m_camera, m_context);
                if ((ret == GP_ERROR_IO_USB_CLAIM) || (ret == GP_ERROR_IO_LOCK)) {
                    // just create / touch if not there
                    int fd = ::open(m_lockfile.toUtf8(), O_CREAT | O_WRONLY, 0600);
                    if (fd != -1)
                        ::close(fd);
                    ::sleep(1);
                    qCDebug(KIO_KAMERA_LOG) << "openCamera at " << getpid() << "- busy, ret " << ret << ", trying again.";
                    continue;
                }
                if (ret == GP_OK)
                    break;
                str = gp_result_as_string(ret);
                return false;
            }
            ::remove(m_lockfile.toUtf8());
            setTimeoutSpecialCommand(1);
            qCDebug(KIO_KAMERA_LOG) << "openCamera succeeded at " << getpid();
            cameraopen = true;
        }
    }
    return true;
}

// should be done after operations over the wire
void KameraProtocol::closeCamera()
{
    int gpr;

    if (!m_camera) {
        return;
    }

    if ((gpr = gp_camera_exit(m_camera, m_context)) != GP_OK) {
        qCDebug(KIO_KAMERA_LOG) << "closeCamera failed with " << gp_result_as_string(gpr);
    }
    // HACK: gp_camera_exit() in gp 2.0 does not close the port if there
    //       is no camera_exit function.
    gp_port_close(m_camera->port);
    cameraopen = false;
    current_camera = QStringLiteral("");
    current_port = QStringLiteral("");
    return;
}

static QString fix_foldername(const QString &ofolder)
{
    QString folder = ofolder;
    if (folder.length() > 1) {
        while ((folder.length() > 1) && (folder.right(1) == QStringLiteral("/")))
            folder = folder.left(folder.length() - 1);
    }
    if (folder.length() == 0) {
        folder = QStringLiteral("/");
    }
    return folder;
}

// The KIO slave "get" function (starts a download from the camera)
// The actual returning of the data is done in the frontend callback functions.
KIO::WorkerResult KameraProtocol::get(const QUrl &url)
{
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::get(" << url.path() << ")";
    QString directory, file;
    CameraFileType fileType;
    int gpr;

    auto splitResult = split_url2camerapath(url.path(), directory, file);

    if (!splitResult.success()) {
        return splitResult;
    }

    if (!openCamera()) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
    }

#define GPHOTO_TEXT_FILE(xx)                                                                                                                                   \
    if (!directory.compare(QStringLiteral("/")) && !file.compare(#xx ".txt")) {                                                                                \
        CameraText xx;                                                                                                                                         \
        gpr = gp_camera_get_##xx(m_camera, &xx, m_context);                                                                                                    \
        if (gpr != GP_OK) {                                                                                                                                    \
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());                                                                               \
        }                                                                                                                                                      \
        QByteArray chunkDataBuffer = QByteArray::fromRawData(xx.text, strlen(xx.text));                                                                        \
        data(chunkDataBuffer);                                                                                                                                 \
        processedSize(strlen(xx.text));                                                                                                                        \
        chunkDataBuffer.clear();                                                                                                                               \
        return KIO::WorkerResult::pass();                                                                                                                      \
    }

    GPHOTO_TEXT_FILE(about);
    GPHOTO_TEXT_FILE(manual);
    GPHOTO_TEXT_FILE(summary);

#undef GPHOTO_TEXT_FILE
    // Q_EMIT info message
    // WARNING Fix this
    // infoMessage( i18n("Retrieving data from camera <b>%1</b>", current_camera) );

    // Note: There's no need to re-read directory for each get() anymore
    gp_file_new(&m_file);

    // Q_EMIT the total size (we must do it before sending data to allow preview)
    CameraFileInfo info;

    gpr = gp_camera_file_get_info(m_camera, tocstr(fix_foldername(directory)), tocstr(file), &info, m_context);
    if (gpr != GP_OK) {
        gp_file_unref(m_file);
        if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND)) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.path());
        } else {
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
        }
    }

    // at last, a proper API to determine whether a thumbnail was requested.
    if (cameraSupportsPreview() && metaData(QStringLiteral("thumbnail")) == QStringLiteral("1")) {
        qCDebug(KIO_KAMERA_LOG) << "get() retrieving the thumbnail";
        fileType = GP_FILE_TYPE_PREVIEW;
        if (info.preview.fields & GP_FILE_INFO_SIZE) {
            totalSize(info.preview.size);
        }
        if (info.preview.fields & GP_FILE_INFO_TYPE) {
            mimeType(info.preview.type);
        }
    } else {
        qCDebug(KIO_KAMERA_LOG) << "get() retrieving the full-scale photo";
        fileType = GP_FILE_TYPE_NORMAL;
        if (info.file.fields & GP_FILE_INFO_SIZE) {
            totalSize(info.file.size);
        }
        if (info.preview.fields & GP_FILE_INFO_TYPE) {
            mimeType(info.file.type);
        }
    }

    // fetch the data
    m_fileSize = 0;
    gpr = gp_camera_file_get(m_camera, tocstr(fix_foldername(directory)), tocstr(file), fileType, m_file, m_context);
    if ((gpr == GP_ERROR_NOT_SUPPORTED) && (fileType == GP_FILE_TYPE_PREVIEW)) {
        // If we get here, the file info command information
        // will either not be used, or still valid.
        fileType = GP_FILE_TYPE_NORMAL;
        gpr = gp_camera_file_get(m_camera, tocstr(fix_foldername(directory)), tocstr(file), fileType, m_file, m_context);
    }
    switch (gpr) {
    case GP_OK:
        break;
    case GP_ERROR_FILE_NOT_FOUND:
    case GP_ERROR_DIRECTORY_NOT_FOUND:
        gp_file_unref(m_file);
        m_file = nullptr;
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, url.fileName());
    default:
        gp_file_unref(m_file);
        m_file = nullptr;
        return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
    }
    // Q_EMIT the mimetype
    // NOTE: we must first get the file, so that CameraFile->name would be set
    const char *fileMimeType;
    gp_file_get_mime_type(m_file, &fileMimeType);
    mimeType(fileMimeType);

    // We need to pass left over data here. Some camera drivers do not
    // implement progress callbacks!
    const char *fileData;
    long unsigned int fileSize;
    // This merely returns us a pointer to gphoto's internal data
    // buffer -- there's no expensive memcpy
    gpr = gp_file_get_data_and_size(m_file, &fileData, &fileSize);
    if (gpr != GP_OK) {
        qCDebug(KIO_KAMERA_LOG) << "get():: get_data_and_size failed.";
        gp_file_free(m_file);
        m_file = nullptr;
        return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
    }
    // make sure we're not sending zero-sized chunks (=EOF)
    // also make sure we send only if the progress did not send the data
    // already.
    if ((fileSize > 0) && (fileSize - m_fileSize) > 0) {
        unsigned long written = 0;
        QByteArray chunkDataBuffer;

        // We need to split it up here. Someone considered it funny
        // to discard any data() larger than 16MB.
        //
        // So nearly any Movie will just fail....
        while (written < fileSize - m_fileSize) {
            unsigned long towrite = 1024 * 1024; // 1MB

            if (towrite > fileSize - m_fileSize - written) {
                towrite = fileSize - m_fileSize - written;
            }
            chunkDataBuffer = QByteArray::fromRawData(fileData + m_fileSize + written, towrite);
            processedSize(m_fileSize + written + towrite);
            data(chunkDataBuffer);
            chunkDataBuffer.clear();
            written += towrite;
        }
        m_fileSize = fileSize;
        setFileSize(fileSize);
    }

    gp_file_unref(m_file); /* just unref, might be stored in fs */
    m_file = nullptr;
    return KIO::WorkerResult::pass();
}

// The KIO slave "stat" function.
KIO::WorkerResult KameraProtocol::stat(const QUrl &url)
{
    qCDebug(KIO_KAMERA_LOG) << "stat(\"" << url.path() << "\")";

    if (url.path().isEmpty()) {
        QUrl rooturl(url);

        qCDebug(KIO_KAMERA_LOG) << "redirecting to /";
        rooturl.setPath(QStringLiteral("/"));
        redirection(rooturl);
        return KIO::WorkerResult::pass();
    }

    if (url.path() == QStringLiteral("/"))
        return statRoot();
    else
        return statRegular(url);

    return KIO::WorkerResult::pass();
}

// Implements stat("/") -- which always returns the same value.
KIO::WorkerResult KameraProtocol::statRoot()
{
    KIO::UDSEntry entry;

    entry.fastInsert(KIO::UDSEntry::UDS_NAME, QString::fromLocal8Bit("/"));

    entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);

    entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (S_IRUSR | S_IRGRP | S_IROTH));
    statEntry(entry);
    // If we just do this call, timeout right away if no other requests are
    // pending. This is for the kdemm autodetection using media://camera
    idletime = MAXIDLETIME;

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KameraProtocol::split_url2camerapath(const QString &url, QString &directory, QString &file)
{
    QStringList components, camarr;
    QString cam, camera, port;
    KIO::WorkerResult result = KIO::WorkerResult::pass();

    components = url.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (components.size() == 0) {
        return KIO::WorkerResult::pass();
    }
    cam = path_unquote(components.takeFirst());
    if (!cam.isEmpty()) {
        if (!cam.contains(QLatin1Char('@'))) {
            return KIO::WorkerResult::fail(KIO::ERR_MALFORMED_URL, url);
        }

        camarr = cam.split(QLatin1Char('@'));
        camera = path_unquote(camarr.takeFirst());
        port = path_unquote(camarr.takeLast());
        result = setCamera(camera, port);
    }
    if (components.isEmpty()) {
        directory = QStringLiteral("/");
        return result;
    }

    file = path_unquote(components.takeLast());
    directory = path_unquote(QStringLiteral("/") + components.join(QLatin1Char('/')));

    return result;
}

// Implements a regular stat() of a file / directory, returning all we know about it
KIO::WorkerResult KameraProtocol::statRegular(const QUrl &xurl)
{
    KIO::UDSEntry entry;
    QString directory, file;
    int gpr;

    qCDebug(KIO_KAMERA_LOG) << "statRegular(\"" << xurl.path() << "\")";

    auto splitResult = split_url2camerapath(xurl.path(), directory, file);

    if (!splitResult.success()) {
        return splitResult;
    }

    if (openCamera() == false) {
        return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, xurl.path());
    }

    if (directory == QLatin1String("/")) {
        KIO::UDSEntry entry;

#define GPHOTO_TEXT_FILE(xx)                                                                                                                                   \
    if (!file.compare(#xx ".txt")) {                                                                                                                           \
        CameraText xx;                                                                                                                                         \
        gpr = gp_camera_get_about(m_camera, &xx, m_context);                                                                                                   \
        if (gpr != GP_OK) {                                                                                                                                    \
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, xurl.fileName());                                                                          \
        }                                                                                                                                                      \
        translateTextToUDS(entry, #xx ".txt", xx.text);                                                                                                        \
        statEntry(entry);                                                                                                                                      \
        return KIO::WorkerResult::pass();                                                                                                                      \
    }
        GPHOTO_TEXT_FILE(about);
        GPHOTO_TEXT_FILE(manual);
        GPHOTO_TEXT_FILE(summary);
#undef GPHOTO_TEXT_FILE

        QString xname = current_camera + QLatin1Char('@') + current_port;
        entry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(xname));
        entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, current_camera);
        entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
        entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (S_IRUSR | S_IRGRP | S_IROTH));
        statEntry(entry);
        return KIO::WorkerResult::pass();
    }

    // Is "url" a directory?
    KameraList dirList;
    qCDebug(KIO_KAMERA_LOG) << "statRegular() Requesting directories list for " << directory;

    gpr = gp_camera_folder_list_folders(m_camera, tocstr(fix_foldername(directory)), dirList, m_context);
    if (gpr != GP_OK) {
        if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND)) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, xurl.path());
        } else {
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
        }
    }

    const char *name;
    for (int i = 0; i < gp_list_count(dirList); i++) {
        gp_list_get_name(dirList, i, &name);
        if (file.compare(name) == 0) {
            KIO::UDSEntry entry;
            translateDirectoryToUDS(entry, file);
            statEntry(entry);
            return KIO::WorkerResult::pass();
        }
    }

    // Is "url" a file?
    CameraFileInfo info;
    gpr = gp_camera_file_get_info(m_camera, tocstr(fix_foldername(directory)), tocstr(file), &info, m_context);
    if (gpr != GP_OK) {
        if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND)) {
            return KIO::WorkerResult::fail(KIO::ERR_DOES_NOT_EXIST, xurl.path());
        } else {
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
        }
    }
    translateFileToUDS(entry, info, file);
    statEntry(entry);
    return KIO::WorkerResult::pass();
}

// The KIO slave "del" function.
KIO::WorkerResult KameraProtocol::del(const QUrl &url, bool isFile)
{
    QString directory, file;
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::del(" << url.path() << ")";

    auto splitResult = split_url2camerapath(url.path(), directory, file);

    if (!splitResult.success()) {
        return splitResult;
    }

    if (!openCamera()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, file);
    }
    if (!cameraSupportsDel()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, file);
    }

    if (isFile) {
        int ret = gp_camera_file_delete(m_camera, tocstr(fix_foldername(directory)), tocstr(file), m_context);

        if (ret != GP_OK) {
            return KIO::WorkerResult::fail(KIO::ERR_CANNOT_DELETE, file);
        } else {
            return KIO::WorkerResult::pass();
        }
    }

    return KIO::WorkerResult::pass();
}

// The KIO slave "listDir" function.
KIO::WorkerResult KameraProtocol::listDir(const QUrl &yurl)
{
    QString directory, file;
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::listDir(" << yurl.path() << ")";

    auto splitResult = split_url2camerapath(yurl.path(), directory, file);

    if (!splitResult.success()) {
        return splitResult;
    }

    if (!file.isEmpty()) {
        if (directory == QLatin1Char('/')) {
            directory = QLatin1Char('/') + file;
        } else {
            directory = directory + QLatin1Char('/') + file;
        }
    }

    if (yurl.path() == QLatin1Char('/')) {
        QUrl xurl;
        // List the available cameras
        QStringList groupList = m_config->groupList();
        qCDebug(KIO_KAMERA_LOG) << "Found cameras: " << groupList.join(QStringLiteral(", "));
        QStringList::Iterator it;
        KIO::UDSEntry entry;

        /*
         * What we do:
         * - Autodetect cameras and remember them with their ports.
         * - List all saved and possible offline cameras.
         * - List all autodetected and not yet printed cameras.
         */
        QMap<QString, QString> ports, names;
        QMap<QString, int> modelcnt;

        /* Autodetect USB cameras ... */
        GPContext *glob_context = nullptr;
        int i, count;
        KameraList list;
        CameraAbilitiesList *al;
        GPPortInfoList *il;

        gp_abilities_list_new(&al);
        gp_abilities_list_load(al, glob_context);
        gp_port_info_list_new(&il);
        gp_port_info_list_load(il);
        gp_abilities_list_detect(al, il, list, glob_context);
        gp_abilities_list_free(al);
        gp_port_info_list_free(il);

        count = gp_list_count(list);

        for (i = 0; i < count; i++) {
            const char *model, *value;

            gp_list_get_name(list, i, &model);
            gp_list_get_value(list, i, &value);

            ports[value] = model;
            // NOTE: We might get different ports than usb: later!
            if (strcmp(value, "usb:") != 0) {
                names[model] = value;
            }

            /* Save them, even though we can autodetect them for
             * offline listing.
             */
#if 0
            KConfigGroup cg(m_config, model);
            cg.writeEntry("Model", model);
            cg.writeEntry("Path", value);
#endif
            modelcnt[model]++;
        }

        /* Avoid duplicated entry, that is a camera with both
         * port usb: and usb:001,042 entries. */
        if (ports.contains(QStringLiteral("usb:")) && names.contains(ports[QStringLiteral("usb:")])
            && names[ports[QStringLiteral("usb:")]] != QStringLiteral("usb:")) {
            ports.remove(QStringLiteral("usb:"));
        }

        for (it = groupList.begin(); it != groupList.end(); it++) {
            QString m_cfgPath;
            if (*it == QStringLiteral("<default>")) {
                continue;
            }

            KConfigGroup cg(m_config, *it);
            m_cfgPath = cg.readEntry("Path");

            // we autodetected those ...
            if (m_cfgPath.contains(QLatin1String("usb:"))) {
                cg.deleteGroup();
                continue;
            }

            QString xname;

            entry.clear();
            entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH));
            xname = (*it) + '@' + m_cfgPath;
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(xname));
            // do not confuse regular users with the @usb...
            entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, *it);
            listEntry(entry);
        }

        QMap<QString, QString>::iterator portsit;

        for (portsit = ports.begin(); portsit != ports.end(); portsit++) {
            entry.clear();
            entry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
            // do not confuse regular users with the @usb...
            entry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, portsit.value());
            entry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(portsit.value() + QLatin1Char('@') + portsit.key()));

            entry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH));
            listEntry(entry);
        }
        return KIO::WorkerResult::pass();
    }

    if (directory.isEmpty()) {
        QUrl rooturl(yurl);

        qCDebug(KIO_KAMERA_LOG) << "redirecting to /";
        if (!current_camera.isEmpty() && !current_port.isEmpty()) {
            rooturl.setPath(QLatin1Char('/') + current_camera + QLatin1Char('@') + current_port + QLatin1Char('/'));
        } else {
            rooturl.setPath(QStringLiteral("/"));
        }
        redirection(rooturl);
        return KIO::WorkerResult::pass();
    }

    if (!openCamera()) {
        return KIO::WorkerResult::fail(KIO::ERR_CANNOT_READ, yurl.path());
    }

    KameraList dirList;
    KameraList fileList;
    KameraList specialList;
    int gpr;

    if (!directory.compare(QStringLiteral("/"))) {
        CameraText text;
        if (GP_OK == gp_camera_get_manual(m_camera, &text, m_context)) {
            gp_list_append(specialList, "manual.txt", nullptr);
        }
        if (GP_OK == gp_camera_get_about(m_camera, &text, m_context)) {
            gp_list_append(specialList, "about.txt", nullptr);
        }
        if (GP_OK == gp_camera_get_summary(m_camera, &text, m_context)) {
            gp_list_append(specialList, "summary.txt", nullptr);
        }
    }

    gpr = readCameraFolder(directory, dirList, fileList);
    if (gpr != GP_OK) {
        qCDebug(KIO_KAMERA_LOG) << "read Camera Folder failed:" << gp_result_as_string(gpr);
        return KIO::WorkerResult::fail(KIO::ERR_WORKER_DEFINED, i18n("Could not read. Reason: %1", QString::fromLocal8Bit(gp_result_as_string(gpr))));
    }

    totalSize(gp_list_count(specialList) + gp_list_count(dirList) + gp_list_count(fileList));

    KIO::UDSEntry entry;
    const char *name;

    for (int i = 0; i < gp_list_count(dirList); ++i) {
        gp_list_get_name(dirList, i, &name);
        translateDirectoryToUDS(entry, QString::fromLocal8Bit(name));
        listEntry(entry);
    }

    CameraFileInfo info;

    for (int i = 0; i < gp_list_count(fileList); ++i) {
        gp_list_get_name(fileList, i, &name);
        // we want to know more info about files (size, type...)
        gp_camera_file_get_info(m_camera, tocstr(directory), name, &info, m_context);
        translateFileToUDS(entry, info, QString::fromLocal8Bit(name));
        listEntry(entry);
    }
    if (!directory.compare(QStringLiteral("/"))) {
        CameraText text;
        if (GP_OK == gp_camera_get_manual(m_camera, &text, m_context)) {
            translateTextToUDS(entry, QStringLiteral("manual.txt"), text.text);
            listEntry(entry);
        }
        if (GP_OK == gp_camera_get_about(m_camera, &text, m_context)) {
            translateTextToUDS(entry, QStringLiteral("about.txt"), text.text);
            listEntry(entry);
        }
        if (GP_OK == gp_camera_get_summary(m_camera, &text, m_context)) {
            translateTextToUDS(entry, QStringLiteral("summary.txt"), text.text);
            listEntry(entry);
        }
    }

    return KIO::WorkerResult::pass();
}

KIO::WorkerResult KameraProtocol::setCamera(const QString &camera, const QString &port)
{
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::setCamera(" << camera << ", " << port << ")";
    int gpr, idx;

    if (!camera.isEmpty() && !port.isEmpty()) {
        if (m_camera && (current_camera == camera) && (current_port == port)) {
            qCDebug(KIO_KAMERA_LOG) << "Configuration is same, nothing to do.";
            return KIO::WorkerResult::pass();
        }
        if (m_camera) {
            qCDebug(KIO_KAMERA_LOG) << "Configuration change detected";
            closeCamera();
            gp_camera_unref(m_camera);
            m_camera = nullptr;
            // WARNING Fix this
            // infoMessage( i18n("Reinitializing camera") );
        } else {
            qCDebug(KIO_KAMERA_LOG) << "Initializing camera";
            // WARNING Fix this
            // infoMessage( i18n("Initializing camera") );
        }
        // fetch abilities
        CameraAbilitiesList *abilities_list;
        gp_abilities_list_new(&abilities_list);
        gp_abilities_list_load(abilities_list, m_context);
        idx = gp_abilities_list_lookup_model(abilities_list, tocstr(camera));
        if (idx < 0) {
            gp_abilities_list_free(abilities_list);
            qCDebug(KIO_KAMERA_LOG) << "Unable to get abilities for model: " << camera;
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(idx)));
        }
        gp_abilities_list_get_abilities(abilities_list, idx, &m_abilities);
        gp_abilities_list_free(abilities_list);

        // fetch port
        GPPortInfoList *port_info_list;
        GPPortInfo port_info;
        gp_port_info_list_new(&port_info_list);
        gp_port_info_list_load(port_info_list);
        idx = gp_port_info_list_lookup_path(port_info_list, tocstr(port));

        /* Handle erronously passed usb:XXX,YYY */
        if ((idx < 0) && port.startsWith(QStringLiteral("usb:"))) {
            idx = gp_port_info_list_lookup_path(port_info_list, "usb:");
        }
        if (idx < 0) {
            gp_port_info_list_free(port_info_list);
            qCDebug(KIO_KAMERA_LOG) << "Unable to get port info for path: " << port;
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(idx)));
        }
        gp_port_info_list_get_info(port_info_list, idx, &port_info);

        current_camera = camera;
        current_port = port;
        // create a new camera object
        gpr = gp_camera_new(&m_camera);
        if (gpr != GP_OK) {
            gp_port_info_list_free(port_info_list);
            return KIO::WorkerResult::fail(KIO::ERR_UNKNOWN, QString::fromLocal8Bit(gp_result_as_string(gpr)));
        }

        // register gphoto2 callback functions
        gp_context_set_status_func(m_context, frontendCameraStatus, this);
        gp_context_set_progress_funcs(m_context, frontendProgressStart, frontendProgressUpdate, nullptr, this);
        // gp_camera_set_message_func(m_camera, ..., this)

        // set model and port
        gp_camera_set_abilities(m_camera, m_abilities);
        gp_camera_set_port_info(m_camera, port_info);
        gp_camera_set_port_speed(m_camera, 0); // TODO: the value needs to be configurable
        qCDebug(KIO_KAMERA_LOG) << "Opening camera model " << camera << " at " << port;

        gp_port_info_list_free(port_info_list);

        QString errstr;
        if (!openCamera(errstr)) {
            if (m_camera) {
                gp_camera_unref(m_camera);
            }
            m_camera = nullptr;
            qCDebug(KIO_KAMERA_LOG) << "Unable to init camera: " << errstr;
            return KIO::WorkerResult::fail(KIO::ERR_SERVICE_NOT_AVAILABLE, errstr);
        }
    }

    return KIO::WorkerResult::pass();
}

void KameraProtocol::reparseConfiguration()
{
    // we have no global config, do we?
}

// translate a simple text to a UDS entry
void KameraProtocol::translateTextToUDS(KIO::UDSEntry &udsEntry, const QString &fn, const char *text)
{
    udsEntry.clear();
    udsEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(fn));
    udsEntry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, fn);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_SIZE, strlen(text));
    udsEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, (S_IRUSR | S_IRGRP | S_IROTH));
    udsEntry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("text/plain"));
}

// translate a CameraFileInfo to a UDSFieldType
// which we can return as a directory listing entry
void KameraProtocol::translateFileToUDS(KIO::UDSEntry &udsEntry, const CameraFileInfo &info, const QString &name)
{
    udsEntry.clear();

    udsEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFREG);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(name));
    udsEntry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, name);

    if (info.file.fields & GP_FILE_INFO_SIZE) {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_SIZE, info.file.size);
    }

    if (info.file.fields & GP_FILE_INFO_MTIME) {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, info.file.mtime);
    } else {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_MODIFICATION_TIME, time(nullptr));
    }

    if (info.file.fields & GP_FILE_INFO_TYPE) {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QString::fromLatin1(info.file.type));
    }

    if (info.file.fields & GP_FILE_INFO_PERMISSIONS) {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, ((info.file.permissions & GP_FILE_PERM_READ) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0));
    } else {
        udsEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH);
    }

    // TODO: We do not handle info.preview in any way
}

// translate a directory name to a UDSFieldType
// which we can return as a directory listing entry
void KameraProtocol::translateDirectoryToUDS(KIO::UDSEntry &udsEntry, const QString &dirname)
{
    udsEntry.clear();

    udsEntry.fastInsert(KIO::UDSEntry::UDS_FILE_TYPE, S_IFDIR);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_NAME, path_quote(dirname));
    udsEntry.fastInsert(KIO::UDSEntry::UDS_DISPLAY_NAME, dirname);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_ACCESS, S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IWOTH | S_IXUSR | S_IXOTH | S_IXGRP);
    udsEntry.fastInsert(KIO::UDSEntry::UDS_MIME_TYPE, QStringLiteral("inode/directory"));
}

bool KameraProtocol::cameraSupportsDel()
{
    return (m_abilities.file_operations & GP_FILE_OPERATION_DELETE);
}

bool KameraProtocol::cameraSupportsPut()
{
    return (m_abilities.folder_operations & GP_FOLDER_OPERATION_PUT_FILE);
}

bool KameraProtocol::cameraSupportsPreview()
{
    return (m_abilities.file_operations & GP_FILE_OPERATION_PREVIEW);
}

int KameraProtocol::readCameraFolder(const QString &folder, CameraList *dirList, CameraList *fileList)
{
    qCDebug(KIO_KAMERA_LOG) << "KameraProtocol::readCameraFolder(" << folder << ")";

    int gpr;
    if ((gpr = gp_camera_folder_list_folders(m_camera, tocstr(folder), dirList, m_context)) != GP_OK) {
        return gpr;
    }
    if ((gpr = gp_camera_folder_list_files(m_camera, tocstr(folder), fileList, m_context)) != GP_OK) {
        return gpr;
    }
    return GP_OK;
}

void frontendProgressUpdate(GPContext * /*context*/, unsigned int /*id*/, float /*current*/, void *data)
{
    auto object = (KameraProtocol *)data;

    // This code will get the last chunk of data retrieved from the
    // camera and pass it to KIO, to allow progressive display
    // of the downloaded photo.

    const char *fileData = nullptr;
    long unsigned int fileSize = 0;

    // This merely returns us a pointer to gphoto's internal data
    // buffer -- there's no expensive memcpy
    if (!object->getFile()) {
        return;
    }
    gp_file_get_data_and_size(object->getFile(), &fileData, &fileSize);
    // make sure we're not sending zero-sized chunks (=EOF)
    if (fileSize > 0) {
        // XXX using assign() here causes segfault, prolly because
        // gp_file_free is called before chunkData goes out of scope
        QByteArray chunkDataBuffer = QByteArray::fromRawData(fileData + object->getFileSize(), fileSize - object->getFileSize());
        // Note: this will fail with sizes > 16MB ...
        object->data(chunkDataBuffer);
        object->processedSize(fileSize);
        chunkDataBuffer.clear();
        object->setFileSize(fileSize);
    }
}

unsigned int frontendProgressStart(GPContext * /*context*/, float totalsize, const char *status, void *data)
{
    auto object = (KameraProtocol *)data;
    /* libgphoto2 2.5 has resolved this already, no need for print */
    object->infoMessage(QString::fromLocal8Bit(status));
    object->totalSize((KIO::filesize_t)totalsize); // hack: call slot directly
    return GP_OK;
}

// this callback function is activated on every status message from gphoto2
static void frontendCameraStatus(GPContext * /*context*/, const char *status, void *data)
{
    auto object = (KameraProtocol *)data;
    object->infoMessage(QString::fromLocal8Bit(status));
}

#include "kamera.moc"
