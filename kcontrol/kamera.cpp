
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
// XXX string.local8Bit() as soon as the gphoto2 API uses 'const char *'
// XXX instead of 'char *' in calls that don't modify the string
#define tocstr(x) ((char *)((x).local8Bit().operator const char *()))

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
	new QLabel(i18n("Unable to intialise the gPhoto2 libraries..."), this);
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
	m_serialRB = new QRadioButton(i18n("serial"), m_portSelectGroup);
	m_portSelectGroup->insert(m_serialRB, INDEX_SERIAL);
	m_parallelRB = new QRadioButton(i18n("parallel"), m_portSelectGroup);
	m_portSelectGroup->insert(m_parallelRB, INDEX_PARALLEL);
	m_USBRB = new QRadioButton(i18n("USB"), m_portSelectGroup);
	m_portSelectGroup->insert(m_USBRB, INDEX_USB);
	m_IEEE1394RB = new QRadioButton(i18n("IEEE1394"), m_portSelectGroup);
	m_portSelectGroup->insert(m_IEEE1394RB, INDEX_IEEE1394);
	m_networkRB = new QRadioButton(i18n("network"), m_portSelectGroup);
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
	m_serialPortLineEdit = new QLineEdit(grid);
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
		QLabel(i18n("No user defineable settings for USB"),
		m_settingsStack), INDEX_USB);
	
	// IEEE1394 tab
	m_settingsStack->addWidget(new
		QLabel(i18n("No user defineable settings for IEEE1394"),
		m_settingsStack), INDEX_IEEE1394);

	// network tab
	grid = new QGrid(2, m_settingsStack);
	grid->setSpacing(5);
	new QLabel(i18n("Host"), grid);
	m_networkHostLineEdit = new QLineEdit(grid);
	new QLabel(i18n("port"), grid);
	m_networkPortLineEdit = new QLineEdit(grid);
	m_settingsStack->addWidget(grid, INDEX_NETWORK);
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

	// enable camera configuration button if supported
	m_configureCamera->setEnabled(abilities.config);

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
		config->writeEntry("Path", m_serialPortLineEdit->text());
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
		m_serialPortLineEdit->setText(config->readEntry("Path", ""));
		
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

	char *name = item->text(0).latin1();

	// retrieve camera abilities structure
	if(gp_camera_abilities_by_name(name, &abilities) == GP_OK) {
		displayCameraAbilities(abilities);
		setPortType(INDEX_NONE);
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
	if(!openSelectedCamera())
		return;

	KMessageBox::information(this, i18n("Camera test successful!"));

	closeCamera();
}

void KKameraConfig::configureCamera(void)
{
	if(!openSelectedCamera())
		return;

	if(gp_camera_config(m_camera) != GP_OK)
		KMessageBox::error(this, i18n("Camera configuration failed."));

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

	if(camera == NULL) {
		KMessageBox::error(this, i18n("No camera selected!"));
		return false;
	}

	if(gp_camera_new_by_name(&m_camera, tocstr(camera->text(0))) != GP_OK) {
		KMessageBox::error(this, i18n("Could not access driver."
				" Check your gPhoto2 installation."));
		return false;
	}

	transferCameraPortInfoFromUI();

	if(gp_camera_init(m_camera, &m_cameraPortInfo) != GP_OK) {
		gp_camera_free(m_camera);
		m_camera = NULL;
		KMessageBox::error(this, i18n("Unable to initialise camera."
			" Check your port settings and camera connectivity"
			" and try again."));
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

	memset(&m_cameraPortInfo, 0, sizeof(m_cameraPortInfo));
	
	if(selected == NULL) {
		m_cameraPortInfo.type = GP_PORT_NONE;
		return;
	}

	QString type = selected->text();

	if(type == i18n("serial")) {
		m_cameraPortInfo.type = GP_PORT_SERIAL;
		strcpy(m_cameraPortInfo.path,
			m_serialPortLineEdit->text().local8Bit());		//lukas: FIXME!!! no strcpy never ever
		m_cameraPortInfo.speed =
			m_serialSpeedCombo->currentText().toInt();
	} else if(type == i18n("parallel")) {
		m_cameraPortInfo.type = GP_PORT_PARALLEL;
		strcpy(m_cameraPortInfo.path,
			m_parallelPortLineEdit->text().local8Bit());	//lukas: FIXME!!!
	} else if(type == i18n("USB")) {
		m_cameraPortInfo.type = GP_PORT_USB;
		strcpy(m_cameraPortInfo.path, "usb");
	} else if(type == i18n("IEEE1394")) {
		m_cameraPortInfo.type = GP_PORT_IEEE1394;
		strcpy(m_cameraPortInfo.path, "ieee1394");
	} else if(type == i18n("network")) {
		m_cameraPortInfo.type = GP_PORT_NETWORK;
		strcpy(m_cameraPortInfo.path, "network");		//lukas: FIXME!!!
		strcpy(m_cameraPortInfo.host,
			m_networkHostLineEdit->text().local8Bit());	//lukas: FIXME!!!
		m_cameraPortInfo.host_port =
			m_networkPortLineEdit->text().toInt();
	}
}
