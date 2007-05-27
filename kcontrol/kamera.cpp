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

#include <kgenericfactory.h>
#include <ksimpleconfig.h>
#include <kaction.h>
#include <kiconloader.h>
#include <kmessagebox.h>
#include <kiconview.h>
#include <kdialog.h>
#include <klocale.h>
#include <ktoolbar.h>
#include <kpopupmenu.h>
#include <kprotocolinfo.h>
#include <kdebug.h>

#include "kameraconfigdialog.h"
#include "kameradevice.h"
#include "kamera.h"
#include "kamera.moc"

typedef KGenericFactory<KKameraConfig, QWidget> KKameraConfigFactory;
K_EXPORT_COMPONENT_FACTORY( kcm_kamera, KKameraConfigFactory( "kcmkamera" ) )

// --------------- Camera control center module widget ---

KKameraConfig *KKameraConfig::m_instance = NULL;

KKameraConfig::KKameraConfig(QWidget *parent, const char *name, const QStringList &)
	: KCModule(KKameraConfigFactory::instance(), parent, name)
{
	m_devicePopup = new KPopupMenu(this);
	m_actions = new KActionCollection(this);
	m_config = new KSimpleConfig(KProtocolInfo::config("camera"));
	
	m_context = gp_context_new();
	if (m_context) {

		// Register the callback functions
		gp_context_set_cancel_func(m_context, cbGPCancel, this);
		gp_context_set_idle_func(m_context, cbGPIdle, this);

		displayGPSuccessDialogue();

		// load existing configuration
		load();

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
	load( true );
}

void KKameraConfig::displayGPFailureDialogue(void)
{
	new QLabel(i18n("Unable to initialize the gPhoto2 libraries."), this);
}

void KKameraConfig::displayGPSuccessDialogue(void)
{
	// set the kcontrol module buttons
	setButtons(Help | Apply | Cancel | Ok);

	// create a layout with two vertical boxes
	QVBoxLayout *topLayout = new QVBoxLayout(this, 0, 0);
	topLayout->setAutoAdd(true);
	
	m_toolbar = new KToolBar(this, "ToolBar");
	m_toolbar->setMovingEnabled(false);
	
	// create list of devices
	m_deviceSel = new KIconView(this);

	connect(m_deviceSel, SIGNAL(rightButtonClicked(QIconViewItem *, const QPoint &)),
		SLOT(slot_deviceMenu(QIconViewItem *, const QPoint &)));
	connect(m_deviceSel, SIGNAL(doubleClicked(QIconViewItem *)),
		SLOT(slot_configureCamera()));
	connect(m_deviceSel, SIGNAL(selectionChanged(QIconViewItem *)),
		SLOT(slot_deviceSelected(QIconViewItem *)));

	m_deviceSel->setSizePolicy(QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding));
	
	// create actions
	KAction *act;
	
	act = new KAction(i18n("Add"), "camera", 0, this, SLOT(slot_addCamera()), m_actions, "camera_add");
	act->setWhatsThis(i18n("Click this button to add a new camera."));
	act->plug(m_toolbar);
	m_toolbar->insertLineSeparator();
	act = new KAction(i18n("Test"), "camera_test", 0, this, SLOT(slot_testCamera()), m_actions, "camera_test");
	act->setWhatsThis(i18n("Click this button to remove the selected camera from the list."));
	act->plug(m_toolbar);
	act = new KAction(i18n("Remove"), "edittrash", 0, this, SLOT(slot_removeCamera()), m_actions, "camera_remove");
	act->setWhatsThis(i18n("Click this button to remove the selected camera from the list."));
	act->plug(m_toolbar);
	act = new KAction(i18n("Configure..."), "configure", 0, this, SLOT(slot_configureCamera()), m_actions, "camera_configure");
	act->setWhatsThis(i18n("Click this button to change the configuration of the selected camera.<br><br>The availability of this feature and the contents of the Configuration dialog depend on the camera model."));
	act->plug(m_toolbar);
	act = new KAction(i18n("Information"), "hwinfo", 0, this, SLOT(slot_cameraSummary()), m_actions, "camera_summary");
	act->setWhatsThis(i18n("Click this button to view a summary of the current status of the selected camera.<br><br>The availability of this feature and the contents of the Configuration dialog depend on the camera model."));
	act->plug(m_toolbar);
	m_toolbar->insertLineSeparator();
	act = new KAction(i18n("Cancel"), "stop", 0, this, SLOT(slot_cancelOperation()), m_actions, "camera_cancel");
	act->setWhatsThis(i18n("Click this button to cancel the current camera operation."));
	act->setEnabled(false);
	act->plug(m_toolbar);
}

void KKameraConfig::populateDeviceListView(void)
{
	m_deviceSel->clear();
	CameraDevicesMap::Iterator it;
	for (it = m_devices.begin(); it != m_devices.end(); it++) {
		if (it.data()) {
			new QIconViewItem(m_deviceSel, it.key(), DesktopIcon("camera"));
		}
	}
	slot_deviceSelected(m_deviceSel->currentItem());
}

void KKameraConfig::save(void)
{
	CameraDevicesMap::Iterator it;

	for (it = m_devices.begin(); it != m_devices.end(); it++)
	{
		it.data()->save(m_config);
	}
	m_config->sync();
}

void KKameraConfig::load(void)
{
	load( false );
}

void KKameraConfig::load(bool useDefaults )
{
	m_config->setReadDefaults( useDefaults );
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
			m_config->setGroup(*it);
			if (m_config->readEntry("Path").contains("usb:"))
				continue;

			kcamera = new KCamera(*it,m_config->readEntry("Path"));
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
		/* kdDebug() << "Adding USB camera: " << portit.data() << " at " << portit.key() << endl; */

		kcamera = new KCamera(portit.data(),portit.key());
		connect(kcamera, SIGNAL(error(const QString &)), SLOT(slot_error(const QString &)));
		connect(kcamera, SIGNAL(error(const QString &, const QString &)), SLOT(slot_error(const QString &, const QString &)));
		m_devices[portit.data()] = kcamera;
	}
	populateDeviceListView();

	gp_list_free (list);

	emit changed( useDefaults );
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
	new_name.replace("/", ""); // we cannot have a slash in a URI's host

	if (!m_devices.contains(new_name)) return new_name;
	
	// try new names with a number appended until we find a free one
	int i = 1;
	while (i++ < 0xffff) {
		new_name = name + " (" + QString::number(i) + ")";
		if (!m_devices.contains(new_name)) return new_name;
	}

	return QString::null;
}

void KKameraConfig::slot_addCamera()
{
	KCamera *m_device = new KCamera(QString::null,QString::null);
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
		m_config->deleteGroup(name, true);
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

void KKameraConfig::slot_deviceMenu(QIconViewItem *item, const QPoint &point)
{
	if (item) {
		m_devicePopup->clear();
		m_actions->action("camera_test")->plug(m_devicePopup);
		m_actions->action("camera_remove")->plug(m_devicePopup);
		m_actions->action("camera_configure")->plug(m_devicePopup);
		m_actions->action("camera_summary")->plug(m_devicePopup);
		m_devicePopup->popup(point);
	}
}

void KKameraConfig::slot_deviceSelected(QIconViewItem *item)
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
	  "You would need to select the camera's model and the port it is connected\n"
	  "to on your computer (e.g. USB, Serial, Firewire). If your camera doesn't\n"
	  "appear in the list of <i>Supported Cameras</i>, go to the\n"
	  "<a href=\"http://www.gphoto.org\">GPhoto web site</a> for a possible update.<br><br>\n"
	  "To view and download images from the digital camera, go to address\n"
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

