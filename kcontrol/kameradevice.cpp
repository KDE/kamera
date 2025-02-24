/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2002-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2002-2003 Marcus Meissner <marcus@jet.franken.de>
    SPDX-FileCopyrightText: 2003 Nadeem Hasan <nhasan@nadmm.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kameradevice.h"
#include "kcm_kamera_log.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <KConfig>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KMessageBox>

extern "C" {
#include <gphoto2.h>
}

#include "kameraconfigdialog.h"

// Define some parts of the old API
#define GP_PROMPT_OK 0
#define GP_PROMPT_CANCEL -1

static const int INDEX_NONE = 0;
static const int INDEX_SERIAL = 1;
static const int INDEX_USB = 2;
static GPContext *glob_context = nullptr;

#ifdef DEBUG
static void gp_errordumper(GPLogLevel level, const char *domain, const char *str, void *data)
{
    qCDebug(KAMERA_KCONTROL) << "GP_LOG: " << str;
}

// Use with
// gp_log_add_func(GP_LOG_LEVEL, gp_errordumper, NULL);
// where LEVEL = { DATA, DEBUG, ERROR, VERBOSE, ALL }
#endif

KCamera::KCamera(const QString &name, const QString &path)
{
    m_name = name;
    m_model = name;
    m_path = path;
    m_camera = nullptr;
    m_abilitylist = nullptr;
}

KCamera::~KCamera()
{
    invalidateCamera();

    if (m_abilitylist)
        gp_abilities_list_free(m_abilitylist);
}

bool KCamera::initInformation()
{
    if (m_model.isNull()) {
        return false;
    }

    if (gp_abilities_list_new(&m_abilitylist) != GP_OK) {
        Q_EMIT error(i18n("Could not allocate memory for the abilities list."));
        return false;
    }
    if (gp_abilities_list_load(m_abilitylist, glob_context) != GP_OK) {
        Q_EMIT error(i18n("Could not load ability list."));
        return false;
    }
    int index = gp_abilities_list_lookup_model(m_abilitylist, m_model.toLocal8Bit().data());
    if (index < 0) {
        Q_EMIT error(
            i18n("Description of abilities for camera %1 is not available."
                 " Configuration options may be incorrect.",
                 m_model));
        return false;
    }
    gp_abilities_list_get_abilities(m_abilitylist, index, &m_abilities);
    return true;
}

bool KCamera::initCamera()
{
    if (m_camera) {
        return true;
    } else {
        int result;

        initInformation();

        if (m_model.isNull() || m_path.isNull()) {
            return false;
        }

        result = gp_camera_new(&m_camera);
        if (result != GP_OK) {
            // m_camera is not initialized, so we cannot get result as string
            Q_EMIT error(i18n("Could not access driver. Check your gPhoto2 installation."));
            return false;
        }

        // set the camera's model
        GPPortInfo info;
        GPPortInfoList *il;
        gp_port_info_list_new(&il);
        gp_port_info_list_load(il);
        gp_port_info_list_get_info(il, gp_port_info_list_lookup_path(il, m_path.toLocal8Bit().data()), &info);
        gp_camera_set_abilities(m_camera, m_abilities);
        gp_camera_set_port_info(m_camera, info);
        gp_port_info_list_free(il);

        // this might take some time (esp. for non-existent camera) - better be done asynchronously
        result = gp_camera_init(m_camera, glob_context);
        if (result != GP_OK) {
            gp_camera_free(m_camera);
            m_camera = nullptr;
            Q_EMIT error(i18n("Unable to initialize camera. Check your port settings and camera connectivity and try again."),
                         QString::fromLocal8Bit(gp_result_as_string(result)));
            return false;
        }

        qCDebug(KAMERA_KCONTROL) << "Initialized camera" << m_name << "on" << m_path;
        return true;
    }
}

QString KCamera::summary()
{
    int result;
    CameraText summary;

    if (!initCamera())
        return QString();

    result = gp_camera_get_summary(m_camera, &summary, glob_context);
    if (result != GP_OK) {
        return i18n("No camera summary information is available.\n");
    }
    return QString::fromLocal8Bit(summary.text);
}

