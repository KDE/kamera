/*

    Copyright (C) 2001 The Kompany
		  2001-2003	Ilya Konstantinov <kde-devel@future.shiny.co.il>
		  2001-2007	Marcus Meissner <marcus@jet.franken.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include <qfile.h>
#include <qtextstream.h>

#include <kdebug.h>
#include <kcomponentdata.h>
#include <kstandarddirs.h>
#include <kconfig.h>
#include <kconfiggroup.h>
#include <klocale.h>
#include <kprotocolinfo.h>
#include <kio/global.h>
#include <kio/slaveconfig.h>
#include <kconfiggroup.h>

#include "config-kamera.h"

#include "kamera.h"

#define tocstr(x) ((x).toLocal8Bit())

#define MAXIDLETIME   30      /* seconds */

using namespace KIO;

extern "C"
{
	KDE_EXPORT int kdemain(int argc, char **argv);

	static void frontendCameraStatus(GPContext *context, const char *format, va_list args, void *data);
	static unsigned int frontendProgressStart(
		GPContext *context, float totalsize, const char *format,
		va_list args, void *data
	);
	static void frontendProgressUpdate(
		GPContext *context, unsigned int id, float current, void *data
	);
}

int kdemain(int argc, char **argv)
{
	KComponentData componentData("kio_kamera");

	if(argc != 4) {
		kDebug(7123) << "Usage: kio_kamera protocol "
			     "domain-socket1 domain-socket2" << endl;
		exit(-1);
	}

	KameraProtocol slave(argv[2], argv[3]);

	slave.dispatchLoop();

	return 0;
}

KameraProtocol::KameraProtocol(const QByteArray &pool, const QByteArray &app)
: SlaveBase("camera", pool, app),
m_camera(NULL)
{
	// attempt to initialize libgphoto2 and chosen camera (requires locking)
	// (will init m_camera, since the m_camera's configuration is empty)
	m_camera = 0;
	m_file = NULL;
	m_config = new KConfig(KProtocolInfo::config("camera"), KConfig::SimpleConfig);
	m_context = gp_context_new();
	actiondone = true;
	cameraopen = false;
	m_lockfile = KStandardDirs::locateLocal("tmp", "kamera");
	idletime = 0;
}

