#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#include <qfile.h>
#include <qtextstream.h>

#include <kdebug.h>
#include <kinstance.h>
#include <kconfig.h>
#include <ksimpleconfig.h>
#include <klocale.h>
#include <kprotocolinfo.h>
#include <kio/slaveconfig.h>

#include "kamera.h"

#define tocstr(x) ((x).local8Bit())

#define LOCK_DIR "/var/lock"

using namespace KIO;

extern "C"
{
	int kdemain(int argc, char **argv);
}

int kdemain(int argc, char **argv)
{
	KInstance instance("kio_kamera");

	if(argc != 4) {
		kdDebug() << "Usage: kio_kamera protocol "
			     "domain-socket1 domain-socket2" << endl;
		exit(-1);
	}

	KameraProtocol slave(argv[2], argv[3]);

	slave.dispatchLoop();

	return 0;
}

QMap<Camera *, KameraProtocol *> KameraProtocol::m_cameraToProtocol;

KameraProtocol::KameraProtocol(const QCString &pool, const QCString &app)
: SlaveBase("camera", pool, app),
m_camera(NULL)
{
	int gpr;
        #ifndef nDEBUG
        int debug = GP_DEBUG_HIGH;
        #else
        int debug = GP_DEBUG_NONE;
        #endif
        gp_debug_set_level(debug);
        /*
	if((gpr = gp_init(GP_DEBUG_LOW)) != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
	}*/

	// register gphoto2 callback functions
	gp_frontend_register(
		frontendCameraStatus,
		frontendCameraProgress,
		0, // CameraMessage
		0, // CameraConfirm
		0  // CameraPrompt
	);
	

	// attempt to initialize libgphoto2 and chosen camera (requires locking)
	// (will init m_camera, since the m_camera's configuration is empty)
	m_camera = 0;
	
	m_config = new KSimpleConfig(KProtocolInfo::config("camera"));
}

KameraProtocol::~KameraProtocol()
{
	if(m_camera)
 		gp_camera_free(m_camera);
		
	gp_exit();
}

// returns the filename of the UUCP lock for our device
QString KameraProtocol::lockFileName() {
	const char *path;
	gp_camera_get_port_path(m_camera, &path);
	QString device(path);
	device = device.mid(device.findRev('/')+1);
	if (device.isEmpty())
		return QString();
	else
		return QString(QString::fromLatin1(LOCK_DIR) + QString::fromLatin1("/LCK..") + device);
}

// initializes the camera for usage - should be done before operations over the wire
bool KameraProtocol::openCamera(void) {
	int gpr;
	QFile lockfile;
	
	if (!m_camera)
		reparseConfiguration();
		
	lock();
	

	return true;
}

// removes the lock - should be done after operations over the wire
void KameraProtocol::closeCamera(void)
{
	unlock();
	
	return;
}

// implements UUCP locking (conforming to the FHS standard)
void KameraProtocol::lock()
{
	// libgphoto2_port provides UUCP locks now
#ifdef KAMERA_UUCP_LOCKING
	QString filename = lockFileName();
	if (!filename.isEmpty()) {
		QFile lockfile(filename);
		if (lockfile.exists()) {
			if (lockfile.open(IO_ReadOnly)) {
				kdDebug() << "Lock was succesfully opened for reading" << endl;
				QTextStream stream(&lockfile);
				QString pid = stream.readLine().left(10).stripWhiteSpace();
				bool ok;
				ulong lock_pid = pid.toULong(&ok);
				lockfile.close();
				if (ok) {
					kdDebug() << "Lock currently owned by " << lock_pid << endl;
					while (!ok || ((lock_pid != getpid()) && ((kill(lock_pid, 0) == 0) || (errno != ESRCH)))) {
						kdDebug() << "openCamera waiting for lock by PID " << lock_pid << " to release" << endl;
						infoMessage( i18n( "Device is busy. Waiting..." ) );
					
						// wait for the lock to release
						sleep(1);
						if (lockfile.open(IO_ReadOnly)) {
							QString pid = stream.readLine();
							pid = pid.stripWhiteSpace();
							lock_pid = pid.toULong(&ok);
							lockfile.close();
						} else
							break;
					}
				}
			}
		}
		if (lockfile.open(IO_WriteOnly | IO_Truncate)) {
			kdDebug() << "Lock was succesfully opened for writing" << endl;
			QTextStream stream(&lockfile);
			stream << QString().setNum(getpid()).rightJustify(10, ' ') << endl;
			lockfile.close();
		} else {
			kdDebug() << "openCamera unable to create a lock file " << filename << endl;
		}
	}
#endif
}