bool KCamera::configure()
{
    CameraWidget *window;
    int result;

    if (!initCamera())
        return false;

    result = gp_camera_get_config(m_camera, &window, glob_context);
    if (result != GP_OK) {
        Q_EMIT error(i18n("Camera configuration failed."), QString::fromLocal8Bit(gp_result_as_string(result)));
        return false;
    }

    KameraConfigDialog kcd(m_camera, window);
    result = kcd.exec() ? GP_PROMPT_OK : GP_PROMPT_CANCEL;

    if (result == GP_PROMPT_OK) {
        result = gp_camera_set_config(m_camera, window, glob_context);
        if (result != GP_OK) {
            Q_EMIT error(i18n("Camera configuration failed."), QString::fromLocal8Bit(gp_result_as_string(result)));
            return false;
        }
    }

    return true;
}

bool KCamera::test()
{
    // TODO: Make testing non-blocking (maybe via KIO?)
    // Currently, a failed serial test times out at about 30 sec.

    if (!initCamera())
        return false;

    return true;
}

void KCamera::load(KConfig *config)
{
    KConfigGroup group = config->group(m_name);
    if (m_model.isNull()) {
        m_model = group.readEntry("Model");
    }
    if (m_path.isNull()) {
        m_path = group.readEntry("Path");
    }
    invalidateCamera();
}

void KCamera::save(KConfig *config)
{
    KConfigGroup group = config->group(m_name);
    group.writeEntry("Model", m_model);
    group.writeEntry("Path", m_path);
}

QString KCamera::portName()
{
    const QString port = m_path.left(m_path.indexOf(QLatin1Char(':'))).toLower();
    if (port == QStringLiteral("serial"))
        return i18n("Serial");
    if (port == QStringLiteral("usb"))
        return i18n("USB");
    return i18n("Unknown port");
}

void KCamera::setName(const QString &name)
{
    m_name = name;
}

void KCamera::setModel(const QString &model)
{
    m_model = model;
    invalidateCamera();
    initInformation();
}

void KCamera::setPath(const QString &path)
{
    m_path = path;
    invalidateCamera();
}

void KCamera::invalidateCamera()
{
    if (m_camera) {
        qCDebug(KAMERA_KCONTROL) << "Finalizing camera" << m_name << "on" << m_path;
        gp_camera_exit(m_camera, glob_context);
        gp_camera_free(m_camera);
        m_camera = nullptr;
    }
}

bool KCamera::isTestable() const
{
    return true;
}

bool KCamera::isConfigurable()
{
    initInformation();
    return m_abilities.operations & GP_OPERATION_CONFIG;
}

QStringList KCamera::supportedPorts()
{
    initInformation();
    QStringList ports;
    if (m_abilities.port & GP_PORT_SERIAL) {
        ports.append(QStringLiteral("serial"));
    }
    if (m_abilities.port & GP_PORT_USB) {
        ports.append(QStringLiteral("usb"));
    }
    return ports;
}

CameraAbilities KCamera::abilities() const
{
    return m_abilities;
}

// ---------- KameraSelectCamera ------------

