
#include <qlabel.h>
#include <qlineedit.h>
#include <qcombobox.h>
#include <qlayout.h>
#include <qradiobutton.h>
#include <qpushbutton.h>
#include <qvbox.h>
#include <qvgroupbox.h>
#include <qvbuttongroup.h>
#include <qgrid.h>
#include <qwidgetstack.h>
#include <qcheckbox.h>
#include <qdir.h>

#include <kconfig.h>
#include <klistview.h>
#include <kdialog.h>
#include <klocale.h>
#include <kglobal.h>
#include <kmessagebox.h>

#include "kameraconfigdialog.h"
#include "kamera.h"
#include "kamera.moc"

// XXX HACK HACK HACK
// XXX All tocstr(string) references can be safely replaced with
// XXX string.latin1() as soon as the gphoto2 API uses 'const char *'
// XXX instead of 'char *' in calls that don't modify the string
#define tocstr(x) ((char *)((x).latin1()))

// Undefined constant from struct CameraAbilities
// definition in gphoto2-datatypes.h
static const int GPHOTO2_CAMERA_NAME_MAX = 128;

static const int INDEX_NONE= 0;
static const int INDEX_SERIAL = 1;
static const int INDEX_PARALLEL = 2;
static const int INDEX_USB= 3;
static const int INDEX_IEEE1394 = 4;
static const int INDEX_NETWORK = 5;

extern "C"
{
	KCModule *create_kamera(QWidget *parent, const char *name)
	{
		KGlobal::locale()->insertCatalogue("kcmkamera");
		return new KKameraConfig(parent, name);
	}
}

KKameraConfig *KKameraConfig::m_instance = NULL;

KKameraConfig::KKameraConfig(QWidget *parent, const char *name)
:KCModule(parent, name),
m_gpInitialised(false)
{
	m_cameraModel = new QString;
	if(gp_init(GP_DEBUG_HIGH) == GP_ERROR) {
		displayGPFailureDialogue();
	} else if(gp_frontend_register(NULL,
				       NULL,
				       NULL,
				       NULL,
				       frontend_prompt) == GP_ERROR) {
		gp_exit();
		displayGPFailureDialogue();
 	} else {
		// store instance for frontend_prompt
		m_instance = this;

		// remember to gp_exit() in destructor
		m_gpInitialised = true;

		// build and display normal dialogue
		displayGPSuccessDialogue();

		// XXX unchecked return
		populateCameraListView();

		// load existing configuration
		load();
	}
}

KKameraConfig::~KKameraConfig()
{
	// shutdown libgphoto2 if necessary

	if(m_gpInitialised == true) {
		gp_exit();
	}
}

void KKameraConfig::defaults()
{
}

void KKameraConfig::displayGPFailureDialogue(void)
{
	new QLabel(i18n("Unable to initialize the gPhoto2 libraries."), this);
}

