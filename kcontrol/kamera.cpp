/*

    Copyright (C) 2001 The Kompany
		  2002-2003	Ilya Konstantinov <kde-devel@future.shiny.co.il>
		  2002-2003	Marcus Meissner <marcus@jet.franken.de>
		  2003		Nadeem Hasan <nhasan@nadmm.com>

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
#include "kameraconfigdialog.h"
#include "kameradevice.h"

K_PLUGIN_FACTORY(KKameraConfigFactory, registerPlugin<KKameraConfig>();)

Q_LOGGING_CATEGORY(KAMERA_KCONTROL, "kamera.kcontrol")

// --------------- Camera control center module widget ---

KKameraConfig::KKameraConfig(QWidget *parent, const QVariantList &)
    : KCModule(parent)
{
#ifdef DEBUG_KAMERA_KCONTROL
    QLoggingCategory::setFilterRules(QStringLiteral("kamera.kcontrol.debug = true"));
#endif
    m_devicePopup = new QMenu(this);
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

void KKameraConfig::defaults()
{
}

void KKameraConfig::displayGPFailureDialogue()
{
	QVBoxLayout *topLayout = new QVBoxLayout(this);
	topLayout->setSpacing(0);
	topLayout->setContentsMargins(0, 0, 0, 0);
	QLabel *label = new QLabel(i18n("Unable to initialize the gPhoto2 libraries."), this);
	topLayout->addWidget(label);
}

void KKameraConfig::displayGPSuccessDialogue()
{
	// set the kcontrol module buttons
	setButtons(Help | Apply );

	// create a layout with two vertical boxes
	QVBoxLayout *topLayout = new QVBoxLayout(this);
	topLayout->setSpacing(0);
	topLayout->setContentsMargins(0, 0, 0, 0);

	m_toolbar = new KToolBar(this, "ToolBar");
	topLayout->addWidget(m_toolbar);
	m_toolbar->setMovable(false);

    // create list of devices - this is the large white box
	m_deviceSel = new QListView(this);
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
			QStandardItem *deviceItem = new QStandardItem;
			deviceItem->setEditable(false);
			deviceItem->setText(it.key());
            deviceItem->setIcon(QIcon::fromTheme(QStringLiteral("camera-photo")));
			m_deviceModel->appendRow(deviceItem);
		}
	}
	slot_deviceSelected(m_deviceSel->currentIndex());
}

void KKameraConfig::save(void)
{
	CameraDevicesMap::Iterator it;

	for (it = m_devices.begin(); it != m_devices.end(); it++)
	{
		it.value()->save(m_config);
	}
	m_config->sync();
}

void KKameraConfig::load(void)
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
	new_name.remove('/'); // we cannot have a slash in a URI's host

	if (!m_devices.contains(new_name)) return new_name;

	// try new names with a number appended until we find a free one
	int i = 1;
	while (i++ < 0xffff) {
		new_name = name + " (" + QString::number(i) + ')';
		if (!m_devices.contains(new_name)) return new_name;
	}

	return QString();
}

void KKameraConfig::slot_addCamera()
{
        KCamera *m_device = new KCamera(QString(), QString());
        connect(m_device, qOverload<const QString&>(&KCamera::error),
                this, qOverload<const QString&>(&KKameraConfig::slot_error));

        connect(m_device, qOverload<const QString&, const QString&>(&KCamera::error),
                this, qOverload<const QString&,  const QString&>(&KKameraConfig::slot_error));

	KameraDeviceSelectDialog dialog(this, m_device);
	if (dialog.exec() == QDialog::Accepted) {
		dialog.save();
		m_device->setName(suggestName(m_device->model()));
		m_devices.insert(m_device->name(), m_device);
		populateDeviceListView();
		emit changed(true);
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
		emit changed(true);
	}
}

void KKameraConfig::slot_testCamera()
{
	beforeCameraOperation();

	const QString name = m_deviceSel->currentIndex().data(Qt::DisplayRole).toString();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices.value(name);
        if (m_device->test()) {
			KMessageBox::information(this, i18n("Camera test was successful."));
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
			KMessageBox::information(this, summary);
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
	KKameraConfig *self( reinterpret_cast<KKameraConfig*>(data) );

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

void KKameraConfig::slot_error(const QString &message)
{
	KMessageBox::error(this, message);
}

void KKameraConfig::slot_error(const QString &message, const QString &details)
{
	KMessageBox::detailedError(this, message, details);
}

#include "kamera.moc"