// implements UUCP locking (conforming to the FHS standard)
void KameraProtocol::unlock()
{
	// libgphoto2_port provides UUCP locks now
#ifdef KAMERA_UUCP_LOCKING
	QString device = lockFileName();
	if (!device.isEmpty()) {
		QFile lockfile(device);
		if (lockfile.open(IO_ReadOnly)) {
			QTextStream stream(&lockfile);
			QString pid = stream.readLine().left(10).stripWhiteSpace();
			bool ok;
			ulong lock_pid = pid.toULong(&ok);
			if (!ok)
				kdDebug() << "Invalid PID (" << lock_pid << ") in lock file " << device << " -- Not erasing" << endl;
			else if (lock_pid != getpid())
				kdDebug() << "Alien PID (" << lock_pid << ") in lock file " << device << " -- Not erasing" << endl;
			else
				lockfile.remove();
		} else
			kdDebug() << "Lock file " << device << " mysteriously vanished before closeCamera()" << endl;
	}
#endif
}

// The KIO slave "get" function (starts a download from the camera)
// The actual returning of the data is done in the frontend callback functions.
void KameraProtocol::get(const KURL &url)
{
	kdDebug() << "KameraProtocol::get(" << url.path() << ")" << endl;

	CameraFileType fileType;
	int gpr;

	if(openCamera() == false)
		return;
		
	if (url.host().isEmpty()) {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}
	
	// emit info message
	const char *model;
	gp_camera_get_model(m_camera, &model);
	infoMessage( i18n("Retrieving data from camera <b>%1</b>").arg(QString::fromLocal8Bit(model)) );

	// Note: There's no need to re-read directory for each get() anymore
	CameraFile *cameraFile; 
	gp_file_new(&cameraFile);

	// emit the mimetype
	const char *fileMimeType;
	gp_file_get_mime_type(cameraFile, &fileMimeType);
	mimeType(fileMimeType);

	// emit the total size (we must do it before sending data to allow preview)
	CameraFileInfo info;
	gpr = gp_camera_file_get_info(m_camera, tocstr(url.directory(false)), tocstr(url.fileName()), &info);
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		closeCamera();
		return;
	}

	// at last, a proper API to determine whether a thumbnail was requested.
	if(cameraSupportsPreview() && metaData("thumbnail") == "1") {
		kdDebug() << "get() retrieving the thumbnail" << endl;
		fileType = GP_FILE_TYPE_PREVIEW;
		if (info.preview.fields & GP_FILE_INFO_SIZE)
			totalSize(info.preview.size);
	} else {
		kdDebug() << "get() retrieving the full-scale photo" << endl;
		fileType = GP_FILE_TYPE_NORMAL;
		if (info.file.fields & GP_FILE_INFO_SIZE)
			totalSize(info.file.size);
	}
	
	// fetch the data
	fileSize = 0;
	gpr = gp_camera_file_get(m_camera, tocstr(url.directory(false)), tocstr(url.filename()), fileType, cameraFile);

	switch(gpr) {
	case GP_OK:
		break;
	case GP_ERROR_FILE_NOT_FOUND:
	case GP_ERROR_DIRECTORY_NOT_FOUND:
		gp_file_free(cameraFile);
		error(KIO::ERR_DOES_NOT_EXIST, url.filename());
		closeCamera();
		return ;
	default:
		gp_file_free(cameraFile);
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		closeCamera();
		return;
	}

	data(QByteArray()); // signal an EOF
	finished();

	gp_file_free(cameraFile);

	closeCamera();
}