// This handler is getting called every second. We use it to do the
// delayed close of the camera.
// Logic is:
// 	- No more requests in the queue (signaled by actiondone) AND
//		- We are MAXIDLETIME seconds idle OR
//		- Another slave wants to have access to the camera.
//
// The existance of a lockfile is used to signify "please give up camera".
//
void KameraProtocol::special(const QByteArray&) {
	kDebug(7123) << "KameraProtocol::special() at " << getpid();

	if (!actiondone && cameraopen) {
		struct stat	stbuf;
		if ((-1!=::stat(m_lockfile.toUtf8(),&stbuf)) || (idletime++ >= MAXIDLETIME)) {
			kDebug(7123) << "KameraProtocol::special() closing camera.";
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
}

KameraProtocol::~KameraProtocol()
{
	kDebug(7123) << "KameraProtocol::~KameraProtocol()";
	delete m_config;
	if(m_camera) {
		closeCamera();
 		gp_camera_free(m_camera);
		m_camera = NULL;
	}
}

// initializes the camera for usage - should be done before operations over the wire
bool KameraProtocol::openCamera(QString &str) {
	idletime = 0;
	actiondone = true;
	if (!m_camera) {
		reparseConfiguration();
	} else {
		if (!cameraopen) {
			int ret, tries = 15;
			kDebug(7123) << "KameraProtocol::openCamera at " << getpid();
			while (tries--) {
				ret = gp_camera_init(m_camera, m_context);
				if (	(ret == GP_ERROR_IO_USB_CLAIM) || 
					(ret == GP_ERROR_IO_LOCK)) {
					// just create / touch if not there
					int fd = ::open(m_lockfile.toUtf8(),O_CREAT|O_WRONLY,0600);
					if (fd != -1) ::close(fd);
					::sleep(1);
					kDebug(7123) << "openCamera at " << getpid() << "- busy, ret " << ret << ", trying again.";
					continue;
				}
				if (ret == GP_OK) break;
				str = gp_result_as_string(ret);
				return false;
			}
			::unlink(m_lockfile.toUtf8());
			setTimeoutSpecialCommand(1);
			kDebug(7123) << "openCamera succeeded at " << getpid();
			cameraopen = true;
		}
	}
	return true;
}

// should be done after operations over the wire
void KameraProtocol::closeCamera(void)
{
	int gpr;

	if (!m_camera)
		return;

	kDebug(7123) << "KameraProtocol::closeCamera at " << getpid();
	if ((gpr=gp_camera_exit(m_camera,m_context))!=GP_OK) {
		kDebug(7123) << "closeCamera failed with " << gp_result_as_string(gpr);
	}
	// HACK: gp_camera_exit() in gp 2.0 does not close the port if there
	//       is no camera_exit function.
	gp_port_close(m_camera->port);
	cameraopen = false;
	return;
}

static QString fix_foldername(QString ofolder) {
	QString folder = ofolder;
	if (folder.length() > 1) {
		while ((folder.length()>1) && (folder.right(1) == "/"))
			folder = folder.left(folder.length()-1);
	}
	if (folder.length() == 0)
		folder = "/";
	return folder;
}

// The KIO slave "get" function (starts a download from the camera)
// The actual returning of the data is done in the frontend callback functions.
void KameraProtocol::get(const KUrl &url)
{
	kDebug(7123) << "KameraProtocol::get(" << url.path() << ")";

	CameraFileType fileType;
	int gpr;
	if (url.host().isEmpty()) {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}

	if(!openCamera()) {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}

	// fprintf(stderr,"get(%s)\n",url.path().toLatin1());

#define GPHOTO_TEXT_FILE(xx)						\
	if (!url.path().compare("/" #xx ".txt")) {			\
		CameraText xx;						\
		gpr = gp_camera_get_##xx(m_camera,  &xx, m_context);	\
		if (gpr != GP_OK) {					\
			error(KIO::ERR_DOES_NOT_EXIST, url.path());	\
			return;						\
		}							\
		QByteArray chunkDataBuffer = QByteArray::fromRawData(xx.text, strlen(xx.text));	\
		data(chunkDataBuffer);					\
		processedSize(strlen(xx.text));				\
		chunkDataBuffer.clear();                                \
		finished();						\
		return;							\
	}

	GPHOTO_TEXT_FILE(about);
	GPHOTO_TEXT_FILE(manual);
	GPHOTO_TEXT_FILE(summary);

#undef GPHOTO_TEXT_FILE
	// emit info message
	infoMessage( i18n("Retrieving data from camera <b>%1</b>", url.user()) );

	// Note: There's no need to re-read directory for each get() anymore
	gp_file_new(&m_file);

	// emit the total size (we must do it before sending data to allow preview)
	CameraFileInfo info;
	
	gpr = gp_camera_file_get_info(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), tocstr(url.fileName()), &info, m_context);
	if (gpr != GP_OK) {
		// fprintf(stderr,"Folder %s / File %s not found, gpr is %d\n",folder.toLatin1(), url.fileName().toLatin1(), gpr);
		gp_file_unref(m_file);
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		else
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return;
	}

	// at last, a proper API to determine whether a thumbnail was requested.
	if(cameraSupportsPreview() && metaData("thumbnail") == "1") {
		kDebug(7123) << "get() retrieving the thumbnail";
		fileType = GP_FILE_TYPE_PREVIEW;
		if (info.preview.fields & GP_FILE_INFO_SIZE)
			totalSize(info.preview.size);
		if (info.preview.fields & GP_FILE_INFO_TYPE)
			mimeType(info.preview.type);
	} else {
		kDebug(7123) << "get() retrieving the full-scale photo";
		fileType = GP_FILE_TYPE_NORMAL;
		if (info.file.fields & GP_FILE_INFO_SIZE)
			totalSize(info.file.size);
		if (info.preview.fields & GP_FILE_INFO_TYPE)
			mimeType(info.file.type);
	}

	// fetch the data
	m_fileSize = 0;
	gpr = gp_camera_file_get(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), tocstr(url.fileName()), fileType, m_file, m_context);
	if (	(gpr == GP_ERROR_NOT_SUPPORTED) &&
		(fileType == GP_FILE_TYPE_PREVIEW)
	) {
		// If we get here, the file info command information 
		// will either not be used, or still valid.
		fileType = GP_FILE_TYPE_NORMAL;
		gpr = gp_camera_file_get(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), tocstr(url.fileName()), fileType, m_file, m_context);
	}
	switch(gpr) {
		case GP_OK:
			break;
		case GP_ERROR_FILE_NOT_FOUND:
		case GP_ERROR_DIRECTORY_NOT_FOUND:
			gp_file_unref(m_file);
			m_file = NULL;
			error(KIO::ERR_DOES_NOT_EXIST, url.fileName());
			return ;
		default:
			gp_file_unref(m_file);
			m_file = NULL;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
			return;
	}
	// emit the mimetype
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
		kDebug(7123) << "get():: get_data_and_size failed.";
		gp_file_free(m_file);
		m_file = NULL;
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return;
	}
	// make sure we're not sending zero-sized chunks (=EOF)
	// also make sure we send only if the progress did not send the data
	// already.
	if ((fileSize > 0)  && (fileSize - m_fileSize)>0) {
		unsigned long written = 0;
		QByteArray chunkDataBuffer;

		// We need to split it up here. Someone considered it funny
		// to discard any data() larger than 16MB.
		//
		// So nearly any Movie will just fail....
		while (written < fileSize-m_fileSize) {
			unsigned long towrite = 1024*1024; // 1MB

			if (towrite > fileSize-m_fileSize-written)
				towrite = fileSize-m_fileSize-written;
			chunkDataBuffer = QByteArray::fromRawData(fileData + m_fileSize + written, towrite);
			processedSize(m_fileSize + written + towrite);
			data(chunkDataBuffer);
			chunkDataBuffer.clear();
			written += towrite;
		}
		m_fileSize = fileSize;
		setFileSize(fileSize);
	}

	finished();
	gp_file_unref(m_file); /* just unref, might be stored in fs */
	m_file = NULL;
}

