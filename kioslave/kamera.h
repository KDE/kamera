
#ifndef __kamera_h__
#define __kamera_h__

#include <config.h>
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
	CameraAbilities m_abilities;
	KSimpleConfig *m_config;

	GPContext	*m_context;

	void autoDetect(void);

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

	CameraFile *m_file;
	int m_fileSize;

	// gphoto2 frontend callbacks
	static void frontendCameraStatus(GPContext *context, const char *format, va_list args, void *data);
	static unsigned int frontendProgressStart(GPContext *, float, const char *, void *, void *);
	static void frontendProgressUpdate(GPContext *context, unsigned int id, float current, void *data);
};
#endif