// The KIO slave "stat" function.
void KameraProtocol::stat(const KURL &url)
{
	kdDebug() << "KameraProtocol::stat(" << url.path() << ")" << endl;

	if(url.path() == "/") {
		statRoot();
	} else {
		statRegular(url);
	}
}

// Implements stat("/") -- which always returns the same value.
void KameraProtocol::statRoot(void)
{
	UDSEntry entry;
 	UDSAtom atom;

	atom.m_uds = UDS_NAME;
	atom.m_str = "/";
	entry.append(atom);  

	atom.m_uds = UDS_FILE_TYPE;
	atom.m_long = S_IFDIR;
	entry.append(atom);    	

	atom.m_uds = UDS_ACCESS;
	atom.m_long = S_IRUSR | S_IRGRP | S_IROTH |
			S_IWUSR | S_IWGRP | S_IWOTH;
	entry.append(atom);

	statEntry(entry);

	finished();
}

// Implements a regular stat() of a file / directory, returning all we know about it
void KameraProtocol::statRegular(const KURL &url)
{
	UDSEntry entry;
	int gpr;

	if (openCamera() == false)
		return;

	// Is "url" a directory?
	CameraList *dirList;
	gp_list_new(&dirList);
	kdDebug() << "statRegular() Requesting directories list for " << url.directory() << endl;
	gpr = gp_camera_folder_list_folders(m_camera, tocstr(url.directory()), dirList);
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		gp_list_free(dirList);
		return;
	}

	const char *name;
	for(int i = 0; i < gp_list_count(dirList); i++) {
		gp_list_get_name(dirList, i, &name);
		if (url.filename().compare(name) == 0) {
			gp_list_free(dirList);
			UDSEntry entry;
			translateDirectoryToUDS(entry, url.fileName());
			statEntry(entry);
			finished();
			closeCamera();
			return;
		}
	}
	gp_list_free(dirList);
	
	// Is "url" a file?
	CameraFileInfo info;
	gpr = gp_camera_file_get_info(m_camera, tocstr(url.directory(false)), tocstr(url.fileName()), &info);
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		closeCamera();
		return;
	}

	translateFileToUDS(entry, info);
	statEntry(entry);
	finished();
	closeCamera();
}

// The KIO slave "del" function.
void KameraProtocol::del(const KURL &url, bool isFile)
{
	kdDebug() << "KameraProtocol::del(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	if(cameraSupportsDel() && isFile){
		CameraList *list;
		gp_list_new(&list);
		int ret;
 
		ret = gp_camera_file_delete(m_camera, tocstr(url.directory(false)), tocstr(url.filename()));

		if(ret != GP_OK) {
			error(KIO::ERR_CANNOT_DELETE, url.filename());
		} else {
			finished();
		}
	}

	closeCamera();
}