// The KIO slave "stat" function.
void KameraProtocol::stat(const KUrl &url)
{
	kDebug(7123) << "stat(\"" << url.path() << "\")";
	
	if (url.path().isEmpty()) {
		KUrl rooturl(url);

		kDebug(7123) << "redirecting to /";
		rooturl.setPath("/");
		rooturl.setHost(url.host());
		rooturl.setUser(url.user());
		redirection(rooturl);
		finished();
		return;
	}

	if(url.path() == "/")
		statRoot();
	else
		statRegular(url);
}

// Implements stat("/") -- which always returns the same value.
void KameraProtocol::statRoot(void)
{
	KIO::UDSEntry entry;

	entry.insert( KIO::UDSEntry::UDS_NAME, QString::fromLocal8Bit("/"));

	entry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFDIR);

	entry.insert(KIO::UDSEntry::UDS_ACCESS,(S_IRUSR | S_IRGRP | S_IROTH |S_IWUSR | S_IWGRP | S_IWOTH));

	statEntry(entry);

	finished();
	// If we just do this call, timeout right away if no other requests are
	// pending. This is for the kdemm autodetection using media://camera
	idletime = MAXIDLETIME;
}

// Implements a regular stat() of a file / directory, returning all we know about it
void KameraProtocol::statRegular(const KUrl &url)
{
	KIO::UDSEntry entry;
	int gpr;

	kDebug(7123) << "statRegular(\"" << url.path() << "\")";
	if (openCamera() == false) {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}

	// fprintf(stderr,"statRegular(%s)\n",url.path().toLatin1());

	// Is "url" a directory?
	CameraList *dirList;
	gp_list_new(&dirList);
	kDebug(7123) << "statRegular() Requesting directories list for " << url.directory();

	gpr = gp_camera_folder_list_folders(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), dirList, m_context);
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		else
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		gp_list_free(dirList);
		return;
	}

