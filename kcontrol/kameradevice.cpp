#include <qobject.h>
#include <qgrid.h>
#include <qlayout.h>
#include <qwidgetstack.h>
#include <qvbuttongroup.h>
#include <qvgroupbox.h>
#include <qcombobox.h>
#include <qlineedit.h>
#include <qradiobutton.h>
#include <qwhatsthis.h>
#include <qlabel.h>

#include <klocale.h>
#include <kconfig.h>
#include <klistview.h>
#include <kmessagebox.h>
#include <kdebug.h>

#include <gphoto2.h>
#include "kamera.h"
#include "kameraconfigdialog.h"
#include "kameradevice.moc"

static const int INDEX_NONE= 0;
static const int INDEX_SERIAL = 1;
static const int INDEX_PARALLEL = 2;
static const int INDEX_USB= 3;
static const int INDEX_IEEE1394 = 4;
static const int INDEX_NETWORK = 5;

KCamera::KCamera(const QString &name)
{
	m_name = name;
	m_camera = NULL;
}

KCamera::~KCamera()
{
	if(m_camera)
		gp_camera_free(m_camera);
}

bool KCamera::initInformation()
{
}

bool KCamera::initCamera()
{
	if (m_camera)
		return m_camera;
	else {
		int result;

		if (!m_model) return false;

		// get the model's abilities
		CameraAbilitiesList *abilities_list;
		gp_abilities_list_new(&abilities_list);
		gp_abilities_list_load(abilities_list);
		int idx = gp_abilities_list_lookup_model(abilities_list, m_model.local8Bit().data());

		// if we cannot get abilities, we cannot go forward anymore
		if (idx < 0) {
			gp_abilities_list_free(abilities_list);
			emit error(i18n("No drivers found for camera model <b>%1</b>.").arg(m_model));
			return false;
		}
		gp_abilities_list_get_abilities(abilities_list, idx, &m_abilities);
		gp_abilities_list_free(abilities_list);
		
		// get the port's info
		GPPortInfoList *port_list;
		GPPortInfo port;
		gp_port_info_list_new(&port_list);
		gp_port_info_list_load(port_list);
		idx = gp_port_info_list_lookup_path(port_list, m_path.local8Bit().data());
		if (idx < 0) {
			gp_port_info_list_free(port_list);
			emit error(i18n("No drivers found for port <b>%1</b>.").arg(m_path));
			return false;
		}
		gp_port_info_list_get_info(port_list, idx, &port);
		gp_port_info_list_free(port_list);
		
		// initialize the camera object
		gp_camera_new(&m_camera);
		gp_camera_set_abilities(m_camera, m_abilities);
		gp_camera_set_port_info(m_camera, port);
		// TODO: gp_camera_set_port_speed(camera, int speed)

		// This might take some time (esp. for non-existant camera).
		// TODO: Should be done asynchronously.
		result = gp_camera_init(m_camera);
		if (result != GP_OK) {
			gp_camera_free(m_camera);
			m_camera = NULL;
			emit error(
				i18n("Unable to initialize camera. Check your port settings and camera connection and try again."),
				gp_result_as_string(result));
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

bool KCamera::configure()
{
	CameraWidget *window;
	int result;
	
	initCamera();

	result = gp_camera_get_config(m_camera, &window);
	if (result != GP_OK) {
		emit error(i18n("Camera configuration failed."), QString::fromLocal8Bit(gp_camera_get_error(m_camera)));
		return false; 
	}

	KameraConfigDialog kcd(m_camera, window);
	result = kcd.exec();

	if (result) {
		result = gp_camera_set_config(m_camera, window);
		if (result != GP_OK) {
			emit error(i18n("Camera configuration failed."), gp_camera_get_error(m_camera));
			return false;
		}
	}
	
	return true;
}

bool KCamera::test()
{
	// TODO: Make testing non-blocking (maybe via KIO?)
	// Currently, a failed serial test times out at about 30 sec.
	if (camera()) {
		return true;
	}
}

void KCamera::load(KConfig *config)
{
	config->setGroup(m_name);
	m_model = config->readEntry("Model");
	m_path = config->readEntry("Path");
	invalidateCamera();
}

void KCamera::save(KConfig *config)
{
	config->setGroup(m_name);
	config->writeEntry("Model", m_model);
	config->writeEntry("Path", m_path);
}

QString KCamera::portName()
{
	QString port = m_path.left(m_path.find(":")).lower();
	if (port == "serial") return i18n("Serial");
	if (port == "parallel") return i18n("Parallel");
	if (port == "usb") return i18n("USB");
	if (port == "ieee1394") return i18n("IEEE1394");
	if (port == "network") return i18n("Network");
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

bool KCamera::isTestable()
{
	return true;
}

bool KCamera::isConfigurable()
{
	initInformation();
	return m_abilities.operations & GP_OPERATION_CONFIG;
}

CameraAbilities KCamera::abilities()
{
	return m_abilities;
}

// ---------- KameraSelectCamera ------------

KameraDeviceSelectDialog::KameraDeviceSelectDialog(QWidget *parent, KCamera *device)
	: KDialogBase(parent, "kkameradeviceselect", true, i18n("Select camera device"), Ok | Cancel)
{
	m_device = device;
	connect(m_device, SIGNAL(error(const QString &)), SLOT(slot_error(const QString &)));
	connect(m_device, SIGNAL(error(const QString &, const QString &)), SLOT(slot_error(const QString &, const QString &)));

	QWidget *page = new QWidget( this ); 
	setMainWidget(page);

	// a layout with vertical boxes
	QHBoxLayout *topLayout = new QHBoxLayout(page, KDialog::marginHint(), KDialog::spacingHint());
	topLayout->setAutoAdd(true);

	// the models list
	m_modelSel = new KListView(page);
	m_modelSel->addColumn(i18n("Supported Cameras"));
	m_modelSel->setColumnWidthMode(0, QListView::Maximum);
	connect(m_modelSel, SIGNAL(selectionChanged(QListViewItem *)), SLOT(slot_setModel(QListViewItem *)));
	// make sure listview only as wide as it needs to be
	m_modelSel->setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred));

	QWidget *right = new QWidget(page);
	QVBoxLayout *rightLayout = new QVBoxLayout(right);
	rightLayout->setSpacing(10);

	/*
	QGrid *grid = new QGrid(2, right);
	new QLabel(i18n("Name:"), grid);
	m_nameEdit = new QLineEdit(m_device->name(), grid);
	rightLayout->addWidget(grid);
	*/

	m_portSelectGroup = new QVButtonGroup(i18n("Port"), right);
	rightLayout->addWidget(m_portSelectGroup);
	QVGroupBox *portSettingsGroup = new QVGroupBox(i18n("Port Settings"), right);
	rightLayout->addWidget(portSettingsGroup);

	QGrid *grid = new QGrid(2, right);
	rightLayout->addWidget(grid);
	grid->setSpacing(5);

	// Create port type selection radiobuttons.
	m_serialRB = new QRadioButton(i18n("Serial"), m_portSelectGroup);
	m_portSelectGroup->insert(m_serialRB, INDEX_SERIAL);
	QWhatsThis::add(m_serialRB, i18n("If this option is checked, the camera would have to be connected one of the serial ports (known as COM in Microsoft Windows) in your computer."));
	m_serialRB->setEnabled(false);

	m_USBRB = new QRadioButton(i18n("USB"), m_portSelectGroup);
	m_portSelectGroup->insert(m_USBRB, INDEX_USB);
	QWhatsThis::add(m_USBRB, i18n("If this option is checked, the camera would have to be connected to one of the USB slots in your computer or USB hub."));
	m_USBRB->setEnabled(false);

	m_parallelRB = new QRadioButton(i18n("Parallel"), m_portSelectGroup);
	m_portSelectGroup->insert(m_parallelRB, INDEX_PARALLEL);
	QWhatsThis::add(m_parallelRB, i18n("If this option is checked, the camera would have to be connected one the parallel ports (known as LPT in Microsoft Windows) in your computer."));
	m_parallelRB->setEnabled(false);

	m_IEEE1394RB = new QRadioButton(i18n("IEEE1394"), m_portSelectGroup);
	m_portSelectGroup->insert(m_IEEE1394RB, INDEX_IEEE1394);
	QWhatsThis::add(m_IEEE1394RB, i18n("If this option is checked, the camera would have to be connected to one of the IEEE1394 (FireWire) ports in your computer."));
	m_IEEE1394RB->setEnabled(false);

	m_networkRB = new QRadioButton(i18n("Network"), m_portSelectGroup);
	m_portSelectGroup->insert(m_networkRB, INDEX_NETWORK);
	m_networkRB->setEnabled(false);

	// Create port settings widget stack
	m_settingsStack = new QWidgetStack(portSettingsGroup);
	connect(m_portSelectGroup, SIGNAL(clicked(int)), m_settingsStack, SLOT(raiseWidget(int)));

	// none tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No port type selected."),
		m_settingsStack), INDEX_NONE);

	// serial tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Port"), grid);
	m_serialPortCombo = new QComboBox(TRUE, grid);
	QWhatsThis::add(m_serialPortCombo, i18n("Here you should choose the serial port you connect the camera to."));
	m_settingsStack->addWidget(grid, INDEX_SERIAL);

	// parallel tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Port"), grid);
	m_parallelPortCombo = new QComboBox(TRUE, grid);
	QWhatsThis::add(m_serialPortCombo, i18n("Here you should choose the parallel port you connect the camera to."));
	m_settingsStack->addWidget(grid, INDEX_PARALLEL);

	// USB tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No further configuration is required for USB."),
		m_settingsStack), INDEX_USB);
	
	// IEEE1394 tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No further configuration is required for IEEE1394."),
		m_settingsStack), INDEX_IEEE1394);

	// network tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Host"), grid);
	m_networkHostLineEdit = new QLineEdit(grid);
	new QLabel(i18n("Port"), grid);
	m_networkPortLineEdit = new QLineEdit(grid);
	m_settingsStack->addWidget(grid, INDEX_NETWORK);

	// query gphoto2 for existing serial ports
	GPPortInfoList *port_list;
	gp_port_info_list_new(&port_list);
	gp_port_info_list_load(port_list);
	int gphoto_ports = gp_port_info_list_count(port_list);
	
	GPPortInfo port_info;
	for (int i = 0; i < gphoto_ports; i++) {
		if (gp_port_info_list_get_info(port_list, i, &port_info) >= 0) {
			if (strncmp(port_info.path, "serial:", 7) == 0)
				m_serialPortCombo->insertItem(QString::fromLatin1(port_info.path).mid(7));
			if (strncmp(port_info.path, "parallel:", 9) == 0)
				m_parallelPortCombo->insertItem(QString::fromLatin1(port_info.path).mid(7));
		}
	}
	
	// add a spacer
	rightLayout->addItem( new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding) );

	populateCameraListView();
	load();
}