// The KIO slave "listDir" function.
void KameraProtocol::listDir(const KURL &url)
{
	kdDebug() << "KameraProtocol::listDir(" << url.path() << ")" << endl;

	if (url.host().isEmpty()) {
		// List the available cameras
		QStringList groupList = m_config->groupList(); 
		QStringList::Iterator it;
		UDSEntry entry;
		UDSAtom atom;
		for (it = groupList.begin(); it != groupList.end(); it++) {
			if (*it != "<default>") {
				entry.clear();
				atom.m_uds = UDS_FILE_TYPE; // UDS type
				atom.m_long = S_IFDIR; // directory
				entry.append(atom);

				atom.m_uds = UDS_NAME;
				atom.m_str = *it;
				entry.append(atom);

				atom.m_uds = UDS_ACCESS;
				atom.m_long = S_IRUSR | S_IRGRP | S_IROTH |
					S_IWUSR | S_IWGRP | S_IWOTH;
				entry.append(atom);
				
				atom.m_uds = UDS_URL;
				atom.m_str = QString::fromLatin1("camera://") + *it + QString::fromLatin1("/");
				entry.append(atom);
				
				listEntry(entry, false);
			}
		}
		listEntry(entry, true);
		finished();
		return;
	}
	
	if(openCamera() == false)
		return;

	CameraList *dirList;
	CameraList *fileList;
	gp_list_new(&dirList);
	gp_list_new(&fileList);
	int gpr;

	gpr = readCameraFolder(url.path(), dirList, fileList);
	if(gpr != GP_OK) {
		gp_list_free(dirList);
		gp_list_free(fileList);
		error(KIO::ERR_COULD_NOT_READ, gp_result_as_string(gpr));
		return;
	}

	totalSize(gp_list_count(dirList) + gp_list_count(fileList));

	UDSEntry entry;
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
		gp_camera_file_get_info(m_camera, tocstr(url.path()), name, &info);
		translateFileToUDS(entry, info);
		listEntry(entry, false);
	}

	gp_list_free(fileList);
	gp_list_free(dirList);

	listEntry(entry, true); // 'entry' is not used in this case - we only signal list completion
	finished();

	closeCamera();
}

void KameraProtocol::setHost(const QString& host, int port, const QString& user, const QString& pass )
{
	int gpr;
	
	// Read configuration
	QString m_cfgModel = config()->readEntry("Model");
	QString m_cfgPath = config()->readEntry("Path");
	
	if (m_camera) {
		kdDebug() << "Configuration change detected" << endl;
		m_cameraToProtocol.remove(m_camera);
		gp_camera_unref(m_camera);
		infoMessage( i18n("Reinitializing camera") );
	} else {
		kdDebug() << "Initializing camera" << endl;
		infoMessage( i18n("Initializing camera") );
	}

	// create a new Camera object
	gpr = gp_camera_new(&m_camera);
	if(gpr != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return;
	}
	gp_camera_set_model(m_camera, tocstr(m_cfgModel));
	gp_camera_set_port_path(m_camera, tocstr(m_cfgPath));
		
	kdDebug() << "Opening camera model " << m_cfgModel << " at " << m_cfgPath << endl;

	// initialize the camera (might take time on a non-existant or disconnected camera)
	lock();
	gpr = gp_camera_init(m_camera);
	unlock();
		
	if(gpr != GP_OK) {
		m_cfgModel = ""; // force a configuration reload (since init didn't complete)
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return;
	}

	// Add Camera => KameraProtocol mapping, so that frontend callbacks could
	// retrieve the original 'this' object.
	m_cameraToProtocol[m_camera] = this;
}

void KameraProtocol::reparseConfiguration(void)
{
	// we have no global config, do we?
}

// translate a CameraFileInfo to a UDSEntry which we can return as a directory listing entry
void KameraProtocol::translateFileToUDS(UDSEntry &udsEntry, const CameraFileInfo &info)
{
	UDSAtom atom;

	udsEntry.clear();

	atom.m_uds = UDS_FILE_TYPE; // UDS type
	atom.m_long = S_IFREG; // file
	udsEntry.append(atom);
	
	if (info.file.fields & GP_FILE_INFO_NAME) {
		atom.m_uds = UDS_NAME;
		atom.m_str = QString::fromLocal8Bit(info.file.name);
		udsEntry.append(atom);
	}

	if (info.file.fields & GP_FILE_INFO_SIZE) {
		atom.m_uds = UDS_SIZE;
		atom.m_long = info.file.size;
		udsEntry.append(atom);
	}
	
	if (info.file.fields & GP_FILE_INFO_TYPE) {
		atom.m_uds = UDS_MIME_TYPE;
		atom.m_str = QString::fromLatin1(info.file.type);
		udsEntry.append(atom);
	}
	
	if (info.file.fields & GP_FILE_INFO_PERMISSIONS) {
		atom.m_uds = UDS_ACCESS;
		atom.m_long = 0;
		atom.m_long |= (info.file.permissions & GP_FILE_PERM_READ) ? (S_IRUSR | S_IRGRP | S_IROTH) : 0;
		// we cannot represent individual FP_FILE_PERM_DELETE permission in the Unix access scheme
		// since the parent directory's write permission defines that
		udsEntry.append(atom);
	} else {
		// basic permissions, in case the camera doesn't provide permissions info
		atom.m_uds = UDS_ACCESS;
		atom.m_long = S_IRUSR | S_IRGRP | S_IROTH;
		udsEntry.append(atom);
	}

	// TODO: We do not handle info.preview in any way
}

