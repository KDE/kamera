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

#include <config.h>

#include "kamera.h"

#define tocstr(x) ((x).local8Bit())

using namespace KIO;

extern "C"
{
	int kdemain(int argc, char **argv);

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

KameraProtocol::KameraProtocol(const QCString &pool, const QCString &app)
: SlaveBase("camera", pool, app),
m_camera(NULL)
{
	// attempt to initialize libgphoto2 and chosen camera (requires locking)
	// (will init m_camera, since the m_camera's configuration is empty)
	m_camera = 0;

	
	m_config = new KSimpleConfig(KProtocolInfo::config("camera"));
	autoDetect();
	m_context = gp_context_new();
}

KameraProtocol::~KameraProtocol()
{
  delete m_config;
	if(m_camera) {
		closeCamera();
 		gp_camera_free(m_camera);
	}
}

void KameraProtocol::autoDetect(void)
{
	GPContext *glob_context = NULL;
	QStringList groupList = m_config->groupList();

        int i, count;
        CameraList list;
        CameraAbilitiesList *al;
        GPPortInfoList *il;
        const char *model, *value;

        gp_abilities_list_new (&al);
        gp_abilities_list_load (al, glob_context);
        gp_port_info_list_new (&il);
        gp_port_info_list_load (il);
        gp_abilities_list_detect (al, il, &list, glob_context);
        gp_abilities_list_free (al);
        gp_port_info_list_free (il);

        count = gp_list_count (&list);
	
	for (i = 0 ; i<count ; i++) {
		gp_list_get_name  (&list, i, &model);
		gp_list_get_value (&list, i, &value);

		if (groupList.contains(model))
			continue;
		kdDebug() << "Adding " << model << " at " << value << endl;
		m_config->setGroup(model);
		m_config->writeEntry("Model",model);
		m_config->writeEntry("Path",value);
	}
}


// initializes the camera for usage - should be done before operations over the wire
bool KameraProtocol::openCamera(void) {
	if (!m_camera)
		reparseConfiguration();
	return true;
}

// should be done after operations over the wire
void KameraProtocol::closeCamera(void)
{
	int gpr;
	if ((gpr=gp_camera_exit(m_camera,m_context))!=GP_OK) {
		kdDebug() << "closeCamera failed with " << gp_result_as_string(gpr) << endl;
	}
	// HACK: gp_camera_exit() in gp 2.0 does not close the port if there
	//       is no camera_exit function.
	gp_port_close(m_camera->port);
	return;
}

// The KIO slave "get" function (starts a download from the camera)
// The actual returning of the data is done in the frontend callback functions.
void KameraProtocol::get(const KURL &url)
{
	kdDebug() << "KameraProtocol::get(" << url.path() << ")" << endl;

	CameraFileType fileType;
	int gpr;
	if (url.host().isEmpty()) {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}

	if(!openCamera())
		return;

	// emit info message
	infoMessage( i18n("Retrieving data from camera <b>%1</b>").arg(m_cfgModel) );

	// Note: There's no need to re-read directory for each get() anymore
	gp_file_new(&m_file);

	// emit the total size (we must do it before sending data to allow preview)
	CameraFileInfo info;
	gpr = gp_camera_file_get_info(m_camera, tocstr(url.directory(false)), tocstr(url.fileName()), &info, m_context);
	if (gpr != GP_OK) {
		gp_file_free(m_file);
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		closeCamera();
		return;
	}

	// at last, a proper API to determine whether a thumbnail was requested.
	if(cameraSupportsPreview() && metaData("thumbnail") == "1") {
		kdDebug() << "get() retrieving the thumbnail" << endl;
		fileType = GP_FILE_TYPE_PREVIEW;
		if (info.preview.fields & GP_FILE_INFO_SIZE) {
			totalSize(info.preview.size);
		}
	} else {
		kdDebug() << "get() retrieving the full-scale photo" << endl;
		fileType = GP_FILE_TYPE_NORMAL;
		if (info.file.fields & GP_FILE_INFO_SIZE) {
			totalSize(info.file.size);
		}
	}
	
	// fetch the data
	m_fileSize = 0;
	gpr = gp_camera_file_get(m_camera, tocstr(url.directory(false)), tocstr(url.filename()), fileType, m_file, m_context);
	switch(gpr) {
		case GP_OK:
			break;
		case GP_ERROR_FILE_NOT_FOUND:
		case GP_ERROR_DIRECTORY_NOT_FOUND:
			gp_file_free(m_file);
			error(KIO::ERR_DOES_NOT_EXIST, url.filename());
			closeCamera();
			return ;
		default:
			gp_file_free(m_file);
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
			closeCamera();
			return;
	}
	const char *xdata;
	unsigned long int xsize;
	gp_file_get_data_and_size(m_file,&xdata,&xsize);
	QByteArray	barr(xsize);
	barr.setRawData(xdata,xsize);
	data(barr);
	barr.resetRawData(xdata,xsize);

	// emit the mimetype
	// NOTE: we must first get the file, so that CameraFile->name would be set
	const char *fileMimeType;
	gp_file_get_mime_type(m_file, &fileMimeType);
	mimeType(fileMimeType);

	finished();
	gp_file_free(m_file);
	closeCamera();
}

// The KIO slave "stat" function.
void KameraProtocol::stat(const KURL &url)
{
	kdDebug() << "KameraProtocol("<<this<<")::stat(" << url.path() << ")" << endl;

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
	gpr = gp_camera_folder_list_folders(m_camera, tocstr(url.directory()), dirList, m_context);
	closeCamera();
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
			return;
		}
	}
	gp_list_free(dirList);
	
	// Is "url" a file?
	CameraFileInfo info;
	gpr = gp_camera_file_get_info(m_camera, tocstr(url.directory(false)), tocstr(url.fileName()), &info, m_context);
	closeCamera();
	if (gpr != GP_OK) {
		if ((gpr == GP_ERROR_FILE_NOT_FOUND) || (gpr == GP_ERROR_DIRECTORY_NOT_FOUND))
			error(KIO::ERR_DOES_NOT_EXIST, url.path());
		return;
	}
	translateFileToUDS(entry, info);
	statEntry(entry);
	finished();
}