#define GPHOTO_TEXT_FILE(xx)						\
	if (!url.path().compare("/"#xx".txt")) {			\
		CameraText xx;						\
		gpr = gp_camera_get_about(m_camera,  &xx, m_context);	\
		if (gpr != GP_OK) {					\
			error(KIO::ERR_DOES_NOT_EXIST, url.fileName());	\
			return;						\
		}							\
		translateTextToUDS(entry,#xx".txt",xx.text);		\
		statEntry(entry);					\
		finished();						\
		return;							\
	}
	GPHOTO_TEXT_FILE(about);
	GPHOTO_TEXT_FILE(manual);
	GPHOTO_TEXT_FILE(summary);
#undef GPHOTO_TEXT_FILE

	const char *name;
	for(int i = 0; i < gp_list_count(dirList); i++) {
		gp_list_get_name(dirList, i, &name);
		if (url.fileName().compare(name) == 0) {
			gp_list_free(dirList);
			KIO::UDSEntry entry;
			translateDirectoryToUDS(entry, url.fileName());
			statEntry(entry);
			finished();
			return;
		}
	}
	gp_list_free(dirList);

	// Is "url" a file?
	CameraFileInfo info;
	gpr = gp_camera_file_get_info(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), tocstr(url.fileName()), &info, m_context);
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		else
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return;
	}
	translateFileToUDS(entry, info, url.fileName());
	statEntry(entry);
	finished();
}

// The KIO slave "del" function.
void KameraProtocol::del(const KUrl &url, bool isFile)
{
	kDebug(7123) << "KameraProtocol::del(" << url.path() << ")";

	if(!openCamera()) {
		error(KIO::ERR_CANNOT_DELETE, url.fileName());
		return;
	}
	if (!cameraSupportsDel()) {
		error(KIO::ERR_CANNOT_DELETE, url.fileName());
		return;
	}
	if(isFile){
		CameraList *list;
		gp_list_new(&list);
		int ret;

		ret = gp_camera_file_delete(m_camera, tocstr(fix_foldername(url.directory(KUrl::AppendTrailingSlash))), tocstr(url.fileName()), m_context);

		if(ret != GP_OK) {
			error(KIO::ERR_CANNOT_DELETE, url.fileName());
		} else {
			finished();
		}
	}
}

