/*

    Copyright (C) 2001 The Kompany
		  2001-2003	Ilya Konstantinov <kde-devel@future.shiny.co.il>
		  2001-2003	Marcus Meissner <marcus@jet.franken.de>

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

#ifndef __kamera_h__
#define __kamera_h__

#include <config.h>
#include <kio/slavebase.h>
#include <gphoto2.h>

class KConfig;

class KameraProtocol : public KIO::SlaveBase
{
public:
	KameraProtocol(const QByteArray &pool, const QByteArray &app);
	virtual ~KameraProtocol();

	virtual void get(const KUrl &url);
	virtual void stat(const KUrl &url);
	virtual void del(const KUrl &url, bool isFile);
        virtual void setHost(const QString& host, quint16 port, const QString& user, const QString& pass );
	virtual void listDir(const KUrl &url);

        CameraFile *getFile() { return m_file; }
        int getFileSize() { return m_fileSize; }
        void setFileSize(int newfs) { m_fileSize = newfs; }

private:
	Camera *m_camera;
	CameraAbilities m_abilities;
	KConfig *m_config;

	GPContext	*m_context;

	void reparseConfiguration(void);
	bool openCamera(void);
	void closeCamera(void);

	void statRoot(void);
	void statRegular(const KUrl &url);
        void translateTextToUDS(KIO::UDSEntry &udsEntry, const QString &info, const char *txt);
        void translateFileToUDS(KIO::UDSEntry &udsEntry, const CameraFileInfo &info, QString name);
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

	int m_fileSize;
	CameraFile *m_file;
};
#endif