void KKameraConfig::displayGPSuccessDialogue(void)
{
	QHBoxLayout *topLayout = new QHBoxLayout(this,
						KDialog::marginHint(),
						KDialog::spacingHint());

	topLayout->setAutoAdd(true);

	m_camSel = new KListView(this);
	m_camSel->addColumn(i18n("Supported Cameras"));
	m_camSel->setColumnWidthMode(0, QListView::Maximum);

	// Make sure listview only as wide as it needs to be
	QSizePolicy camSelSizePolicy(QSizePolicy::Maximum,
				     QSizePolicy::Preferred);
	m_camSel->setSizePolicy(camSelSizePolicy);

	connect(m_camSel, SIGNAL(selectionChanged(QListViewItem *)),
		SLOT(setCameraType(QListViewItem *)));

	QVBox *rightLayout = new QVBox(this);
	rightLayout->setSpacing(10);
						
	m_portSelectGroup = new QVButtonGroup(i18n("Port"), rightLayout);
	QVGroupBox *portSettingsGroup = new QVGroupBox(i18n("Port Settings"),
							rightLayout);
	QVGroupBox *miscSettingsGroup = new QVGroupBox(i18n("Miscellaneous"),
							rightLayout);

	QGrid *grid = new QGrid(2, rightLayout);
	grid->setSpacing(5);
	QPushButton *testCamera = new QPushButton(i18n("Test"), grid);
	m_configureCamera = new QPushButton(i18n("Configure"), grid);

	connect(testCamera, SIGNAL(clicked()),
		this, SLOT(testCamera()));
	connect(m_configureCamera, SIGNAL(clicked()),
		this, SLOT(configureCamera()));

	m_cacheHackCB = new QCheckBox(i18n("Use camera previews for"
					" Konqueror thumbnails"),
					miscSettingsGroup);

	// Create port type selection radiobuttons.
	m_serialRB = new QRadioButton(i18n("Serial"), m_portSelectGroup);
	m_portSelectGroup->insert(m_serialRB, INDEX_SERIAL);
	m_parallelRB = new QRadioButton(i18n("Parallel"), m_portSelectGroup);
	m_portSelectGroup->insert(m_parallelRB, INDEX_PARALLEL);
	m_USBRB = new QRadioButton(i18n("USB"), m_portSelectGroup);
	m_portSelectGroup->insert(m_USBRB, INDEX_USB);
	m_IEEE1394RB = new QRadioButton(i18n("IEEE1394"), m_portSelectGroup);
	m_portSelectGroup->insert(m_IEEE1394RB, INDEX_IEEE1394);
	m_networkRB = new QRadioButton(i18n("Network"), m_portSelectGroup);
	m_portSelectGroup->insert(m_networkRB, INDEX_NETWORK);

	// Create port settings widget stack
	m_settingsStack = new QWidgetStack(portSettingsGroup);

	connect(m_portSelectGroup, SIGNAL(clicked(int)),
		m_settingsStack, SLOT(raiseWidget(int)));

	// none tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No port type selected."),
		m_settingsStack), INDEX_NONE);

	// serial tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Port"), grid);
	m_serialPortCombo = new QComboBox(TRUE, grid);
	new QLabel(i18n("Speed"), grid);
	m_serialSpeedCombo = new QComboBox(FALSE, grid);
	m_settingsStack->addWidget(grid, INDEX_SERIAL);

	// parallel tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Port"), grid);
	m_parallelPortLineEdit = new QLineEdit(grid);
	m_settingsStack->addWidget(grid, INDEX_PARALLEL);

	// USB tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No user definable settings for USB"),
		m_settingsStack), INDEX_USB);
	
	// IEEE1394 tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No user definable settings for IEEE1394"),
		m_settingsStack), INDEX_IEEE1394);

	// network tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Host"), grid);
	m_networkHostLineEdit = new QLineEdit(grid);
	new QLabel(i18n("port"), grid);
	m_networkPortLineEdit = new QLineEdit(grid);
	m_settingsStack->addWidget(grid, INDEX_NETWORK);

	// query gphoto2 for existing ports
	int gphoto_ports = gp_port_count_get();
	gp_port_info info;
	for (int i = 0; i < gphoto_ports; i++) {
		if (gp_port_info_get(i, &info) >= 0) {
			if (strncmp(info.path, "serial:", 7) == 0)
				m_serialPortCombo->insertItem(QString::fromLatin1(info.path).mid(7));
		}
	}
}

void KKameraConfig::displayCameraAbilities(const CameraAbilities &abilities)
{
	// turn off any selected port
	QButton *selected = m_portSelectGroup->selected();
	if(selected != NULL)
		selected->toggle();
	
	// enable radiobuttons for supported port types
	m_serialRB->setEnabled(SERIAL_SUPPORTED(abilities.port));
	m_parallelRB->setEnabled(PARALLEL_SUPPORTED(abilities.port));
	m_USBRB->setEnabled(USB_SUPPORTED(abilities.port));
	m_IEEE1394RB->setEnabled(IEEE1394_SUPPORTED(abilities.port));
	m_networkRB->setEnabled(NETWORK_SUPPORTED(abilities.port));
	
        // if there's only one available port type, make sure it's selected
	if ((SERIAL_SUPPORTED(abilities.port)?1:0) + (PARALLEL_SUPPORTED(abilities.port)?1:0) + (USB_SUPPORTED(abilities.port)?1:0) + (IEEE1394_SUPPORTED(abilities.port)?1:0) + (NETWORK_SUPPORTED(abilities.port)?1:0)) {
		if (SERIAL_SUPPORTED(abilities.port)) setPortType(INDEX_SERIAL);
		if (PARALLEL_SUPPORTED(abilities.port)) setPortType(INDEX_PARALLEL);
		if (USB_SUPPORTED(abilities.port)) setPortType(INDEX_USB);
		if (IEEE1394_SUPPORTED(abilities.port)) setPortType(INDEX_IEEE1394);
		if (NETWORK_SUPPORTED(abilities.port)) setPortType(INDEX_NETWORK);
	};

	// enable camera configuration button if supported
	m_configureCamera->setEnabled(abilities.operations & GP_OPERATION_CONFIG);

	// populate serial speed listbox from abilities
	if(SERIAL_SUPPORTED(abilities.port)) {
		m_serialSpeedCombo->clear();

		for(int i = 0; abilities.speed[i]; ++i)
			m_serialSpeedCombo->insertItem(QString::number(abilities.speed[i]));

		// default to max speed
		m_serialSpeedCombo->setCurrentItem(m_serialSpeedCombo->count() - 1);
	}
}

