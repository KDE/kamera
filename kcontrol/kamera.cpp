/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2002-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2002-2003 Marcus Meissner <marcus@jet.franken.de>
    SPDX-FileCopyrightText: 2003 Nadeem Hasan <nhasan@nadmm.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kamera.h"

#include <QLabel>
#include <QListView>
#include <QVBoxLayout>
#include <QApplication>
#include <QStandardItemModel>
#include <QMenu>
#include <QIcon>

#include <KConfig>
#include <KMessageBox>
#include <KLocalizedString>
#include <KToolBar>
#include <KProtocolInfo>
#include <KActionCollection>
#include <KConfigGroup>
#include "kameradevice.h"
#include "kcm_kamera_log.h"

K_PLUGIN_CLASS_WITH_JSON(KKameraConfig, "kcm_kamera.json")

// --------------- Camera control center module widget ---
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
KKameraConfig::KKameraConfig(QWidget *parent, const QVariantList &)
    : KCModule(parent)
#else
KKameraConfig::KKameraConfig(QObject *parent, const KPluginMetaData &md)
    : KCModule(parent)
#endif
{
#ifdef DEBUG_KAMERA_KCONTROL
    QLoggingCategory::setFilterRules(QStringLiteral("kamera.kcm.debug = true"));
#endif
    m_devicePopup = new QMenu(widget());
	m_actions = new KActionCollection(this);
    m_config = new KConfig(KProtocolInfo::config(QStringLiteral("camera")), KConfig::SimpleConfig);
    m_deviceModel = new QStandardItemModel(this);

	m_context = gp_context_new();
	if (m_context) {
		// Register the callback functions
		gp_context_set_cancel_func(m_context, cbGPCancel, this);
		gp_context_set_idle_func(m_context, cbGPIdle, this);

		displayGPSuccessDialogue();
	} else {
		displayGPFailureDialogue();
	}
}

KKameraConfig::~KKameraConfig()
{
    delete m_config;
}

#if KCOREADDONS_VERSION < QT_VERSION_CHECK(5, 105, 0)
QWidget *KKameraConfig::widget()
{
    return this;
}
#endif

void KKameraConfig::defaults()
{
}

void KKameraConfig::displayGPFailureDialogue()
{
    auto topLayout = new QVBoxLayout(widget());
	topLayout->setSpacing(0);
	topLayout->setContentsMargins(0, 0, 0, 0);
    auto label = new QLabel(i18n("Unable to initialize the gPhoto2 libraries."), widget());
	topLayout->addWidget(label);
}

