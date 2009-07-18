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
#include <qlayout.h>
#include <q3widgetstack.h>

#include <qcombobox.h>
#include <qlineedit.h>
#include <qradiobutton.h>
#include <Q3Button>

#include <qlabel.h>
#include <q3grid.h>
//Added by qt3to4:
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <Q3VButtonGroup>
#include <klocale.h>
#include <kconfig.h>
#include <k3listview.h>
#include <kmessagebox.h>
#include <kdebug.h>

extern "C" {
	#include <gphoto2.h>
}

#include "kamera.h"
#include "kameraconfigdialog.h"
#include "kameradevice.moc"

// Define some parts of the old API
#define GP_PROMPT_OK 0
#define GP_PROMPT_CANCEL -1

static const int INDEX_NONE= 0;
static const int INDEX_SERIAL = 1;
static const int INDEX_USB= 3;
static GPContext *glob_context = 0;

KCamera::KCamera(const QString &name, const QString &path)
{
	m_name	= name;
	m_model	= name;
	m_path	= path;
	m_camera = NULL;
}

KCamera::~KCamera()
{
	if(m_camera)
		gp_camera_free(m_camera);
	if(m_abilitylist)
		gp_abilities_list_free(m_abilitylist);
}

bool KCamera::initInformation()
{
	if (m_model.isNull())
		return false;

	if(gp_abilities_list_new(&m_abilitylist) != GP_OK) {
		emit error(i18n("Could not allocate memory for the abilities list."));
		return false;
	}
	if(gp_abilities_list_load(m_abilitylist, glob_context) != GP_OK) {
		emit error(i18n("Could not load ability list."));
		return false;
	}
	int index = gp_abilities_list_lookup_model(m_abilitylist, m_model.toLocal8Bit().data());
	if(index < 0) {
		emit error(i18n("Description of abilities for camera %1 is not available."
					" Configuration options may be incorrect.", m_model));
		return false;
	}
        gp_abilities_list_get_abilities(m_abilitylist, index, &m_abilities);
	return true;
}

bool KCamera::initCamera()
{
	if (m_camera)
		return m_camera;
	else {
		int result;

		initInformation();

		if (m_model.isNull() || m_path.isNull())
			return false;

		result = gp_camera_new(&m_camera);
		if (result != GP_OK) {
			// m_camera is not initialized, so we cannot get result as string
			emit error(i18n("Could not access driver. Check your gPhoto2 installation."));
			return false;
		}

		// set the camera's model
		GPPortInfo info;
		GPPortInfoList *il;
		gp_port_info_list_new(&il);
		gp_port_info_list_load(il);
		gp_port_info_list_get_info(il, gp_port_info_list_lookup_path(il, m_path.toLocal8Bit().data()), &info);
		gp_port_info_list_free(il);
		gp_camera_set_abilities(m_camera, m_abilities);
		gp_camera_set_port_info(m_camera, info);

		// this might take some time (esp. for non-existent camera) - better be done asynchronously
		result = gp_camera_init(m_camera, glob_context);
		if (result != GP_OK) {
			gp_camera_free(m_camera);
			m_camera = NULL;
			emit error(
				i18n("Unable to initialize camera. Check your port settings and camera connectivity and try again."),
				QString::fromUtf8(gp_result_as_string(result)));
			return false;
		}

		return m_camera;
	}
}

Camera* KCamera::camera()
{
	initCamera();
	return m_camera;
}

QString KCamera::summary()
{
	int result;
	CameraText	summary;

	initCamera();

	result = gp_camera_get_summary(m_camera, &summary, glob_context);
	if (result != GP_OK)
		return i18n("No camera summary information is available.\n");
	return QString(summary.text);
}

bool KCamera::configure()
{
	CameraWidget *window;
	int result;

	initCamera();

	result = gp_camera_get_config(m_camera, &window, glob_context);
	if (result != GP_OK) {
		emit error(i18n("Camera configuration failed."), QString::fromUtf8(gp_result_as_string(result)));
		return false;
	}

	KameraConfigDialog kcd(m_camera, window);
	result = kcd.exec() ? GP_PROMPT_OK : GP_PROMPT_CANCEL;

	if (result == GP_PROMPT_OK) {
		result = gp_camera_set_config(m_camera, window, glob_context);
		if (result != GP_OK) {
			emit error(i18n("Camera configuration failed."), QString::fromUtf8(gp_result_as_string(result)));
			return false;
		}
	}

	return true;
}

bool KCamera::test()
{
	// TODO: Make testing non-blocking (maybe via KIO?)
	// Currently, a failed serial test times out at about 30 sec.
	return camera() != 0;
}

