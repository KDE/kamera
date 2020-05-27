/*

    Copyright (C) 2001 The Kompany
          2001-2003	Ilya Konstantinov <kde-devel@future.shiny.co.il>
          2001-2008	Marcus Meissner <marcus@jet.franken.de>

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

#include <kio/slavebase.h>
#include <gphoto2.h>
#include <KConfig>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(KAMERA_KIOSLAVE)

class KConfig;

class KameraProtocol : public KIO::SlaveBase
{
public:
    KameraProtocol(const QByteArray &pool, const QByteArray &app);
    ~KameraProtocol() override;

    void get(const QUrl &url) override;
    void stat(const QUrl &url) override;
    void del(const QUrl &url, bool isFile) override;
    void listDir(const QUrl &url) override;
    void special(const QByteArray &data) override;

    CameraFile *getFile() { return m_file; }
    KIO::filesize_t getFileSize() { return m_fileSize; }
    void setFileSize(KIO::filesize_t newfs) { m_fileSize = newfs; }

private:
    Camera *m_camera;
    QString current_camera, current_port;
    CameraAbilities m_abilities;
    KConfig *m_config;

    GPContext *m_context;

    void split_url2camerapath(const QString &url, QString &directory, QString &file);
    void setCamera(const QString &cam, const QString &port);
    void reparseConfiguration(void) override;
    bool openCamera(QString& str);
    bool openCamera(void ) {
        QString errstr;
        return openCamera(errstr);
    }
    void closeCamera(void);

    void statRoot(void);
    void statRegular(const QUrl &url);
    void translateTextToUDS(KIO::UDSEntry &udsEntry,
                            const QString &info,
                            const char *txt);
    void translateFileToUDS(KIO::UDSEntry &udsEntry,
                            const CameraFileInfo &info,
                            const QString &name);
    void translateDirectoryToUDS(KIO::UDSEntry &udsEntry, const QString &dirname);
    bool cameraSupportsPreview(void);
    bool cameraSupportsDel(void);
    bool cameraSupportsPut(void);
    int readCameraFolder(const QString &folder,
                         CameraList *dirList,
                         CameraList *fileList);

    QString m_lockfile;
    int     idletime;

    KIO::filesize_t m_fileSize;
    CameraFile *m_file = nullptr;
    bool actiondone, cameraopen;
};
#endif