bool KKameraConfig::populateCameraListView(void)
{
	int numCams = gp_camera_count();

	if(numCams < 0) {
		// XXX libgphoto2 failed to get the camera list
		return false;
	} else {
		for(int x = 0; x < numCams; ++x) {
			char camName[GPHOTO2_CAMERA_NAME_MAX];

			if(gp_camera_name(x, camName) == GP_OK) {
				new QListViewItem(m_camSel, camName);
			}
		}

		return true;
	}
}

void KKameraConfig::save(void)
{
	// open konfiguration object
	KConfig *config = new KConfig("kioslaverc");
	config->setGroup("Kamera Settings");

	QListViewItem *driver = m_camSel->selectedItem();

	if(driver == NULL) {
		delete config;
		return;
	}

	// Store driver name
	config->writeEntry("Driver", driver->text(0));

	QButton *selected = m_portSelectGroup->selected();

	if(selected == NULL) {
		delete config;
		return;
	}

	// A port type is selected, store related settings
	QString type = selected->text();

	if(type == i18n("serial")) {
		config->writeEntry("Port", "serial");
		config->writeEntry("Path", m_serialPortCombo->currentText());
		config->writeEntry("Speed", m_serialSpeedCombo->currentText());
	} else if(type == i18n("parallel")) {
		config->writeEntry("Port", "parallel");
		config->writeEntry("Path", m_parallelPortLineEdit->text());
	} else if(type == i18n("USB")) {
		config->writeEntry("Port", "usb");
	} else if(type == i18n("IEEE1394")) {
		config->writeEntry("Port", "ieee1394");
	} else if(type == i18n("network")) {
		config->writeEntry("Port", "network");
		config->writeEntry("NetHost", m_networkHostLineEdit->text());
		config->writeEntry("NetPort", m_networkPortLineEdit->text());
	}

	config->writeEntry("PreviewThumbs",
			   m_cacheHackCB->isChecked() ? "true" : "false");

	delete config;
}

void KKameraConfig::load(void)
{
	KConfig *config = new KConfig("kioslaverc");
	config->setGroup("Kamera Settings");

	QString driver = config->readEntry("Driver", "");

	bool found = false;

	// search m_camSel for driver name
	for(QListViewItem *tmp = m_camSel->firstChild();
	    tmp != NULL;
	    tmp = tmp->nextSibling()) {
		if(tmp->text(0) == driver) {
			m_camSel->setSelected(tmp, true);
			m_camSel->ensureItemVisible(tmp);
			found = true;
			break;
		}
	}

	// no driver found, nothing left to do
	if(found == false) {
		setPortType(INDEX_NONE);
		return;
	}

	QString port = config->readEntry("Port", "none");

	if(port == "none") {
		setPortType(INDEX_NONE);
		return;
	} else if(port == "serial") {
		QString path = config->readEntry("Path", "");
		if (!path.isEmpty()) {
			for (int i = 0; i < m_serialPortCombo->count(); ++i)
				if (m_serialPortCombo->text(i) == path)
					m_serialPortCombo->setCurrentItem(i);
		}
		QString speed = config->readEntry("Speed", "");

		// see if we can find 'speed' in available list - default
		// to maximum if not found
		for(int i = 0; i < m_serialSpeedCombo->count(); ++i) {
			m_serialSpeedCombo->setCurrentItem(i);
			if(m_serialSpeedCombo->currentText() == speed)
				break;
		}

		setPortType(INDEX_SERIAL);
	} else if(port == "parallel") {
		m_parallelPortLineEdit->setText(config->readEntry("Path", ""));
		setPortType(INDEX_PARALLEL);
	} else if(port == "usb") {
		setPortType(INDEX_USB);
	} else if(port == "ieee1394") {
		setPortType(INDEX_IEEE1394);
	} else if(port == "network") {
		m_networkHostLineEdit->setText(config->readEntry("NetHost",
								 ""));
		m_networkPortLineEdit->setText(config->readEntry("NetPort",
								 ""));
		setPortType(INDEX_NETWORK);
	}

	m_cacheHackCB->setChecked(config->readBoolEntry("PreviewThumbs", true));
}

void KKameraConfig::setCameraType(QListViewItem *item)
{
	CameraAbilities abilities;

        *m_cameraModel = item->text(0);
	char *name = tocstr(*m_cameraModel);

	// retrieve camera abilities structure
	if(gp_camera_abilities_by_name(name, &abilities) == GP_OK) {
		displayCameraAbilities(abilities);
	} else {
		// XXX display error ?
	}
}

