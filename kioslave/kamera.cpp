
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
}

KameraProtocol::~KameraProtocol()
{
}

void KameraProtocol::get(const KURL &url)
{
	kdDebug() << "KameraProtocol::get(" << url.path() << ")" << endl;

	if(openCamera() == false)
		return;

	int (*gp_get)(Camera *, CameraFile *, char *, char *);
	KURL tmpUrl(url);

	if(m_previewThumbs &&
	   cameraSupportsPreview() &&
	   stripCachePath(tmpUrl)) {
		gp_get = gp_camera_file_get_preview;
	} else {
		gp_get = gp_camera_file_get;
	}

	CameraFile *cameraFile = gp_file_new();

	int gpr = gp_get(m_camera, cameraFile,
			tocstr(tmpUrl.directory()),
			tocstr(tmpUrl.filename()));

	switch(gpr) {
	case GP_OK:
		break;
	case GP_ERROR_FILE_NOT_FOUND:
	case GP_ERROR_DIRECTORY_NOT_FOUND:
		error(KIO::ERR_DOES_NOT_EXIST, tmpUrl.filename());
		return;
	default:
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
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

	int ret = gp_camera_file_delete(m_camera, tocstr(url.directory()),
					tocstr(url.filename()));

	if(ret != GP_OK) {
		error(KIO::ERR_CANNOT_DELETE, url.filename());
	} else {
		finished();
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
	// clear port info struct
	memset(&m_portInfo, 0, sizeof(m_portInfo));

	KConfig config("kioslaverc");
	config.setGroup("Kamera Settings");

	QString driver = config.readEntry("Driver", "Directory Browse");
	QString port = config.readEntry("Port", "none");

	strcpy(m_portInfo.name, tocstr(driver));

	if(port == "none") {
		m_portInfo.type = GP_PORT_NONE;
	} else if(port == "serial") {
		m_portInfo.type = GP_PORT_SERIAL;
		QString path = config.readEntry("Path");
		strcpy(m_portInfo.path, tocstr(path));
		m_portInfo.speed = config.readNumEntry("Speed");
	} else if(port == "parallel") {
		m_portInfo.type = GP_PORT_PARALLEL;
		QString path = config.readEntry("Path");
		strcpy(m_portInfo.path, tocstr(path));
	} else if(port == "usb") {
		m_portInfo.type = GP_PORT_USB;
	} else if(port == "ieee1394") {
		m_portInfo.type = GP_PORT_IEEE1394;
	} else if(port == "network") {
		m_portInfo.type = GP_PORT_NETWORK;
		QString netHost= config.readEntry("NetHost");
		strcpy(m_portInfo.host, tocstr(netHost));
		m_portInfo.host_port = config.readNumEntry("NetPort");
	}

	m_previewThumbs = config.readBoolEntry("PreviewThumbs", false);
}

bool KameraProtocol::openCamera(void)
{
	int gpr;

	// load camera settings from kioslaverc
	loadSettings();

	// attempt to initialise libgphoto2 and chosen camera
	// XXX Move library init to constructor/destructor for efficiency ?
	if((gpr = gp_init(GP_DEBUG_LOW)) != GP_OK) {
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return false;
	} else if((gpr = gp_camera_new_by_name(&m_camera,
					       m_portInfo.name)) != GP_OK) {
		gp_exit();
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return false;
	} else if((gpr = gp_camera_init(m_camera, &m_portInfo)) != GP_OK) {
		gp_camera_free(m_camera);
		gp_exit();
		error(KIO::ERR_UNKNOWN, gp_result_as_string(gpr));
		return false;
	}

	return true;
}

void KameraProtocol::closeCamera(void)
{
	gp_camera_free(m_camera);

	gp_exit();
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
	case GP_LIST_FOLDER:
		 atom.m_long = S_IFDIR; break;
	}
	udsEntry.append(atom);
}

bool KameraProtocol::cameraSupportsPreview(void)
{
	CameraAbilities cameraAbilities;

	if(gp_camera_abilities_by_name(m_portInfo.name,
					&cameraAbilities) != GP_OK)
		return false;

	return cameraAbilities.file_preview;
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

	if((gpr = gp_camera_folder_list(m_camera, list,
					tocstr(folder))) != GP_OK)
		return gpr;
	
	CameraList *fl = gp_list_new();

	if((gpr = gp_camera_file_list(m_camera, fl, tocstr(folder))) != GP_OK) {
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
