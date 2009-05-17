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
#include <qlabel.h>
#include <qlayout.h>
//Added by qt3to4:
#include <QVBoxLayout>
#include <QApplication>
#include <kgenericfactory.h>
#include <kconfig.h>
#include <kaction.h>
#include <kiconloader.h>
#include <kmessagebox.h>
#include <k3iconview.h>
#include <kdialog.h>
#include <klocale.h>
#include <ktoolbar.h>
#include <kmenu.h>
#include <kprotocolinfo.h>
#include <kdebug.h>
#include <kactioncollection.h>

#include "kameraconfigdialog.h"
#include "kameradevice.h"
#include "kamera.h"
#include "kamera.moc"

// XXX HACK HACK HACK
// XXX All tocstr(string) references can be safely replaced with
// XXX string.latin1() as soon as the gphoto2 API uses 'const char *'
// XXX instead of 'char *' in calls that don't modify the string
#define tocstr(x) ((char *)((x).latin1()))

K_PLUGIN_FACTORY(KKameraConfigFactory, registerPlugin<KKameraConfig>();)
K_EXPORT_PLUGIN(KKameraConfigFactory("kcmkamera"))

// --------------- Camera control center module widget ---

KKameraConfig *KKameraConfig::m_instance = NULL;

KKameraConfig::KKameraConfig(QWidget *parent, const QVariantList &)
	: KCModule(KKameraConfigFactory::componentData(), parent/*, name*/)
{
	m_devicePopup = new KMenu(this);
	m_actions = new KActionCollection(this);
	m_config = new KConfig(KProtocolInfo::config("camera"), KConfig::SimpleConfig);

	m_context = gp_context_new();
	if (m_context) {

		// Register the callback functions
		gp_context_set_cancel_func(m_context, cbGPCancel, this);
		gp_context_set_idle_func(m_context, cbGPIdle, this);

		displayGPSuccessDialogue();

	} else {

		displayGPFailureDialogue();
	}

	// store instance for frontend_prompt
	m_instance = this;
}

KKameraConfig::~KKameraConfig()
{
   delete m_config;
}

void KKameraConfig::defaults()
{
}

void KKameraConfig::displayGPFailureDialogue(void)
{
	QVBoxLayout *topLayout = new QVBoxLayout(this);
	topLayout->setSpacing(0);
	topLayout->setMargin(0);
	QLabel *label = new QLabel(i18n("Unable to initialize the gPhoto2 libraries."), this);
	topLayout->addWidget(label);
}

void KKameraConfig::displayGPSuccessDialogue(void)
{
	// set the kcontrol module buttons
	setButtons(Help | Apply );

	// create a layout with two vertical boxes
	QVBoxLayout *topLayout = new QVBoxLayout(this);
	topLayout->setSpacing(0);
	topLayout->setMargin(0);

	m_toolbar = new KToolBar(this, "ToolBar");
	topLayout->addWidget(m_toolbar);
	m_toolbar->setMovable(false);

	// create list of devices
	m_deviceSel = new K3IconView(this);
	topLayout->addWidget(m_deviceSel);

	connect(m_deviceSel, SIGNAL(rightButtonClicked(Q3IconViewItem *, const QPoint &)),
		SLOT(slot_deviceMenu(Q3IconViewItem *, const QPoint &)));
	connect(m_deviceSel, SIGNAL(doubleClicked(Q3IconViewItem *)),
		SLOT(slot_configureCamera()));
	connect(m_deviceSel, SIGNAL(selectionChanged(Q3IconViewItem *)),
		SLOT(slot_deviceSelected(Q3IconViewItem *)));

	m_deviceSel->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));

	// create actions
	QAction *act;

	act = m_actions->addAction("camera_add");
        act->setIcon(KIcon("camera-photo"));
        act->setText(i18n("Add"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_addCamera()));
	act->setWhatsThis(i18n("Click this button to add a new camera."));
	m_toolbar->addAction(act);
	m_toolbar->addSeparator();
	act = m_actions->addAction("camera_test");
        act->setIcon(KIcon("dialog-ok"));
        act->setText(i18n("Test"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_testCamera()));
	act->setWhatsThis(i18n("Click this button to test the connection to the selected camera."));
	m_toolbar->addAction(act);
	act = m_actions->addAction("camera_remove");
        act->setIcon(KIcon("user-trash"));
        act->setText(i18n("Remove"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_removeCamera()));
	act->setWhatsThis(i18n("Click this button to remove the selected camera from the list."));
	m_toolbar->addAction(act);
	act = m_actions->addAction("camera_configure");
        act->setIcon(KIcon("configure"));
        act->setText(i18n("Configure..."));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_configureCamera()));
	act->setWhatsThis(i18n("Click this button to change the configuration of the selected camera.<br><br>The availability of this feature and the contents of the Configuration dialog depend on the camera model."));
	m_toolbar->addAction(act);
	act = m_actions->addAction("camera_summary");
        act->setIcon(KIcon("hwinfo"));
        act->setText(i18n("Information"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_cameraSummary()));
	act->setWhatsThis(i18n("Click this button to view a summary of the current status of the selected camera.<br><br>The availability of this feature and the contents of the Information dialog depend on the camera model."));
	m_toolbar->addAction(act);
	m_toolbar->addSeparator();
	act = m_actions->addAction("camera_cancel");
        act->setIcon(KIcon("process-stop"));
        act->setText(i18n("Cancel"));
	connect(act, SIGNAL(triggered(bool)), this, SLOT(slot_cancelOperation()));
	act->setWhatsThis(i18n("Click this button to cancel the current camera operation."));
	act->setEnabled(false);
	m_toolbar->addAction(act);

	load();
}