// The KIO slave "del" function.
void KameraProtocol::del(const KURL &url, bool isFile)
{
	kdDebug() << "KameraProtocol::del(" << url.path() << ")" << endl;

	if(!openCamera())
		return;
	if (!cameraSupportsDel()) {
		error(KIO::ERR_CANNOT_DELETE, url.filename());
		return;
	}
	if(isFile){
		CameraList *list;
		gp_list_new(&list);
		int ret;
 
		ret = gp_camera_file_delete(m_camera, tocstr(url.directory(false)), tocstr(url.filename()), m_context);

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
		kdDebug() << "Found cameras: " << groupList.join(", ") << endl;
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
	
	if (!openCamera())
		return;

	CameraList *dirList;
	CameraList *fileList;
	gp_list_new(&dirList);
	gp_list_new(&fileList);
	int gpr;

	gpr = readCameraFolder(url.path(), dirList, fileList);
	if(gpr != GP_OK) {
		kdDebug() << "read Camera Folder failed:" << gp_result_as_string(gpr) <<endl;
		closeCamera();
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
		gp_camera_file_get_info(m_camera, tocstr(url.path()), name, &info, m_context);
		translateFileToUDS(entry, info);
		listEntry(entry, false);
	}
	closeCamera();

	gp_list_free(fileList);
	gp_list_free(dirList);

	listEntry(entry, true); // 'entry' is not used in this case - we only signal list completion
	finished();
}

void KameraProtocol::setHost(const QString& host, int port, const QString& user, const QString& pass )
{
	kdDebug() << "KameraProtocol::setHost(" << host << ", " << port << ", " << user << ", " << pass << ")" << endl;
	int gpr, idx;

	if (!host.isEmpty()) {
		// Read configuration
		m_config->setGroup(host);
		QString m_cfgModel = m_config->readEntry("Model");
		QString m_cfgPath = m_config->readEntry("Path");

		kdDebug() << "model is " << m_cfgModel << ", port is " << m_cfgPath << endl;

		if (m_camera) {
			kdDebug() << "Configuration change detected" << endl;
			if (GP_OK!=gp_camera_exit(m_camera,m_context)) {
				kdDebug() << "camera exit failed for old cam?\n";
			}
			gp_camera_unref(m_camera);
			m_camera = NULL;
			infoMessage( i18n("Reinitializing camera") );
		} else {
			kdDebug() << "Initializing camera" << endl;
			infoMessage( i18n("Initializing camera") );
		}
		// fetch abilities
		CameraAbilitiesList *abilities_list;
		gp_abilities_list_new(&abilities_list);
		gp_abilities_list_load(abilities_list, m_context);
		idx = gp_abilities_list_lookup_model(abilities_list, tocstr(m_cfgModel));
		if (idx < 0) {
			gp_abilities_list_free(abilities_list);
			kdDebug() << "Unable to get abilities for model: " << m_cfgModel << endl;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
			return;
		}
		gp_abilities_list_get_abilities(abilities_list, idx, &m_abilities);
		gp_abilities_list_free(abilities_list);

		// fetch port
		GPPortInfoList *port_info_list;
		GPPortInfo port_info;
		gp_port_info_list_new(&port_info_list);
		gp_port_info_list_load(port_info_list);
		idx = gp_port_info_list_lookup_path(port_info_list, tocstr(m_cfgPath));
		if (idx < 0) {
			gp_port_info_list_free(port_info_list);
			kdDebug() << "Unable to get port info for path: " << m_cfgPath << endl;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
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
		kdDebug() << "Opening camera model " << m_cfgModel << " at " << m_cfgPath << endl;
#if 0
		// initialize the camera (might take time on a non-existant or disconnected camera)
		gpr = gp_camera_init(m_camera, m_context);
		if(gpr != GP_OK) {
			gp_camera_unref(m_camera);
			m_camera = NULL;
			m_cfgModel = ""; // force a configuration reload (since init didn't complete)
			kdDebug() << "Unable to init camera: " << gp_result_as_string(gpr) << endl;
			error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
			return;
		}
		gp_camera_exit(m_camera, m_context);
#endif
	}
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
	kdDebug() << "KameraProtocol::readCameraFolder(" << folder << ")" << endl;

	int gpr;
	if((gpr = gp_camera_folder_list_folders(m_camera, tocstr(folder), dirList, m_context)) != GP_OK)
		return gpr;
	if((gpr = gp_camera_folder_list_files(m_camera, tocstr(folder), fileList, m_context)) != GP_OK)
		return gpr;
	return GP_OK;
}

static void frontendProgressUpdate(
	GPContext *context, unsigned int id, float current, void *data
) {
	KameraProtocol *object = (KameraProtocol*)data;

	object->processedSize((int)current); // hack: call slot directly 
}

static unsigned int frontendProgressStart(
	GPContext *context, float totalsize, const char *format, va_list args,
	void *data
) {
	KameraProtocol *object = (KameraProtocol*)data;

	int size=vsnprintf(NULL, 0, format, args);
	if(size<=0)
		return GP_OK; // vsnprintf is broken, better don't do anything.

	char *status=new char[size+1];
	vsnprintf(status, size, format, args);
	
	object->infoMessage(QString::fromLocal8Bit(status));
	delete status;

	object->totalSize((int)totalsize); // hack: call slot directly 

	return GP_OK;
}

// this callback function is activated on every status message from gphoto2
static void frontendCameraStatus(GPContext *context, const char *format, va_list args, void *data)
{
	KameraProtocol *object = (KameraProtocol*)data;
	int size=vsnprintf(NULL, 0, format, args);
	if(size<=0)
		return; // vsnprintf is broken, better don't do anything.

	char *status=new char[size+1];
	vsnprintf(status, size, format, args);
	
	object->infoMessage(QString::fromLocal8Bit(status));
	delete status;
}
