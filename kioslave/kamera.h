
#ifndef __kamera_h__
#define __kamera_h__

#include <kio/slavebase.h>

extern "C" {
	#include <gphoto2.h>
}

class KSimpleConfig;

class KameraProtocol : public KIO::SlaveBase
{
public:
	KameraProtocol(const QCString &pool, const QCString &app);
	virtual ~KameraProtocol();

	virtual void get(const KURL &url);
	virtual void stat(const KURL &url);
	virtual void del(const KURL &url, bool isFile);
        virtual void setHost(const QString& host, int port, const QString& user, const QString& pass );
	virtual void listDir(const KURL &url);

private:
	Camera *m_camera;
	KSimpleConfig *m_config;

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

	QString m_cfgModel;
	QString m_cfgPath;

	static QMap<Camera *, KameraProtocol *> m_cameraToProtocol;
	int fileSize;

	// static frontend callbacks
	static int frontendCameraStatus(Camera *camera, char *status);
	static int frontendCameraProgress(Camera *camera, CameraFile *file, float progress);
};

#endif