void KKameraConfig::displayGPSuccessDialogue()
{
	// set the kcontrol module buttons
	setButtons(Help | Apply );

	// create a layout with two vertical boxes
    auto topLayout = new QVBoxLayout(widget());
	topLayout->setSpacing(0);
	topLayout->setContentsMargins(0, 0, 0, 0);

    m_toolbar = new KToolBar(widget(), "ToolBar");
	topLayout->addWidget(m_toolbar);
	m_toolbar->setMovable(false);

    // create list of devices - this is the large white box
    m_deviceSel = new QListView(widget());
	topLayout->addWidget(m_deviceSel);

	m_deviceSel->setModel(m_deviceModel);

	connect(m_deviceSel, &QListView::customContextMenuRequested,
		this, &KKameraConfig::slot_deviceMenu);
	connect(m_deviceSel, &QListView::doubleClicked,
		this, &KKameraConfig::slot_configureCamera);
	connect(m_deviceSel, &QListView::activated,
		this, &KKameraConfig::slot_deviceSelected);
	connect(m_deviceSel, &QListView::clicked,
		this, &KKameraConfig::slot_deviceSelected);

	m_deviceSel->setViewMode(QListView::IconMode);
	m_deviceSel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_deviceSel->setContextMenuPolicy(Qt::CustomContextMenu);

    // create actions, add to the toolbar
	QAction *act;
    act = m_actions->addAction(QStringLiteral("camera_add"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("camera-photo")));
	act->setText(i18n("Add"));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_addCamera);
	act->setWhatsThis(i18n("Click this button to add a new camera."));
	m_toolbar->addAction(act);
	m_toolbar->addSeparator();

    act = m_actions->addAction(QStringLiteral("camera_test"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("dialog-ok")));
	act->setText(i18n("Test"));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_testCamera);
	act->setWhatsThis(i18n("Click this button to test the connection to the selected camera."));
	m_toolbar->addAction(act);

    act = m_actions->addAction(QStringLiteral("camera_remove"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("user-trash")));
	act->setText(i18n("Remove"));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_removeCamera);
	act->setWhatsThis(i18n("Click this button to remove the selected camera from the list."));
	m_toolbar->addAction(act);

    act = m_actions->addAction(QStringLiteral("camera_configure"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
	act->setText(i18n("Configure..."));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_configureCamera);
	act->setWhatsThis(i18n("Click this button to change the configuration of the selected camera.<br><br>The availability of this feature and the contents of the Configuration dialog depend on the camera model."));
	m_toolbar->addAction(act);

    act = m_actions->addAction(QStringLiteral("camera_summary"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("hwinfo")));
	act->setText(i18n("Information"));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_cameraSummary);
	act->setWhatsThis(i18n("Click this button to view a summary of the current status of the selected camera.<br><br>The availability of this feature and the contents of the Information dialog depend on the camera model."));
	m_toolbar->addAction(act);
	m_toolbar->addSeparator();

    act = m_actions->addAction(QStringLiteral("camera_cancel"));
    act->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
	act->setText(i18n("Cancel"));
	connect(act, &QAction::triggered, this, &KKameraConfig::slot_cancelOperation);
	act->setWhatsThis(i18n("Click this button to cancel the current camera operation."));
	act->setEnabled(false);
	m_toolbar->addAction(act);
}

void KKameraConfig::populateDeviceListView()
{
	m_deviceModel->clear();
    CameraDevicesMap::ConstIterator itEnd = m_devices.constEnd();
    for (CameraDevicesMap::ConstIterator it = m_devices.constBegin(); it != itEnd; ++it) {
		if (it.value()) {
			auto deviceItem = new QStandardItem;
			deviceItem->setEditable(false);
			deviceItem->setText(it.key());
            deviceItem->setIcon(QIcon::fromTheme(QStringLiteral("camera-photo")));
			m_deviceModel->appendRow(deviceItem);
		}
	}
	slot_deviceSelected(m_deviceSel->currentIndex());
}

void KKameraConfig::save()
{
	CameraDevicesMap::Iterator it;

	for (it = m_devices.begin(); it != m_devices.end(); it++)
	{
		it.value()->save(m_config);
	}
	m_config->sync();
}

void KKameraConfig::load()
{
	QStringList groupList = m_config->groupList();

	QStringList::Iterator it;
	int i, count;
	CameraList *list;
	CameraAbilitiesList *al;
	GPPortInfoList *il;
	const char *model, *value;
	KCamera *kcamera;

	for (it = groupList.begin(); it != groupList.end(); it++) {
        if (*it != QStringLiteral("<default>"))	{
			KConfigGroup cg(m_config, *it);
            if (cg.readEntry("Path").contains(QStringLiteral("usb:"))) {
				continue;
			}

			// Load configuration for Serial port cameras
			qCDebug(KAMERA_KCONTROL) << "Loading configuration for serial port camera: "
                                     << *it;
			kcamera = new KCamera(*it, cg.readEntry("Path"));
            connect(kcamera, qOverload<const QString&>(&KCamera::error),
                this, qOverload<const QString&>(&KKameraConfig::slot_error));

            connect(kcamera, qOverload<const QString&, const QString&>(&KCamera::error),
                this, qOverload<const QString&, const QString&>(&KKameraConfig::slot_error));

			kcamera->load(m_config);
			m_devices[*it] = kcamera;
		}
	}
	m_cancelPending = false;

	gp_list_new (&list);

	gp_abilities_list_new (&al);
	gp_abilities_list_load (al, m_context);
	gp_port_info_list_new (&il);
	gp_port_info_list_load (il);
	gp_abilities_list_detect (al, il, list, m_context);
	gp_abilities_list_free (al);
	gp_port_info_list_free (il);

	count = gp_list_count (list);

	QMap<QString,QString>	ports, names;

	for (i = 0 ; i<count ; i++) {
		gp_list_get_name  (list, i, &model);
		gp_list_get_value (list, i, &value);

		ports[value] = model;
		if (!strcmp(value,"usb:")) {
			names[model] = value;
		}
	}

    if (ports.contains(QStringLiteral("usb:")) && names[ports[QStringLiteral("usb:")]]!=QStringLiteral("usb:")) {
        ports.remove(QStringLiteral("usb:"));
	}

	QMap<QString,QString>::iterator portit;

	for (portit = ports.begin() ; portit != ports.end(); portit++) {
		qCDebug(KAMERA_KCONTROL) << "Adding USB camera: " << portit.value() << " at " << portit.key();

		kcamera = new KCamera(portit.value(), portit.key());

        connect(kcamera, qOverload<const QString&>(&KCamera::error),
            this, qOverload<const QString&>(&KKameraConfig::slot_error));

        connect(kcamera, qOverload<const QString&, const QString&>(&KCamera::error),
            this, qOverload<const QString&, const QString&>(&KKameraConfig::slot_error));

		m_devices[portit.value()] = kcamera;
	}
	populateDeviceListView();

	gp_list_free (list);
}

