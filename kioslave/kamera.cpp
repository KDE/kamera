
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include <kdebug.h>
#include <kinstance.h>
#include <kconfig.h>

#include "kamera.h"

// XXX HACK HACK HACK
// XXX All tocstr(string) references can be safely replaced with
// XXX string.local8Bit() as soon as the gphoto2 API uses 'const char *'
// XXX instead of 'char *' in calls that don't modify the string
#define tocstr(x) ((char *)((x).local8Bit().operator const char *()))

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

KameraProtocol::KameraProtocol(const QCString &pool, const QCString &app)
: SlaveBase("gphoto", pool, app),
m_camera(NULL)
{
	int gpr;
	if((gpr = gp_init(GP_DEBUG_LOW)) != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
	}

	if((gpr = gp_camera_new(&m_camera)) != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
	}
}

KameraProtocol::~KameraProtocol()
{
	if(m_camera)
		gp_camera_free(m_camera);	
	
	gp_exit();
}

void KameraProtocol::get(const KURL &url)
{
	kdDebug() << "KameraProtocol::get(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	int (*gp_get)(Camera *, const  char *, const char *, CameraFile *);
	KURL tmpUrl(url);

	if(m_previewThumbs &&
	   cameraSupportsPreview() &&
	   stripCachePath(tmpUrl)) {
		gp_get = gp_camera_file_get_preview;
	} else {
		gp_get = gp_camera_file_get_file;
	}

	CameraFile *cameraFile = gp_file_new();

//	We must list filename in the folder before get it!!

	CameraList *list = gp_list_new();
        int ret;
 
        ret = readCameraFolder(list, tocstr(tmpUrl.directory()));
	gp_list_free(list);
	if(ret != GP_OK){
                error(KIO::ERR_COULD_NOT_READ, gp_result_as_string(ret));
		closeCamera();
                return;
        }

	int gpr = gp_get(m_camera, 
			tocstr(tmpUrl.directory()),
			tocstr(tmpUrl.filename()),cameraFile);

	switch(gpr) {
	case GP_OK:
		break;
	case GP_ERROR_FILE_NOT_FOUND:
	case GP_ERROR_DIRECTORY_NOT_FOUND:
		error(KIO::ERR_DOES_NOT_EXIST, tmpUrl.filename());
		closeCamera();
		return ;
	default:
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		closeCamera();
		return;
	}

	totalSize(cameraFile->size);
	mimeType(cameraFile->type);

	QByteArray fileData;

	// XXX using assign() here causes segfault, prolly because
	// gp_file_free is called before fileData goes out of scope
	fileData.duplicate(cameraFile->data, cameraFile->size);
	data(fileData);

	gp_file_free(cameraFile);

	processedSize(cameraFile->size);

	finished();

	closeCamera();
}

void KameraProtocol::stat(const KURL &url)
{
	kdDebug() << "KameraProtocol::stat(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	if(url.path() == "/") {
		statRoot();
	} else {
		statRegular(url);
	}

	closeCamera();
}

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

void KameraProtocol::statRegular(const KURL &url)
{
	CameraListEntry cle;
	UDSEntry entry;
	KURL tmpUrl(url);

	if(m_previewThumbs && cameraSupportsPreview())
		stripCachePath(tmpUrl);

	if(findCameraListEntry(tmpUrl, cle)) {
		translateCLEToUDS(entry, cle);
		statEntry(entry);
		finished();
	} else {
		error(KIO::ERR_DOES_NOT_EXIST, url.path());
	}
}

bool KameraProtocol::stripCachePath(KURL &url)
{
	kdDebug() << "KameraProtocol::stripCachePath(" << url.path() <<
		")" << endl;

	const char *paths[] = { ".pics/small/",
				".pics/med/",
				".pics/large/",
				NULL };

	for(int p = 0; paths[p] != NULL; ++p) {
		if(url.path().contains(paths[p]) == 1) {
			int i = url.path().find(paths[p]);
			QString newPath = url.path();
			newPath.replace(i, strlen(paths[p]), "");

			url.setPath(newPath);

			return true;
		}
	}

	return false;
}

void KameraProtocol::del(const KURL &url, bool isFile)
{
	kdDebug() << "KameraProtocol::del(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	if(cameraSupportsDel() && isFile){
		CameraList *list = gp_list_new();
        	KURL tmpUrl(url);
		int ret;
 
        	ret = readCameraFolder(list, tocstr(tmpUrl.directory()));
        	gp_list_free(list);
        	if(ret != GP_OK){
                	error(KIO::ERR_COULD_NOT_READ, gp_result_as_string(ret));
                	closeCamera();
                	return;
        	}

		ret = gp_camera_file_delete(m_camera, tocstr(url.directory()),
					tocstr(url.filename()));

		if(ret != GP_OK) {
			error(KIO::ERR_CANNOT_DELETE, url.filename());
		} else {
			finished();
		}
	}
	closeCamera();
}

