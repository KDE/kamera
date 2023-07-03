/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2001-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2001-2008 Marcus Meissner <marcus@jet.franken.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __kamera_h__
#define __kamera_h__

#include <KIO/WorkerBase>
#include <gphoto2.h>
class KConfig;

class KConfig;

class KameraProtocol : public KIO::WorkerBase
{
public:
    KameraProtocol(const QByteArray &pool, const QByteArray &app);
    ~KameraProtocol() override;

    KIO::WorkerResult get(const QUrl &url) override;
    KIO::WorkerResult stat(const QUrl &url) override;
    KIO::WorkerResult del(const QUrl &url, bool isFile) override;
    KIO::WorkerResult listDir(const QUrl &url) override;
    KIO::WorkerResult special(const QByteArray &data) override;

    CameraFile *getFile()
    {
        return m_file;
    }
    KIO::filesize_t getFileSize()
    {
        return m_fileSize;
    }
    void setFileSize(KIO::filesize_t newfs)
    {
        m_fileSize = newfs;
    }

private:
    Camera *m_camera;
    QString current_camera, current_port;
    CameraAbilities m_abilities;
    KConfig *m_config;

    GPContext *m_context;

    [[nodiscard]] KIO::WorkerResult split_url2camerapath(const QString &url, QString &directory, QString &file);
    [[nodiscard]] KIO::WorkerResult setCamera(const QString &cam, const QString &port);
    void reparseConfiguration() override;
    bool openCamera(QString &str);
    bool openCamera()
    {
        QString errstr;
        return openCamera(errstr);
    }
    void closeCamera();

    [[nodiscard]] KIO::WorkerResult statRoot();
    [[nodiscard]] KIO::WorkerResult statRegular(const QUrl &url);
    void translateTextToUDS(KIO::UDSEntry &udsEntry, const QString &info, const char *txt);
    void translateFileToUDS(KIO::UDSEntry &udsEntry, const CameraFileInfo &info, const QString &name);
    void translateDirectoryToUDS(KIO::UDSEntry &udsEntry, const QString &dirname);
    bool cameraSupportsPreview();
    bool cameraSupportsDel();
    bool cameraSupportsPut();
    int readCameraFolder(const QString &folder, CameraList *dirList, CameraList *fileList);

    QString m_lockfile;
    int idletime;

    KIO::filesize_t m_fileSize;
    CameraFile *m_file = nullptr;
    bool actiondone, cameraopen;
};
#endif