// The KIO slave "listDir" function.
void KameraProtocol::listDir(const KUrl &url)
{
	kDebug(7123) << "KameraProtocol::listDir(" << url.path() << ")";

	if (url.host().isEmpty()) {
		KUrl xurl;
		// List the available cameras
		QStringList groupList = m_config->groupList();
		kDebug(7123) << "Found cameras: " << groupList.join(", ");
		QStringList::Iterator it;
		KIO::UDSEntry entry;


		/*
		 * What we do:
		 * - Autodetect cameras and remember them with their ports.
		 * - List all saved and possible offline cameras.
		 * - List all autodetected and not yet printed cameras.
		 */
		QMap<QString,QString>	ports, names;
		QMap<QString,int>	modelcnt;

		/* Autodetect USB cameras ... */
		GPContext *glob_context = NULL;
		int i, count;
		CameraList *list;
		CameraAbilitiesList *al;
		GPPortInfoList *il;

		gp_list_new (&list);
		gp_abilities_list_new (&al);
		gp_abilities_list_load (al, glob_context);
		gp_port_info_list_new (&il);
		gp_port_info_list_load (il);
		gp_abilities_list_detect (al, il, list, glob_context);
		gp_abilities_list_free (al);
		gp_port_info_list_free (il);

		count = gp_list_count (list);

		for (i = 0 ; i<count ; i++) {
			const char *model, *value;

			gp_list_get_name  (list, i, &model);
			gp_list_get_value (list, i, &value);
	
			ports[value] = model;
			// NOTE: We might get different ports than usb: later!
			if (strcmp(value,"usb:"))
				names[model] = value;

			/* Save them, even though we can autodetect them for
			 * offline listing.
			 */
			KConfigGroup cg(m_config, model);
			cg.writeEntry("Model", model);
			cg.writeEntry("Path", value);
			modelcnt[model]++;
		}
		gp_list_free (list);

		/* Avoid duplicated entry for usb: and usb:001,042 entries. */
		if (ports.contains("usb:") && names[ports["usb:"]]!="usb:")
			ports.remove("usb:");

		for (it = groupList.begin(); it != groupList.end(); it++) {
			QString m_cfgPath;
			if (*it == "<default>")
				continue;

			KConfigGroup cg(m_config, *it);
			m_cfgPath = cg.readEntry("Path");

			/* If autodetect by USB autodetect ... skip it here.
			 * We leave unattached USB cameras in here, because the user
			 * might plug them in later and does not want to press reload.
			 * We add them with port "usb:".
			 */
			if (modelcnt[*it] > 0)
				continue;

			entry.clear();
			entry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFDIR);
			entry.insert(KIO::UDSEntry::UDS_NAME,*it);
			entry.insert(KIO::UDSEntry::UDS_ACCESS,(S_IRUSR | S_IRGRP | S_IROTH |S_IWUSR | S_IWGRP | S_IWOTH));

			
			xurl.setProtocol("camera");
			xurl.setUser(*it);
			/* Avoid setting usb:xxx,yyy. */
			if (m_cfgPath.contains("usb:")>0) {
				names[*it] = "usb:";
				xurl.setHost("usb:");
			} else {
				xurl.setHost(m_cfgPath);
			}
			xurl.setPath("/");
			entry.insert(KIO::UDSEntry::UDS_URL,xurl.url());

			listEntry(entry, false);
		}
	
		QMap<QString,QString>::iterator portsit;

		for (portsit = ports.begin(); portsit != ports.end(); portsit++) {
			entry.clear();
			entry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFDIR);
			entry.insert(KIO::UDSEntry::UDS_NAME, portsit.value());

			entry.insert(KIO::UDSEntry::UDS_ACCESS,(S_IRUSR | S_IRGRP | S_IROTH |S_IWUSR | S_IWGRP | S_IWOTH));

			xurl.setProtocol("camera");
			xurl.setHost(portsit.key());
			xurl.setUser(portsit.value());
			xurl.setPath("/");
			entry.insert(KIO::UDSEntry::UDS_URL,xurl.url());

			listEntry(entry, false);
		}
		listEntry(entry, true);

		finished();
		return;
	}

	if (url.path().isEmpty()) {
		KUrl rooturl(url);

		kDebug(7123) << "redirecting to /";
		rooturl.setPath("/");
		rooturl.setHost(url.host());
		rooturl.setUser(url.user());
		redirection(rooturl);
		finished();
		return;
	}

	if (!openCamera()) {
		error(KIO::ERR_COULD_NOT_READ,url.path());
		return;
	}

	CameraList *dirList;
	CameraList *fileList;
	CameraList *specialList;
	gp_list_new(&dirList);
	gp_list_new(&fileList);
	gp_list_new(&specialList);
	int gpr;

	if (!url.path().compare("/")) {
		CameraText text;
		if (GP_OK == gp_camera_get_manual(m_camera, &text, m_context))
			gp_list_append(specialList,"manual.txt",NULL);
		if (GP_OK == gp_camera_get_about(m_camera, &text, m_context))
			gp_list_append(specialList,"about.txt",NULL);
		if (GP_OK == gp_camera_get_summary(m_camera, &text, m_context))
			gp_list_append(specialList,"summary.txt",NULL);
	}

	gpr = readCameraFolder(url.path(), dirList, fileList);
	if(gpr != GP_OK) {
		kDebug(7123) << "read Camera Folder failed:" << gp_result_as_string(gpr);
		gp_list_free(dirList);
		gp_list_free(fileList);
		gp_list_free(specialList);
		error(KIO::ERR_COULD_NOT_READ, gp_result_as_string(gpr));
		return;
	}

	totalSize(gp_list_count(specialList) + gp_list_count(dirList) + gp_list_count(fileList));

	KIO::UDSEntry entry;
	const char *name;

	for(int i = 0; i < gp_list_count(dirList); ++i) {
		gp_list_get_name(dirList, i, &name);
		translateDirectoryToUDS(entry, QString::fromLocal8Bit(name));
		listEntry(entry, false);
	}

	CameraFileInfo info;

	for(int i = 0; i < gp_list_count(fileList); ++i) {
		gp_list_get_name(fileList, i, &name);
		// we want to know more info about files (size, type...)
		gp_camera_file_get_info(m_camera, tocstr(url.path()), name, &info, m_context);
		translateFileToUDS(entry, info, QString::fromLocal8Bit(name));
		listEntry(entry, false);
	}
	if (!url.path().compare("/")) {
		CameraText text;
		if (GP_OK == gp_camera_get_manual(m_camera, &text, m_context)) {
			translateTextToUDS(entry, "manual.txt", text.text);
			listEntry(entry, false);
		}
		if (GP_OK == gp_camera_get_about(m_camera, &text, m_context)) {
			translateTextToUDS(entry, "about.txt", text.text);
			listEntry(entry, false);
		}
		if (GP_OK == gp_camera_get_summary(m_camera, &text, m_context)) {
			translateTextToUDS(entry, "summary.txt", text.text);
			listEntry(entry, false);
		}
	}


	gp_list_free(fileList);
	gp_list_free(dirList);
	gp_list_free(specialList);

	listEntry(entry, true); // 'entry' is not used in this case - we only signal list completion
	finished();
}

