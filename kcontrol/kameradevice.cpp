#include <qobject.h>
#include <qlayout.h>
#include <qwidgetstack.h>
#include <qvbuttongroup.h>
#include <qvgroupbox.h>
#include <qcombobox.h>
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
	if (!m_model)
		return false;

	int result = gp_camera_abilities_by_name(m_model.local8Bit().data(), &m_abilities);
	if (result == GP_OK)
		return true;
	else {
		emit error(i18n("Description of abilities for camera %1 is not available."
			     " Configuration options may be incorrect.").arg(m_model));
		return false;
	}
}

bool KCamera::initCamera()
{
	if (m_camera)
		return m_camera;
	else {
		int result;

		initInformation();

		if (!m_model || !m_path)
			return false;

		result = gp_camera_new(&m_camera);
		if (result != GP_OK) {
			// m_camera is not initialized, so we cannot get result as string
			emit error(i18n("Could not access driver. Check your gPhoto2 installation."));
			return false;
		}

		// set the camera's model
		gp_camera_set_model(m_camera, m_model.local8Bit().data()); 
		gp_camera_set_port_path(m_camera, m_path.local8Bit().data());

		// this might take some time (esp. for non-existant camera) - better be done asynchronously
		result = gp_camera_init(m_camera);
		if (result != GP_OK) {
			gp_camera_free(m_camera);
			m_camera = NULL;
			emit error(
				i18n("Unable to initialize camera. Check your port settings and camera connectivity and try again."),
				gp_camera_get_result_as_string(m_camera, result));
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
		emit error(i18n("Camera configuration failed."), gp_camera_get_result_as_string(m_camera, result));
		return false; 
	}

	KameraConfigDialog kcd(m_camera, window);
	result = kcd.exec() ? GP_PROMPT_OK : GP_PROMPT_CANCEL;

	if (result == GP_PROMPT_OK) {
		result = gp_camera_set_config(m_camera, window);
		if (result != GP_OK) {
			emit error(i18n("Camera configuration failed."), gp_camera_get_result_as_string(m_camera, result));
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

QStringList KCamera::supportedPorts()
{
	initInformation();
	QStringList ports;
	if (SERIAL_SUPPORTED(m_abilities.port)) ports.append("serial");
	if (PARALLEL_SUPPORTED(m_abilities.port)) ports.append("parallel");
	if (USB_SUPPORTED(m_abilities.port)) ports.append("usb");
	if (IEEE1394_SUPPORTED(m_abilities.port)) ports.append("ieee1394");
	if (NETWORK_SUPPORTED(m_abilities.port)) ports.append("network");
	return ports;
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
	m_parallelRB = new QRadioButton(i18n("Parallel"), m_portSelectGroup);
	m_portSelectGroup->insert(m_parallelRB, INDEX_PARALLEL);
	QWhatsThis::add(m_parallelRB, i18n("If this option is checked, the camera would have to be connected one the parallel ports (known as LPT in Microsoft Windows) in your computer."));
	m_USBRB = new QRadioButton(i18n("USB"), m_portSelectGroup);
	m_portSelectGroup->insert(m_USBRB, INDEX_USB);
	QWhatsThis::add(m_USBRB, i18n("If this option is checked, the camera would have to be connected to one of the USB slots in your computer or USB hub."));
	m_IEEE1394RB = new QRadioButton(i18n("IEEE1394"), m_portSelectGroup);
	m_portSelectGroup->insert(m_IEEE1394RB, INDEX_IEEE1394);
	QWhatsThis::add(m_IEEE1394RB, i18n("If this option is checked, the camera would have to be connected to one of the IEEE1394 (FireWire) ports in your computer."));
	m_networkRB = new QRadioButton(i18n("Network"), m_portSelectGroup);
	m_portSelectGroup->insert(m_networkRB, INDEX_NETWORK);

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
	int gphoto_ports = gp_port_count_get();
	gp_port_info info;
	for (int i = 0; i < gphoto_ports; i++) {
		if (gp_port_info_get(i, &info) >= 0) {
			if (strncmp(info.path, "serial:", 7) == 0)
				m_serialPortCombo->insertItem(QString::fromLatin1(info.path).mid(7));
			if (strncmp(info.path, "parallel:", 9) == 0)
				m_parallelPortCombo->insertItem(QString::fromLatin1(info.path).mid(7));
		}
	}
	
	// add a spacer
	rightLayout->addItem( new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding) );

	populateCameraListView();
	load();
}

bool KameraDeviceSelectDialog::populateCameraListView()
{
	int numCams = gp_camera_count();

	if(numCams < 0) {
		// XXX libgphoto2 failed to get the camera list
		return false;
	} else {
		for(int x = 0; x < numCams; ++x) {
			const char *modelName;
			if(gp_camera_name(x, &modelName) == GP_OK) {
				new QListViewItem(m_modelSel, modelName);
			}
		}
		return true;
	}
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
	
	CameraAbilities abilities;
	int result = gp_camera_abilities_by_name(model.local8Bit().data(), &abilities);
	if (result == GP_OK) {
		// enable radiobuttons for supported port types
		m_serialRB->setEnabled(SERIAL_SUPPORTED(abilities.port));
		m_parallelRB->setEnabled(PARALLEL_SUPPORTED(abilities.port));
		m_USBRB->setEnabled(USB_SUPPORTED(abilities.port));
		m_IEEE1394RB->setEnabled(IEEE1394_SUPPORTED(abilities.port));
		m_networkRB->setEnabled(NETWORK_SUPPORTED(abilities.port));

		// turn off any selected port
		QButton *selected = m_portSelectGroup->selected();
		if(selected != NULL)
			selected->toggle();
	
	        // if there's only one available port type, make sure it's selected
		if ((SERIAL_SUPPORTED(abilities.port)?1:0) + (PARALLEL_SUPPORTED(abilities.port)?1:0) + (USB_SUPPORTED(abilities.port)?1:0) + (IEEE1394_SUPPORTED(abilities.port)?1:0) + (NETWORK_SUPPORTED(abilities.port)?1:0)) {
			if (SERIAL_SUPPORTED(abilities.port)) setPortType(INDEX_SERIAL);
			if (PARALLEL_SUPPORTED(abilities.port)) setPortType(INDEX_PARALLEL);
			if (USB_SUPPORTED(abilities.port)) setPortType(INDEX_USB);
			if (IEEE1394_SUPPORTED(abilities.port)) setPortType(INDEX_IEEE1394);
			if (NETWORK_SUPPORTED(abilities.port)) setPortType(INDEX_NETWORK);
		};
	} else {
		slot_error(i18n("Description of abilities for camera %1 is not available."
			     " Configuration options may be incorrect.").arg(model));
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