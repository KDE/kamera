/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2002-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2002-2003 Marcus Meissner <marcus@jet.franken.de>
    SPDX-FileCopyrightText: 2003 Nadeem Hasan <nhasan@nadmm.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __kamera_h__
#define __kamera_h__

#include "kcoreaddons_version.h"
#include <KCModule>
#include <KPluginFactory>
#include <gphoto2.h>

class QWidget;
class QPushButton;
class QListView;
class QStandardItemModel;
class QModelIndex;
class QMenu;

class KCamera;
class KameraDeviceSelectDialog;
class KConfig;
class KActionCollection;
class KToolBar;

class KKameraConfig : public KCModule
{
    Q_OBJECT
    friend class KameraDeviceSelectDialog;

public:
    explicit KKameraConfig(QObject *parent, const KPluginMetaData &md);
    ~KKameraConfig() override;

    // KCModule interface methods
    void load() override;
    void save() override;
    void defaults() override;
    int buttons();

protected:
    QString suggestName(const QString &name);

protected Q_SLOTS:
    void slot_deviceMenu(const QPoint &point);
    void slot_deviceSelected(const QModelIndex &index);
    void slot_addCamera();
    void slot_removeCamera();
    void slot_configureCamera();
    void slot_cameraSummary();
    void slot_testCamera();
    void slot_cancelOperation();
    void slot_error(const QString &message);
    void slot_error(const QString &message, const QString &details);

private:
    void displayGPFailureDialogue();
    void displayGPSuccessDialogue();
    void populateDeviceListView();
    void beforeCameraOperation();
    void afterCameraOperation();
    // gphoto callbacks
    static void cbGPIdle(GPContext *context, void *data);
    static GPContextFeedback cbGPCancel(GPContext *context, void *data);

private:
    using CameraDevicesMap = QMap<QString, KCamera *>;

    KConfig *m_config;
    CameraDevicesMap m_devices;
    bool m_cancelPending;

    // gphoto members
    GPContext *m_context;

    // widgets for the cameras listview
    QListView *m_deviceSel;
    QStandardItemModel *m_deviceModel;
    KActionCollection *m_actions;
    QPushButton *m_addCamera, *m_removeCamera, *m_testCamera, *m_configureCamera;
    KToolBar *m_toolbar;
    QMenu *m_devicePopup;

    // true if libgphoto2 was initialised successfully in
    // the constructor
    bool m_gpInitialised;
};

#endif