void KameraProtocol::setHost(const QString& host, quint16 port, const QString& user, const QString& pass )
{
	kDebug(7123) << "KameraProtocol::setHost(" << host << ", " << port << ", " << user << ", " << pass << ")";
	int gpr, idx;

	if (!host.isEmpty()) {
		kDebug(7123) << "model is " << user << ", port is " << host;
		if (m_camera) {
			kDebug(7123) << "Configuration change detected";
			closeCamera();
			gp_camera_unref(m_camera);
			m_camera = NULL;
			infoMessage( i18n("Reinitializing camera") );
		} else {
			kDebug(7123) << "Initializing camera";
			infoMessage( i18n("Initializing camera") );
		}
		// fetch abilities
		CameraAbilitiesList *abilities_list;
		gp_abilities_list_new(&abilities_list);
		gp_abilities_list_load(abilities_list, m_context);
		idx = gp_abilities_list_lookup_model(abilities_list, tocstr(user));
		if (idx < 0) {
			gp_abilities_list_free(abilities_list);
			kDebug(7123) << "Unable to get abilities for model: " << user;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(idx));
			return;
		}
		gp_abilities_list_get_abilities(abilities_list, idx, &m_abilities);
		gp_abilities_list_free(abilities_list);

		// fetch port
		GPPortInfoList *port_info_list;
		GPPortInfo port_info;
		gp_port_info_list_new(&port_info_list);
		gp_port_info_list_load(port_info_list);
		idx = gp_port_info_list_lookup_path(port_info_list, tocstr(host));

		/* Handle erronously passed usb:XXX,YYY */
		if ((idx < 0) && host.startsWith("usb:"))
			idx = gp_port_info_list_lookup_path(port_info_list, "usb:");
		if (idx < 0) {
			gp_port_info_list_free(port_info_list);
			kDebug(7123) << "Unable to get port info for path: " << host;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(idx));
			return;
		}
		gp_port_info_list_get_info(port_info_list, idx, &port_info);
		gp_port_info_list_free(port_info_list);

		// create a new camera object
		gpr = gp_camera_new(&m_camera);
		if(gpr != GP_OK) {
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
			return;
		}

		// register gphoto2 callback functions
		gp_context_set_status_func(m_context, frontendCameraStatus, this);
		gp_context_set_progress_funcs(m_context, frontendProgressStart, frontendProgressUpdate, NULL, this);
		// gp_camera_set_message_func(m_camera, ..., this)

		// set model and port
		gp_camera_set_abilities(m_camera, m_abilities);
		gp_camera_set_port_info(m_camera, port_info);
		gp_camera_set_port_speed(m_camera, 0); // TODO: the value needs to be configurable
		kDebug(7123) << "Opening camera model " << user << " at " << host;
		
		QString errstr;
		if (!openCamera(errstr)) {
			if (m_camera)
				gp_camera_unref(m_camera);
			m_camera = NULL;
			kDebug(7123) << "Unable to init camera: " << errstr;
			error(KIO::ERR_SERVICE_NOT_AVAILABLE, errstr);
			return;
		}
	}
}