// translate a directory name to a UDSEntry which we can return as a directory listing entry
void KameraProtocol::translateDirectoryToUDS(UDSEntry &udsEntry, const QString &dirname)
{
	UDSAtom atom;

	udsEntry.clear();

	atom.m_uds = UDS_FILE_TYPE; // UDS type
	atom.m_long = S_IFDIR; // directory
	udsEntry.append(atom);

	atom.m_uds = UDS_NAME;
	atom.m_str = dirname;
	udsEntry.append(atom);

	atom.m_uds = UDS_ACCESS;
	atom.m_long = S_IRUSR | S_IRGRP | S_IROTH |
			S_IWUSR | S_IWGRP | S_IWOTH;
	udsEntry.append(atom);
}

bool KameraProtocol::cameraSupportsDel(void)
{
        return (m_camera->abilities->file_operations &&
                        GP_FILE_OPERATION_DELETE);
}

bool KameraProtocol::cameraSupportsPut(void)
{
        return (m_camera->abilities->folder_operations &&
                        GP_FOLDER_OPERATION_PUT_FILE);
}

bool KameraProtocol::cameraSupportsPreview(void)
{
	return (m_camera->abilities->file_operations && 
			GP_FILE_OPERATION_PREVIEW);
}

int KameraProtocol::readCameraFolder(const QString &folder, CameraList *dirList, CameraList *fileList)
{
	kdDebug() << "KameraProtocol::readCameraFolder(" << folder << ")" << endl;

	int gpr;

	if((gpr = gp_camera_folder_list_folders(m_camera, tocstr(folder), dirList)) != GP_OK)
		return gpr;
	
	if((gpr = gp_camera_folder_list_files(m_camera, tocstr(folder), fileList)) != GP_OK)
		return gpr;

	return GP_OK;
}

// this callback function is activated on every status message from gphoto2
int KameraProtocol::frontendCameraStatus(Camera *camera, char *status)
{
	if (KameraProtocol *object = m_cameraToProtocol[camera])
		object->infoMessage(QString::fromLocal8Bit(status));
        return 0; /// #### what should we return here ?
}

// this callback function is activated on every new chunk of data read
int KameraProtocol::frontendCameraProgress(Camera *camera, CameraFile *file, float progress)
{
	if (KameraProtocol *object = m_cameraToProtocol[camera]) {
		char *chunkData;
		long int chunkSize;
		gp_file_get_last_chunk(file, &chunkData, &chunkSize);
		// make sure we're not sending zero-sized chunks (=EOF)
		if (chunkSize > 0) {
			object->fileSize += chunkSize;
			// XXX using assign() here causes segfault, prolly because
			// gp_file_free is called before chunkData goes out of scope
			QByteArray chunkDataBuffer;
			chunkDataBuffer.setRawData(chunkData, chunkSize);
			object->data(chunkDataBuffer);
			object->processedSize(object->fileSize);
			chunkDataBuffer.resetRawData(chunkData, chunkSize);
		}
	}
#warning what should we return here ?
        return 0; /// #### what should we return here ?
}