bool KameraDeviceSelectDialog::populateCameraListView()
{
	CameraAbilitiesList *al;
	CameraAbilities a;
	gp_abilities_list_new(&al);
	gp_abilities_list_load(al);
	int count = gp_abilities_list_count(al);

	if (count < 0) {
		// failed to get cameras list
		gp_abilities_list_free(al);
		return false;
	}
	
	for (int i=0; i<count; i++) {
		gp_abilities_list_get_abilities(al, i, &a);
		// TODO: Handle a.status
		new QListViewItem(m_modelSel, QString::fromLocal8Bit(a.model));
	}
	gp_abilities_list_free(al);
}

void KameraDeviceSelectDialog::save()
{
	m_device->setModel(m_modelSel->currentItem()->text(0));

	QString type = m_portSelectGroup->selected()->text();

	if(type == i18n("Serial"))
		m_device->setPath("serial:" + m_serialPortCombo->currentText());
	else if(type == i18n("Parallel"))
		m_device->setPath("parallel:" + m_parallelPortCombo->currentText());
	else if(type == i18n("USB"))
 		m_device->setPath("usb:");
	else if(type == i18n("IEEE1394"))
 		m_device->setPath("ieee1394:"); // FIXME?
	else if(type == i18n("Network"))
 		m_device->setPath("network:"); // FIXME
}