void KameraProtocol::listDir(const KURL &url)
{
	kdDebug() << "KameraProtocol::listDir(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	CameraList *list = gp_list_new();
	int ret;

	if((ret = readCameraFolder(list, url.path())) != GP_OK) {
		gp_list_free(list);
		error(KIO::ERR_COULD_NOT_READ, gp_result_as_string(ret));
		return;
	}

	totalSize(gp_list_count(list));

	UDSEntry entry;

	for(int i = 0; i < gp_list_count(list); ++i) {
		CameraListEntry *cameraListEntry = gp_list_entry(list, i);

		translateCLEToUDS(entry, *cameraListEntry);
		listEntry(entry, false);
	}

	listEntry(entry, true);

	gp_list_free(list);

	finished();

	closeCamera();
}

void KameraProtocol::loadSettings(void)
{

	KConfig config("kioslaverc");
	config.setGroup("Kamera Settings");

	QString driver = config.readEntry("Driver", "Directory Browse");
	QString port = config.readEntry("Port", "none");

	strcpy(m_camera->model, tocstr(driver));

	if(port == "none") {
		m_camera->port->type=GP_PORT_NONE;
	} else if(port == "serial") {
		m_camera->port->type=GP_PORT_SERIAL;
		QString path = config.readEntry("Path");
		strcpy(m_camera->port->path, tocstr(path));
		m_camera->port->speed = config.readNumEntry("Speed");
	} else if(port == "parallel") {
                m_camera->port->type=GP_PORT_PARALLEL;
		QString path = config.readEntry("Path");
                strcpy(m_camera->port->path, tocstr(path));
	} else if(port == "usb") {
                m_camera->port->type=GP_PORT_USB;
// XXX HACK!!                
		strcpy(m_camera->port->name,"Universal Serial Bus");
		QString path = config.readEntry("Path");
                strcpy(m_camera->port->path,tocstr(path));
	} else if(port == "ieee1394") {
                m_camera->port->type=GP_PORT_IEEE1394;
	} else if(port == "network") {
                m_camera->port->type=GP_PORT_NETWORK;
	}

	m_previewThumbs = config.readBoolEntry("PreviewThumbs", false);
}

bool KameraProtocol::openCamera(void)
{
	int gpr;

	// load camera settings from kioslaverc

	// attempt to initialise libgphoto2 and chosen camera
	// XXX Move library init to constructor/destructor for efficiency ?
	loadSettings();
	if((gpr = gp_camera_init(m_camera)) != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return false;
	}
	if(gp_camera_abilities_by_name(m_camera->model,
                                        m_camera->abilities) != GP_OK){
		gp_camera_exit(m_camera);
                return false;
	}
	
	return true;
}

void KameraProtocol::closeCamera(void)
{
	gp_camera_exit(m_camera);
}

void KameraProtocol::translateCLEToUDS(UDSEntry &udsEntry,
					const CameraListEntry &cleEntry)
{
	UDSAtom atom;

	udsEntry.clear();

	atom.m_uds = UDS_NAME;
	atom.m_str = cleEntry.name;
	udsEntry.append(atom);

	atom.m_uds = UDS_ACCESS;
	atom.m_long = S_IRUSR | S_IRGRP | S_IROTH |
			S_IWUSR | S_IWGRP | S_IWOTH;
	udsEntry.append(atom);

	atom.m_uds = UDS_FILE_TYPE;
	switch(cleEntry.type) {
	case GP_LIST_FILE:
		 atom.m_long = S_IFREG; break;
//XXX I don't how to handle GP_LIST_CAMERA so...
	case GP_LIST_CAMERA:
	case GP_LIST_FOLDER:
		 atom.m_long = S_IFDIR; break;
	}
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

bool KameraProtocol::findCameraListEntry(const KURL &url,
					 CameraListEntry &cle)
{
	CameraList *list = gp_list_new();

	if(readCameraFolder(list, url.directory()) != GP_OK) {
		gp_list_free(list);
		return false;
	}

	for(int i = 0; i < gp_list_count(list); ++i) {
		CameraListEntry *tmp= gp_list_entry(list, i);

		if(url.fileName() == tmp->name) {
			memcpy(&cle, tmp, sizeof(CameraListEntry));
			gp_list_free(list);
			return true;
		}
	}

	gp_list_free(list);

	return false;
}

int KameraProtocol::readCameraFolder(CameraList *list, const QString &folder)
{
	int gpr;

	if((gpr = gp_camera_folder_list_folders(m_camera, tocstr(folder),
					list)) != GP_OK)
		return gpr;
	
	CameraList *fl = gp_list_new();

	if((gpr = gp_camera_folder_list_files(m_camera, tocstr(folder),
					fl)) != GP_OK) {
		gp_list_free(fl);
		return gpr;
	}

	for(int i = 0; i < gp_list_count(fl); ++i) {
		CameraListEntry *e = gp_list_entry(fl, i);
		gp_list_append(list, e->name, e->type);
	}

	gp_list_free(fl);

	return GP_OK;
}