void KKameraConfig::beforeCameraOperation()
{
	m_cancelPending = false;

    m_actions->action(QStringLiteral("camera_test"))->setEnabled(false);
    m_actions->action(QStringLiteral("camera_remove"))->setEnabled(false);
    m_actions->action(QStringLiteral("camera_configure"))->setEnabled(false);
    m_actions->action(QStringLiteral("camera_summary"))->setEnabled(false);

    m_actions->action(QStringLiteral("camera_cancel"))->setEnabled(true);
}

void KKameraConfig::afterCameraOperation()
{
    m_actions->action(QStringLiteral("camera_cancel"))->setEnabled(false);

	// if we're regaining control after a Cancel...
	if (m_cancelPending) {
		qApp->restoreOverrideCursor();
		m_cancelPending = false;
	}

	// if any item was selected before the operation was run
	// it makes sense for the relevant toolbar buttons to be enabled
	slot_deviceSelected(m_deviceSel->currentIndex());
}

QString KKameraConfig::suggestName(const QString &name)
{
    QString new_name = name;
    new_name.remove(QLatin1Char('/')); // we cannot have a slash in a URI's host

	if (!m_devices.contains(new_name)) return new_name;

	// try new names with a number appended until we find a free one
	int i = 1;
    while (i++ < 0xffff) {
        new_name = name + QStringLiteral(" (") + QString::number(i) + QLatin1Char(')');
		if (!m_devices.contains(new_name)) return new_name;
	}

	return {};
}

void KKameraConfig::slot_addCamera()
{
        auto m_device = new KCamera(QString(), QString());
    connect(m_device, qOverload<const QString&>(&KCamera::error),
                this, qOverload<const QString&>(&KKameraConfig::slot_error));

        connect(m_device, qOverload<const QString&, const QString&>(&KCamera::error),
                this, qOverload<const QString&,  const QString&>(&KKameraConfig::slot_error));

    KameraDeviceSelectDialog dialog(widget(), m_device);
	if (dialog.exec() == QDialog::Accepted) {
		dialog.save();
		m_device->setName(suggestName(m_device->model()));
		m_devices.insert(m_device->name(), m_device);
		populateDeviceListView();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        Q_EMIT changed(true);
#else
        setNeedsSave(true);
#endif
    } else {
		delete m_device;
	}
}

void KKameraConfig::slot_removeCamera()
{
	const QString name = m_deviceSel->currentIndex().data(Qt::DisplayRole).toString();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices.value(name);
		m_devices.remove(name);
		delete m_device;
		m_config->deleteGroup(name);
		populateDeviceListView();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        Q_EMIT changed(true);
#else
        setNeedsSave(true);
#endif
	}
}