void KCamera::load(KConfig *config)
{
	KConfigGroup group = config->group(m_name);
	if (m_model.isNull())
		m_model = group.readEntry("Model");
	if (m_path.isNull())
		m_path = group.readEntry("Path");
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
	QString port = m_path.left(m_path.indexOf(":")).toLower();
	if (port == "serial") return i18n("Serial");
	if (port == "usb") return i18n("USB");
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
		gp_camera_free(m_camera);
		m_camera = NULL;
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
	if (m_abilities.port & GP_PORT_SERIAL)
		ports.append("serial");
	if (m_abilities.port & GP_PORT_USB)
		ports.append("usb");
	return ports;
}

CameraAbilities KCamera::abilities()
{
	return m_abilities;
}

// ---------- KameraSelectCamera ------------

KameraDeviceSelectDialog::KameraDeviceSelectDialog(QWidget *parent, KCamera *device)
	: KDialog(parent)
{
    setCaption( i18n("Select Camera Device") );
    setButtons( Ok | Cancel );
    setDefaultButton( Ok );
    setModal( true );
    showButtonSeparator( true );
	m_device = device;
	connect(m_device, SIGNAL(error(const QString &)),
		SLOT(slot_error(const QString &)));
	connect(m_device, SIGNAL(error(const QString &, const QString &)),
		SLOT(slot_error(const QString &, const QString &)));

	QWidget *page = new QWidget( this );
	setMainWidget(page);

	// a layout with vertical boxes
	QHBoxLayout *topLayout = new QHBoxLayout(page);
	topLayout->setSpacing(KDialog::spacingHint());
	topLayout->setMargin(0);

	// the models list
	m_modelSel = new K3ListView(page);
	topLayout->addWidget( m_modelSel );
	m_modelSel->addColumn(i18n("Supported Cameras"));
	m_modelSel->setColumnWidthMode(0, Q3ListView::Maximum);
	connect(m_modelSel, SIGNAL(selectionChanged(Q3ListViewItem *)),
        SLOT(slot_setModel(Q3ListViewItem *)));
	// make sure listview only as wide as it needs to be
	m_modelSel->setSizePolicy(QSizePolicy(QSizePolicy::Maximum,
		QSizePolicy::Preferred));

	QVBoxLayout *rightLayout = new QVBoxLayout();
	rightLayout->setSpacing(KDialog::spacingHint());
	rightLayout->setMargin(0);
	topLayout->addLayout( rightLayout );

	m_portSelectGroup = new Q3VButtonGroup(i18n("Port"), page);
	rightLayout->addWidget(m_portSelectGroup);
	m_portSettingsGroup = new Q3GroupBox(1, Qt::Horizontal,i18n("Port Settings"), page);
	rightLayout->addWidget(m_portSettingsGroup);

	// Create port type selection radiobuttons.
	m_serialRB = new QRadioButton(i18n("Serial"), m_portSelectGroup);
	m_portSelectGroup->insert(m_serialRB, INDEX_SERIAL);
	m_serialRB->setWhatsThis( i18n("If this option is checked, the camera has to be connected to one of the computer's serial ports (known as COM ports in Microsoft Windows.)"));
	m_USBRB = new QRadioButton(i18n("USB"), m_portSelectGroup);
	m_portSelectGroup->insert(m_USBRB, INDEX_USB);
	m_USBRB->setWhatsThis( i18n("If this option is checked, the camera has to be connected to one of the computer's USB ports, or to a USB hub."));
	// Create port settings widget stack
	m_settingsStack = new Q3WidgetStack(m_portSettingsGroup);
	connect(m_portSelectGroup, SIGNAL(clicked(int)),
		m_settingsStack, SLOT(raiseWidget(int)));

	// none tab
	m_settingsStack->addWidget(new QLabel(i18n("No port type selected."),
		m_settingsStack), INDEX_NONE);

	// serial tab
	Q3Grid *grid = new Q3Grid(2, m_settingsStack);
	grid->setSpacing(KDialog::spacingHint());
	new QLabel(i18n("Port:"), grid);
	m_serialPortCombo = new QComboBox(grid);
	m_serialPortCombo->setEditable(true);
	m_serialPortCombo->setWhatsThis( i18n("Specify here the serial port to which you connect the camera."));
	m_settingsStack->addWidget(grid, INDEX_SERIAL);

	grid = new Q3Grid(2, m_settingsStack);
	grid->setSpacing(KDialog::spacingHint());
	new QLabel(i18n("Port"), grid);

	m_settingsStack->addWidget(new
		QLabel(i18n("No further configuration is required for USB cameras."),
		m_settingsStack), INDEX_USB);

	// query gphoto2 for existing serial ports
	GPPortInfoList *list;
	GPPortInfo info;
	int gphoto_ports=0;
	gp_port_info_list_new(&list);
	if(gp_port_info_list_load(list) >= 0) {
		gphoto_ports = gp_port_info_list_count(list);
	}
	for (int i = 0; i < gphoto_ports; i++) {
		if (gp_port_info_list_get_info(list, i, &info) >= 0) {
			if (strncmp(info.path, "serial:", 7) == 0)
				m_serialPortCombo->addItem(QString::fromLatin1(info.path).mid(7));
		}
	}
	gp_port_info_list_free(list);

	// add a spacer
	rightLayout->addStretch();

	populateCameraListView();
	load();

	enableButtonOk(false );
    m_portSelectGroup->setEnabled( false );
    m_portSettingsGroup->setEnabled( false );
}