void KameraDeviceSelectDialog::load()
{
	QString path = m_device->path();
	QString port = path.left(path.find(":")).lower();

	if (port == "serial") setPortType(INDEX_SERIAL);
	if (port == "parallel") setPortType(INDEX_PARALLEL);
	if (port == "usb") setPortType(INDEX_USB);
	if (port == "ieee1394") setPortType(INDEX_IEEE1394);
	if (port == "network") setPortType(INDEX_NETWORK);
	
	QListViewItem *modelItem = m_modelSel->firstChild();
	do {
		if (modelItem->text(0) == m_device->model()) {
			m_modelSel->setSelected(modelItem, true);
			m_modelSel->ensureItemVisible(modelItem);
		}
	} while ( modelItem = modelItem->nextSibling());
}

void KameraDeviceSelectDialog::slot_setModel(QListViewItem *item)
{
	QString model = item->text(0);
	
	// get the model's abilities
	CameraAbilitiesList *abilities_list;
	CameraAbilities abilities;
	gp_abilities_list_new(&abilities_list);
	gp_abilities_list_load(abilities_list);
	int idx = gp_abilities_list_lookup_model(abilities_list, model.local8Bit().data());

	// if we cannot get abilities, we cannot go forward anymore
	if (idx == GP_ERROR_MODEL_NOT_FOUND) {
		gp_abilities_list_free(abilities_list);
		slot_error(i18n("No drivers found for camera model <b>%1</b>.").arg(model));
		return;
	}
	gp_abilities_list_get_abilities(abilities_list, idx, &abilities);
	gp_abilities_list_free(abilities_list);
	
	// enable radiobuttons for supported port types
	m_serialRB->setEnabled(abilities.port & GP_PORT_SERIAL);
	m_USBRB->setEnabled(abilities.port & GP_PORT_USB);

	/* -- those port types were never implemented --
	m_parallelRB->setEnabled(abilities.port);
	m_IEEE1394RB->setEnabled(abilities.port);
	m_networkRB->setEnabled(abilities.port);
	*/

	// turn off any selected port
	QButton *selected = m_portSelectGroup->selected();
	if(selected != NULL)
		selected->toggle();
	
        // if there's only one available port type, make sure it's selected
	if (abilities.port == GP_PORT_SERIAL)
		setPortType(INDEX_SERIAL);
	if (abilities.port == GP_PORT_USB)
		setPortType(INDEX_USB);
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
