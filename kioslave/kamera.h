
#ifndef __kamera_h__
#define __kamera_h__

#include <kio/slavebase.h>

extern "C" {
	#include <gphoto2.h>
}

class KameraProtocol : public KIO::SlaveBase
{
public:
	KameraProtocol(const QCString &pool, const QCString &app);
	virtual ~KameraProtocol();

	virtual void get(const KURL &url);
	virtual void stat(const KURL &url);
	virtual void del(const KURL &url, bool isFile);
	void listDir(const KURL &url);

private:
	Camera *m_camera;
	bool m_previewThumbs;

	void reparseConfiguration(void);
	bool openCamera(void);
	void closeCamera(void);

	void statRoot(void);
	void statRegular(const KURL &url);
        void translateFileToUDS(KIO::UDSEntry &udsEntry, const CameraFileInfo &info);
	void translateDirectoryToUDS(KIO::UDSEntry &udsEntry, const QString &dirname);
	bool cameraSupportsPreview(void);
	bool cameraSupportsDel(void);
	bool cameraSupportsPut(void);
	int readCameraFolder(const QString &folder, CameraList *dirList, CameraList *fileList);
	QString lockFileName();
	void lock();
	void unlock();

	QMap<QString, QString> *m_listDirCache;
	QMap<QString, QString> *m_listFileCache;
	QString m_cfgDriver;
	QString m_cfgPort;
	QString m_cfgPath;

	static QMap<Camera *, KameraProtocol *> m_cameraToProtocol;
	int fileSize;

	// static frontend callbacks
	static int frontendCameraStatus(Camera *camera, char *status);
	static int frontendCameraProgress(Camera *camera, CameraFile *file, float progress);
};

#endif