void KameraProtocol::reparseConfiguration(void)
{
	// we have no global config, do we?
}

// translate a simple text to a UDS entry
void KameraProtocol::translateTextToUDS(KIO::UDSEntry &udsEntry, const QString &fn,
	const char *text
) {

	udsEntry.clear();

	udsEntry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFREG);

	udsEntry.insert(KIO::UDSEntry::UDS_NAME,fn);

	udsEntry.insert(KIO::UDSEntry::UDS_SIZE,strlen(text));

	udsEntry.insert(KIO::UDSEntry::UDS_ACCESS,(S_IRUSR | S_IRGRP | S_IROTH));
}

// translate a CameraFileInfo to a UDSFieldType which we can return as a directory listing entry
void KameraProtocol::translateFileToUDS(KIO::UDSEntry &udsEntry, const CameraFileInfo &info, QString name)
{

	udsEntry.clear();

	udsEntry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFREG);

	if (info.file.fields & GP_FILE_INFO_NAME)
			udsEntry.insert(KIO::UDSEntry::UDS_NAME,QString::fromLocal8Bit(info.file.name));
	else
		udsEntry.insert(KIO::UDSEntry::UDS_NAME,name);

	if (info.file.fields & GP_FILE_INFO_SIZE) {
		udsEntry.insert(KIO::UDSEntry::UDS_SIZE,info.file.size);
	}

	if (info.file.fields & GP_FILE_INFO_MTIME) {
		udsEntry.insert(KIO::UDSEntry::UDS_MODIFICATION_TIME,info.file.mtime);
	} else {
		udsEntry.insert(KIO::UDSEntry::UDS_MODIFICATION_TIME,time(NULL));
	}

	if (info.file.fields & GP_FILE_INFO_TYPE) {
		udsEntry.insert(KIO::UDSEntry::UDS_MIME_TYPE,QString::fromLatin1(info.file.type));
	}

	if (info.file.fields & GP_FILE_INFO_PERMISSIONS) {
		udsEntry.insert(KIO::UDSEntry::UDS_ACCESS,((info.file.permissions & GP_FILE_PERM_READ) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0));
	} else {
		udsEntry.insert(KIO::UDSEntry::UDS_ACCESS,S_IRUSR | S_IRGRP | S_IROTH);
	}

	// TODO: We do not handle info.preview in any way
}

// translate a directory name to a UDSFieldType which we can return as a directory listing entry
void KameraProtocol::translateDirectoryToUDS(KIO::UDSEntry &udsEntry, const QString &dirname)
{

	udsEntry.clear();

	udsEntry.insert(KIO::UDSEntry::UDS_FILE_TYPE,S_IFDIR);
	udsEntry.insert(KIO::UDSEntry::UDS_NAME,dirname);
	udsEntry.insert(KIO::UDSEntry::UDS_ACCESS,S_IRUSR | S_IRGRP | S_IROTH |S_IWUSR | S_IWGRP | S_IWOTH);
        udsEntry.insert(KIO::UDSEntry::UDS_MIME_TYPE, QString("inode/directory"));
}

bool KameraProtocol::cameraSupportsDel(void)
{
        return (m_abilities.file_operations & GP_FILE_OPERATION_DELETE);
}