KameraDeviceSelectDialog::KameraDeviceSelectDialog(QWidget *parent, KCamera *device)
    : QDialog(parent)
{
    setWindowTitle(i18n("Select Camera Device"));

    setModal(true);
    m_device = device;
    connect(m_device, qOverload<const QString &>(&KCamera::error), this, qOverload<const QString &>(&KameraDeviceSelectDialog::slot_error));

    connect(m_device,
            qOverload<const QString &, const QString &>(&KCamera::error),
            this,
            qOverload<const QString &, const QString &>(&KameraDeviceSelectDialog::slot_error));

    // a layout with horizontal boxes - this gives the two columns
    auto topLayout = new QHBoxLayout(this);

    // the models list
    m_modelSel = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_model->setColumnCount(1);
    m_model->setHeaderData(0, Qt::Horizontal, i18nc("@title:column", "Supported Cameras"));
    m_modelSel->setModel(m_model);

    topLayout->addWidget(m_modelSel);
    connect(m_modelSel, &QListView::activated, this, &KameraDeviceSelectDialog::slot_setModel);
    connect(m_modelSel, &QListView::clicked, this, &KameraDeviceSelectDialog::slot_setModel);

    // make sure listview only as wide as it needs to be
    m_modelSel->setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred));

    auto rightLayout = new QVBoxLayout();
    rightLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->addLayout(rightLayout);

    m_portSelectGroup = new QGroupBox(i18n("Port"), this);
    auto vertLayout = new QVBoxLayout;
    m_portSelectGroup->setLayout(vertLayout);
    m_portSelectGroup->setMinimumSize(100, 120);
    rightLayout->addWidget(m_portSelectGroup);
    // Create port type selection radiobuttons.
    m_serialRB = new QRadioButton(i18n("Serial"));
    vertLayout->addWidget(m_serialRB);
    m_serialRB->setWhatsThis(
        i18n("If this option is checked, the camera has "
             "to be connected to one of the computer's serial ports (known as COM "
             "ports in Microsoft Windows.)"));
    m_USBRB = new QRadioButton(i18n("USB"));
    vertLayout->addWidget(m_USBRB);
    m_USBRB->setWhatsThis(
        i18n("If this option is checked, the camera has to "
             "be connected to one of the computer's USB ports, or to a USB hub."));

    m_portSettingsGroup = new QGroupBox(i18n("Port Settings"), this);
    auto lay = new QVBoxLayout;
    m_portSettingsGroup->setLayout(lay);
    rightLayout->addWidget(m_portSettingsGroup);
    // Create port settings widget stack
    m_settingsStack = new QStackedWidget;
    auto grid2 = new QWidget(m_settingsStack);
    auto gridLayout2 = new QGridLayout(grid2);
    grid2->setLayout(gridLayout2);
    auto label2 = new QLabel(i18n("Port"), grid2);
    gridLayout2->addWidget(label2, 0, 0, Qt::AlignLeft);

    lay->addWidget(grid2);
    lay->addWidget(m_settingsStack);
    connect(m_serialRB, &QRadioButton::toggled, this, &KameraDeviceSelectDialog::changeCurrentIndex);
    connect(m_USBRB, &QRadioButton::toggled, this, &KameraDeviceSelectDialog::changeCurrentIndex);

    // none tab
    m_settingsStack->insertWidget(INDEX_NONE, new QLabel(i18n("No port type selected."), m_settingsStack));

    // serial tab
    auto grid = new QWidget(m_settingsStack);
    auto gridLayout = new QGridLayout(grid);
    grid->setLayout(gridLayout);

    auto label = new QLabel(i18n("Port:"), grid);
    m_serialPortCombo = new QComboBox(grid);
    m_serialPortCombo->setEditable(true);
    m_serialPortCombo->setWhatsThis(
        i18n("Specify here the serial port to "
             "which you connect the camera."));

    gridLayout->addWidget(label, 1, 0, Qt::AlignLeft);
    gridLayout->addWidget(m_serialPortCombo, 1, 1, Qt::AlignRight);
    m_settingsStack->insertWidget(INDEX_SERIAL, grid);

    m_settingsStack->insertWidget(INDEX_USB, new QLabel(i18n("No further configuration is required for USB cameras."), m_settingsStack));

    // Add the ok/cancel buttons to the bottom of the right side
    m_OkCancelButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QPushButton *okButton = m_OkCancelButtonBox->button(QDialogButtonBox::Ok);
    QPushButton *cancelButton = m_OkCancelButtonBox->button(QDialogButtonBox::Cancel);
    okButton->setDefault(true);
    // Set false enabled to allow the use of an equivalent
    // to enableButtonOk(true) in slot_setModel.
    okButton->setEnabled(false);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);
    connect(okButton, &QPushButton::clicked, this, &KameraDeviceSelectDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &KameraDeviceSelectDialog::close);
    // add a spacer
    rightLayout->addStretch();

    rightLayout->addWidget(m_OkCancelButtonBox);

    // query gphoto2 for existing serial ports
    GPPortInfoList *list;
    GPPortInfo info;
    int gphoto_ports = 0;
    gp_port_info_list_new(&list);
    if (gp_port_info_list_load(list) >= 0) {
        gphoto_ports = gp_port_info_list_count(list);
    }
    for (int i = 0; i < gphoto_ports; i++) {
        if (gp_port_info_list_get_info(list, i, &info) >= 0) {
            char *xpath;
            gp_port_info_get_path(info, &xpath);
            if (strncmp(xpath, "serial:", 7) == 0) {
                m_serialPortCombo->addItem(QString::fromLocal8Bit(xpath).mid(7));
            }
        }
    }
    gp_port_info_list_free(list);

    populateCameraListView();
    load();

    m_portSelectGroup->setEnabled(false);
    m_portSettingsGroup->setEnabled(false);
}