void KKameraConfig::slot_testCamera()
{
	beforeCameraOperation();

	const QString name = m_deviceSel->currentIndex().data(Qt::DisplayRole).toString();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices.value(name);
        if (m_device->test()) {
            KMessageBox::information(widget(), i18n("Camera test was successful."));
        }
	}

	afterCameraOperation();
}

void KKameraConfig::slot_configureCamera()
{
	const QString name = m_deviceSel->currentIndex().data(Qt::DisplayRole).toString();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
		m_device->configure();
	}
}

void KKameraConfig::slot_cameraSummary()
{
	const QString name = m_deviceSel->currentIndex().data(Qt::DisplayRole).toString();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
		QString summary = m_device->summary();
		if (!summary.isNull()) {
            KMessageBox::information(widget(), summary);
		}
	}
}

void KKameraConfig::slot_cancelOperation()
{
	m_cancelPending = true;
	// Prevent the user from keeping clicking Cancel
    m_actions->action(QStringLiteral("camera_cancel"))->setEnabled(false);
	// and indicate that the click on Cancel did have some effect
	qApp->setOverrideCursor(Qt::WaitCursor);
}

void KKameraConfig::slot_deviceMenu(const QPoint &point)
{
	QModelIndex index = m_deviceSel->indexAt(point);
	if (index.isValid()) {
		m_devicePopup->clear();
        m_devicePopup->addAction(m_actions->action(QStringLiteral("camera_test")));
        m_devicePopup->addAction(m_actions->action(QStringLiteral("camera_remove")));
        m_devicePopup->addAction(m_actions->action(QStringLiteral("camera_configure")));
        m_devicePopup->addAction(m_actions->action(QStringLiteral("camera_summary")));
		m_devicePopup->exec(m_deviceSel->viewport()->mapToGlobal(point));
	}
}

void KKameraConfig::slot_deviceSelected(const QModelIndex &index)
{
	bool isValid = index.isValid();
    m_actions->action(QStringLiteral("camera_test"))->setEnabled(isValid);
    m_actions->action(QStringLiteral("camera_remove"))->setEnabled(isValid);
    m_actions->action(QStringLiteral("camera_configure"))->setEnabled(isValid);
    m_actions->action(QStringLiteral("camera_summary"))->setEnabled(isValid);
}

void KKameraConfig::cbGPIdle(GPContext * /*context*/, void * /*data*/)
{
	/*KKameraConfig *self( reinterpret_cast<KKameraConfig*>(data) );*/
	qApp->processEvents();
}

GPContextFeedback KKameraConfig::cbGPCancel(GPContext * /*context*/, void *data)
{
	auto self( reinterpret_cast<KKameraConfig*>(data) );

	// Since in practice no camera driver supports idle callbacks yet,
	// we'll use the cancel callback as opportunity to process events
	qApp->processEvents();

	// If a cancel request is pending, ask gphoto to cancel
    if (self->m_cancelPending) {
		return GP_CONTEXT_FEEDBACK_CANCEL;
    } else {
		return GP_CONTEXT_FEEDBACK_OK;
    }
}

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
QString KKameraConfig::quickHelp() const
{
	return i18n("<h1>Digital Camera</h1>\n"
	  "This module allows you to configure support for your digital camera.\n"
	  "You need to select the camera's model and the port it is connected\n"
	  "to on your computer (e.g. USB, Serial, Firewire). If your camera does not\n"
	  "appear on the list of <i>Supported Cameras</i>, go to the\n"
	  "<a href=\"http://www.gphoto.org\">GPhoto web site</a> for a possible update.<br><br>\n"
	  "To view and download images from the digital camera, go to the address\n"
	  "<a href=\"camera:/\">camera:/</a> in Konqueror and other KDE applications.");
}
#endif

void KKameraConfig::slot_error(const QString &message)
{
    KMessageBox::error(widget(), message);
}

void KKameraConfig::slot_error(const QString &message, const QString &details)
{
    KMessageBox::detailedError(widget(), message, details);
}

#include "kamera.moc"

#include "moc_kamera.cpp"