bool KameraProtocol::cameraSupportsPut(void)
{
        return (m_abilities.folder_operations & GP_FOLDER_OPERATION_PUT_FILE);
}

bool KameraProtocol::cameraSupportsPreview(void)
{
	return (m_abilities.file_operations & GP_FILE_OPERATION_PREVIEW);
}

int KameraProtocol::readCameraFolder(const QString &folder, CameraList *dirList, CameraList *fileList)
{
	kDebug(7123) << "KameraProtocol::readCameraFolder(" << folder << ")";

	int gpr;
	if((gpr = gp_camera_folder_list_folders(m_camera, tocstr(folder), dirList, m_context)) != GP_OK)
		return gpr;
	if((gpr = gp_camera_folder_list_files(m_camera, tocstr(folder), fileList, m_context)) != GP_OK)
		return gpr;
	return GP_OK;
}

void frontendProgressUpdate(
	GPContext * /*context*/, unsigned int /*id*/, float /*current*/, void *data 
) {
	KameraProtocol *object = (KameraProtocol*)data;

	// This code will get the last chunk of data retrieved from the
	// camera and pass it to KIO, to allow progressive display
	// of the downloaded photo.

	const char *fileData = NULL;
	long unsigned int fileSize = 0;

	// This merely returns us a pointer to gphoto's internal data
	// buffer -- there's no expensive memcpy
	if (!object->getFile())
		return;
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

unsigned int frontendProgressStart(
	GPContext * /*context*/, float totalsize, const char *format, va_list args,
	void *data
) {
	KameraProtocol *object = (KameraProtocol*)data;
	char *status;

	/* We must copy the va_list to walk it twice, or all hell 
	 * breaks loose on non-i386 platforms.
	 */
#if defined(HAVE_VA_COPY) || defined(HAVE___VA_COPY)
	va_list xvalist;
# ifdef HAVE_VA_COPY
	va_copy(xvalist, args);
# elif HAVE___VA_COPY
	__va_copy(xvalist, args);
# endif
	int size=vsnprintf(NULL, 0, format, xvalist);
	if(size<=0)
		return GP_OK; // vsnprintf is broken, better don't do anything.

	status=new char[size+1];
# ifdef HAVE_VA_COPY
	va_copy(xvalist, args);
# elif HAVE___VA_COPY
	__va_copy(xvalist, args);
# endif
	vsnprintf(status, size+1, format, xvalist);
#else
	/* We cannot copy the va_list, so make sure we 
	 * walk it just _once_.
	 */
	status=new char[300];
	vsnprintf(status, 300, format, args);
#endif

	object->infoMessage(QString::fromLocal8Bit(status));
	delete [] status;
	object->totalSize((int)totalsize); // hack: call slot directly
	return GP_OK;
}

// this callback function is activated on every status message from gphoto2
static void frontendCameraStatus(GPContext * /*context*/, const char *format, va_list args, void *data)
{
	KameraProtocol *object = (KameraProtocol*)data;
	char *status;

	/* We must copy the va_list to walk it twice, or all hell 
	 * breaks loose on non-i386 platforms.
	 */
#if defined(HAVE_VA_COPY) || defined(HAVE___VA_COPY)
	va_list xvalist;
# ifdef HAVE_VA_COPY
	va_copy(xvalist, args);
# elif HAVE___VA_COPY
	__va_copy(xvalist, args);
# endif
	int size=vsnprintf(NULL, 0, format, xvalist);
	if(size<=0)
		return; // vsnprintf is broken, better don't do anything.

	status=new char[size+1];
# ifdef HAVE_VA_COPY
	va_copy(xvalist, args);
# elif HAVE___VA_COPY
	__va_copy(xvalist, args);
# endif
	vsnprintf(status, size+1, format, xvalist);
#else
	/* We cannot copy the va_list, so make sure we 
	 * walk it just _once_.
	 */
	status=new char[300];
	vsnprintf(status, 300, format, args);
#endif
	object->infoMessage(QString::fromLocal8Bit(status));
	delete [] status;
}