void KameraDeviceSelectDialog::changeCurrentIndex()
{
    auto send = dynamic_cast<QRadioButton *>(sender());
    if (send) {
        if (send == m_serialRB) {
            m_settingsStack->setCurrentIndex(INDEX_SERIAL);
        } else if (send == m_USBRB) {
            m_settingsStack->setCurrentIndex(INDEX_USB);
        }
    }
}

bool KameraDeviceSelectDialog::populateCameraListView()
{
    gp_abilities_list_new(&m_device->m_abilitylist);
    gp_abilities_list_load(m_device->m_abilitylist, glob_context);
    int numCams = gp_abilities_list_count(m_device->m_abilitylist);
    CameraAbilities a;

    if (numCams < 0) {
        // XXX libgphoto2 failed to get te camera list
        return false;
    } else {
        for (int x = 0; x < numCams; ++x) {
            if (gp_abilities_list_get_abilities(m_device->m_abilitylist, x, &a) == GP_OK) {
                auto cameraItem = new QStandardItem;
                cameraItem->setEditable(false);
                cameraItem->setText(a.model);
                m_model->appendRow(cameraItem);
            }
        }
        return true;
    }
}

void KameraDeviceSelectDialog::save()
{
    m_device->setModel(m_modelSel->currentIndex().data(Qt::DisplayRole).toString());

    if (m_serialRB->isChecked()) {
        m_device->setPath(QStringLiteral("serial:") + m_serialPortCombo->currentText());
    } else if (m_USBRB->isChecked()) {
        m_device->setPath(QStringLiteral("usb:"));
    }
}

void KameraDeviceSelectDialog::load()
{
    QString path = m_device->path();
    QString port = path.left(path.indexOf(QLatin1Char(':'))).toLower();

    if (port == QLatin1String("serial")) {
        setPortType(INDEX_SERIAL);
    } else if (port == QLatin1String("usb")) {
        setPortType(INDEX_USB);
    }

    const QList<QStandardItem *> items = m_model->findItems(m_device->model());
    for (QStandardItem *item : items) {
        const QModelIndex index = m_model->indexFromItem(item);
        m_modelSel->selectionModel()->select(index, QItemSelectionModel::Select);
    }
}

void KameraDeviceSelectDialog::slot_setModel(const QModelIndex &modelIndex)
{
    m_portSelectGroup->setEnabled(true);
    m_portSettingsGroup->setEnabled(true);

    QString model = modelIndex.data(Qt::DisplayRole).toString();

    CameraAbilities abilities;
    int index = gp_abilities_list_lookup_model(m_device->m_abilitylist, model.toLocal8Bit().data());
    if (index < 0) {
        slot_error(
            i18n("Description of abilities for camera %1 is not available."
                 " Configuration options may be incorrect.",
                 model));
    }
    int result = gp_abilities_list_get_abilities(m_device->m_abilitylist, index, &abilities);
    if (result == GP_OK) {
        // enable radiobuttons for supported port types
        m_serialRB->setEnabled(abilities.port & GP_PORT_SERIAL);
        m_USBRB->setEnabled(abilities.port & GP_PORT_USB);
        // if there's only one available port type, make sure it's selected
        if (abilities.port == GP_PORT_SERIAL) {
            setPortType(INDEX_SERIAL);
        }
        if (abilities.port == GP_PORT_USB) {
            setPortType(INDEX_USB);
        }
    } else {
        slot_error(
            i18n("Description of abilities for camera %1 is not available."
                 " Configuration options may be incorrect.",
                 model));
    }
    QPushButton *okButton = m_OkCancelButtonBox->button(QDialogButtonBox::Ok);
    okButton->setEnabled(true);
}

void KameraDeviceSelectDialog::setPortType(int type)
{
    // Enable the correct button
    if (type == INDEX_USB) {
        m_USBRB->setChecked(true);
    } else if (type == INDEX_SERIAL) {
        m_serialRB->setChecked(true);
    }

    // Bring the right tab to the front
    m_settingsStack->setCurrentIndex(type);
}

void KameraDeviceSelectDialog::slot_error(const QString &message)
{
    KMessageBox::error(this, message);
}

void KameraDeviceSelectDialog::slot_error(const QString &message, const QString &details)
{
    KMessageBox::detailedError(this, message, details);
}

#include "moc_kameradevice.cpp"