void KKameraConfig::populateDeviceListView(void)
{
	m_deviceSel->clear();
	CameraDevicesMap::Iterator it;
	for (it = m_devices.begin(); it != m_devices.end(); it++) {
		if (it.value()) {
			new Q3IconViewItem(m_deviceSel, it.key(), DesktopIcon("camera-photo"));
		}
	}
	slot_deviceSelected(m_deviceSel->currentItem());
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
		if (*it != "<default>")	{
			KConfigGroup cg(m_config, *it);
			if (cg.readEntry("Path").contains("usb:"))
				continue;

			kcamera = new KCamera(*it, cg.readEntry("Path"));
			connect(kcamera, SIGNAL(error(const QString &)), SLOT(slot_error(const QString &)));
			connect(kcamera, SIGNAL(error(const QString &, const QString &)), SLOT(slot_error(const QString &, const QString &)));
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
		if (!strcmp(value,"usb:"))
			names[model] = value;
	}
	if (ports.contains("usb:") && names[ports["usb:"]]!="usb:")
		ports.remove("usb:");

	QMap<QString,QString>::iterator portit;

	for (portit = ports.begin() ; portit != ports.end(); portit++) {
		/* kDebug() << "Adding USB camera: " << portit.data() << " at " << portit.key(); */

		kcamera = new KCamera(portit.value(), portit.key());
		connect(kcamera, SIGNAL(error(const QString &)), SLOT(slot_error(const QString &)));
		connect(kcamera, SIGNAL(error(const QString &, const QString &)), SLOT(slot_error(const QString &, const QString &)));
		m_devices[portit.value()] = kcamera;
	}
	populateDeviceListView();

	gp_list_free (list);
}

void KKameraConfig::beforeCameraOperation(void)
{
	m_cancelPending = false;

	m_actions->action("camera_test")->setEnabled(false);
	m_actions->action("camera_remove")->setEnabled(false);
	m_actions->action("camera_configure")->setEnabled(false);
	m_actions->action("camera_summary")->setEnabled(false);

	m_actions->action("camera_cancel")->setEnabled(true);
}

void KKameraConfig::afterCameraOperation(void)
{
	m_actions->action("camera_cancel")->setEnabled(false);

	// if we're regaining control after a Cancel...
	if (m_cancelPending) {
		qApp->restoreOverrideCursor();
		m_cancelPending = false;
	}

	// if any item was selected before the operation was run
	// it makes sense for the relevant toolbar buttons to be enabled
	slot_deviceSelected(m_deviceSel->currentItem());
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
	KCamera *m_device = new KCamera(QString::null, QString());	//krazy:exclusion=nullstrassign for old broken gcc
	connect(m_device, SIGNAL(error(const QString &)), SLOT(slot_error(const QString &)));
	connect(m_device, SIGNAL(error(const QString &, const QString &)), SLOT(slot_error(const QString &, const QString &)));
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
	QString name = m_deviceSel->currentItem()->text();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
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

	QString name = m_deviceSel->currentItem()->text();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
		if (m_device->test())
			KMessageBox::information(this, i18n("Camera test was successful."));
	}

	afterCameraOperation();
}

void KKameraConfig::slot_configureCamera()
{
	QString name = m_deviceSel->currentItem()->text();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
		m_device->configure();
	}
}

void KKameraConfig::slot_cameraSummary()
{
	QString summary;
	QString name = m_deviceSel->currentItem()->text();
	if (m_devices.contains(name)) {
		KCamera *m_device = m_devices[name];
		summary = m_device->summary();
		if (!summary.isNull()) {
			KMessageBox::information(this, summary);
		}
	}
}

void KKameraConfig::slot_cancelOperation()
{
	m_cancelPending = true;
	// Prevent the user from keeping clicking Cancel
	m_actions->action("camera_cancel")->setEnabled(false);
	// and indicate that the click on Cancel did have some effect
	qApp->setOverrideCursor(Qt::WaitCursor);
}

void KKameraConfig::slot_deviceMenu(Q3IconViewItem *item, const QPoint &point)
{
	if (item) {
		m_devicePopup->clear();
		m_devicePopup->addAction(m_actions->action("camera_test"));
		m_devicePopup->addAction(m_actions->action("camera_remove"));
		m_devicePopup->addAction(m_actions->action("camera_configure"));
		m_devicePopup->addAction(m_actions->action("camera_summary"));
		m_devicePopup->popup(point);
	}
}

void KKameraConfig::slot_deviceSelected(Q3IconViewItem *item)
{
	m_actions->action("camera_test")->setEnabled(item);
	m_actions->action("camera_remove")->setEnabled(item);
	m_actions->action("camera_configure")->setEnabled(item);
	m_actions->action("camera_summary")->setEnabled(item);
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
	if (self->m_cancelPending)
		return GP_CONTEXT_FEEDBACK_CANCEL;
	else
		return GP_CONTEXT_FEEDBACK_OK;
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