void KKameraConfig::setPortType(int type)
{
	// Enable the correct button
	m_portSelectGroup->setButton(type);

	// Bring the right tab to the front
	m_settingsStack->raiseWidget(type);
}

void KKameraConfig::testCamera(void)
{
	// TODO: Make testing non-blocking (maybe via KIO?)
	// Currently, a failed serial test times out at about 30 sec
	if(!openSelectedCamera())
		return;

	KMessageBox::information(this, i18n("Camera test was successful."));

	closeCamera();
}

void KKameraConfig::configureCamera(void)
{
	CameraWidget *window;
	int result;

	if(!openSelectedCamera())
		return;

	result = gp_camera_get_config(m_camera, &window);
	if (result != GP_OK)
		KMessageBox::detailedError(this,
			i18n("Camera configuration failed."),
			gp_camera_get_result_as_string(m_camera, result));
	

	if (frontend_prompt (m_camera, window) == GP_PROMPT_OK) {
		result = gp_camera_set_config(m_camera, window);
		if (result != GP_OK)
			KMessageBox::detailedError(this,
				i18n("Camera configuration failed."),
				gp_camera_get_result_as_string(m_camera, result));
	}

	closeCamera();
}

int KKameraConfig::doConfigureCamera(Camera *camera, CameraWidget *widgets)
{
	KameraConfigDialog kcd(camera, widgets);

	return kcd.exec() ? GP_PROMPT_OK : GP_PROMPT_CANCEL;
}

int KKameraConfig::frontend_prompt(Camera *camera, CameraWidget *widgets)
{
	if(m_instance)
		return m_instance->doConfigureCamera(camera, widgets);
}

bool KKameraConfig::openSelectedCamera(void)
{
	QListViewItem *camera = m_camSel->selectedItem();
	int result;

	if(camera == NULL) {
		KMessageBox::error(this, i18n("No camera selected!"));
		return false;
	}

	result = gp_camera_new(&m_camera);
	if (result != GP_OK) {
		// m_camera is not initialized, so we cannot get result as string
		KMessageBox::error(this, i18n("Could not access driver."
				" Check your gPhoto2 installation."));
		return false;
	}
	
	// set the camera's model
	snprintf(m_camera->model, sizeof(m_camera->model)-1, "%s", m_cameraModel->latin1()); 

	// fill-in the CameraPortInfo structure
	transferCameraPortInfoFromUI();

	// this might take some time (esp. for non-existant camera) - better be done asynchronously
	result = gp_camera_init(m_camera);
	if (result != GP_OK) {
		gp_camera_free(m_camera);
		m_camera = NULL;
		KMessageBox::detailedError(this, i18n("Unable to initialize camera.\n"
			" Check your port settings and camera connectivity and try again."),
			gp_camera_get_result_as_string(m_camera, result));
		return false;
	}

	return true;
}

void KKameraConfig::closeCamera(void)
{
	if(m_camera)
		gp_camera_free(m_camera);
}

void KKameraConfig::transferCameraPortInfoFromUI(void)
{
	QButton *selected = m_portSelectGroup->selected();
	
	// According to the current API, there are no functions to access CameraPortInfo internals,
	// so we modify the members directly.

	if(selected == NULL) {
		m_camera->port->type = GP_PORT_NONE;
		return;
	}

	QString type = selected->text();

	if(type == i18n("serial")) {
		m_camera->port->type = GP_PORT_SERIAL;
		snprintf(m_camera->port->path, sizeof(m_camera->port->path)-1, "serial:%s", m_serialPortCombo->currentText().latin1());
		m_camera->port->speed =	m_serialSpeedCombo->currentText().toInt();
	} else if(type == i18n("parallel")) {
		m_camera->port->type = GP_PORT_PARALLEL;
		snprintf(m_camera->port->path, sizeof(m_camera->port->path)-1, "parallel:%s", m_parallelPortLineEdit->text().latin1());
	} else if(type == i18n("USB")) {
		m_camera->port->type = GP_PORT_USB;
		snprintf(m_camera->port->path, sizeof(m_camera->port->path)-1, "usb:");
	} else if(type == i18n("IEEE1394")) {
		m_camera->port->type = GP_PORT_IEEE1394;
		snprintf(m_camera->port->path, sizeof(m_camera->port->path)-1, "ieee1394:");
	} else if(type == i18n("network")) {
		m_camera->port->type = GP_PORT_NETWORK;
//		strncpy(m_camera->port->path, "network");
//lukas: FIXME!!!
//		strcpy(m_cameraPortInfo.host,
//			m_networkHostLineEdit->text().local8Bit());	//lukas: FIXME!!!
//		m_cameraPortInfo.host_port =
//			m_networkPortLineEdit->text().toInt();
	}
}