bool KameraDeviceSelectDialog::populateCameraListView()
{
	gp_abilities_list_new (&m_device->m_abilitylist);
	gp_abilities_list_load(m_device->m_abilitylist, glob_context);
	int numCams = gp_abilities_list_count(m_device->m_abilitylist);
	CameraAbilities a;

	if(numCams < 0) {
		// XXX libgphoto2 failed to get te camera list
		return false;
	} else {
		for(int x = 0; x < numCams; ++x) {
			if(gp_abilities_list_get_abilities(m_device->m_abilitylist, x, &a) == GP_OK) {
				new Q3ListViewItem(m_modelSel, a.model);
			}
		}
		return true;
	}
}

void KameraDeviceSelectDialog::save()
{
	m_device->setModel(m_modelSel->currentItem()->text(0));

	if (m_portSelectGroup->selected()) {
		QString type = m_portSelectGroup->selected()->text();

		if(type == i18n("Serial"))
			m_device->setPath("serial:" + m_serialPortCombo->currentText());
		else if(type == i18n("USB"))
 			m_device->setPath("usb:");
 	} else {
 		// This camera has no port type (e.g. "Directory Browse" camera).
 		// Do nothing.
 	}
}

void KameraDeviceSelectDialog::load()
{
	QString path = m_device->path();
	QString port = path.left(path.indexOf(":")).toLower();

	if (port == "serial") setPortType(INDEX_SERIAL);
	if (port == "usb") setPortType(INDEX_USB);

	Q3ListViewItem *modelItem = m_modelSel->firstChild();
	if( modelItem )
		do {
			if (modelItem->text(0) == m_device->model()) {
				m_modelSel->setSelected(modelItem, true);
				m_modelSel->ensureItemVisible(modelItem);
			}
		} while ( ( modelItem = modelItem->nextSibling() ) );
}

void KameraDeviceSelectDialog::slot_setModel(Q3ListViewItem *item)
{
    enableButtonOk(true);
    m_portSelectGroup->setEnabled(true);
    m_portSettingsGroup->setEnabled(true);

    QString model = item->text(0);

	CameraAbilities abilities;
	int index = gp_abilities_list_lookup_model(m_device->m_abilitylist, model.toLocal8Bit().data());
	if(index < 0) {
		slot_error(i18n("Description of abilities for camera %1 is not available."
				" Configuration options may be incorrect.", model));
	}
	int result = gp_abilities_list_get_abilities(m_device->m_abilitylist, index, &abilities);
	if (result == GP_OK) {
		// enable radiobuttons for supported port types
		m_serialRB->setEnabled(abilities.port & GP_PORT_SERIAL);
		m_USBRB->setEnabled(abilities.port & GP_PORT_USB);

		// turn off any selected port
		QAbstractButton *selected = m_portSelectGroup->selected();
		if(selected != NULL)
			selected->toggle();

	        // if there's only one available port type, make sure it's selected
		if (abilities.port == GP_PORT_SERIAL)
			setPortType(INDEX_SERIAL);
		if (abilities.port == GP_PORT_USB)
			setPortType(INDEX_USB);
	} else {
		slot_error(i18n("Description of abilities for camera %1 is not available."
			     " Configuration options may be incorrect.", model));
	}
}

void KameraDeviceSelectDialog::setPortType(int type)
{
	// Enable the correct button
	m_portSelectGroup->setButton(type);

	// Bring the right tab to the front
	m_settingsStack->raiseWidget(type);
}

void KameraDeviceSelectDialog::slot_error(const QString &message)
{
	KMessageBox::error(this, message);
}

void KameraDeviceSelectDialog::slot_error(const QString &message, const QString &details)
{
	KMessageBox::detailedError(this, message, details);
}
