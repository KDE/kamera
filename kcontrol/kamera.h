
#ifndef __kamera_h__
#define __kamera_h__

#include <kcmodule.h>

extern "C"
{
	#include <gphoto2.h>
//Defination for this is missing in gphoto2.h
	int gp_camera_exit (Camera *camera); 
}

class QWidget;
class KListView;
class QRadioButton;
class QPushButton;
class QComboBox;
class QVButtonGroup;
class QLineEdit;
class QWidgetStack;
class QCheckBox;

class KKameraConfig : public KCModule
{
	Q_OBJECT

public:
	KKameraConfig(QWidget *parent = 0L, const char *name = 0L);
	virtual ~KKameraConfig();

	// KCModule interface methods
	void load();
	void save();
	void defaults();
	int buttons();

	static int frontend_prompt(Camera *camera, CameraWidget *widgets);
	int doConfigureCamera(Camera *camera, CameraWidget *widgets);

protected slots:
	void setCameraType(QListViewItem *item);
	void setPortType(int type);

	void configureCamera(void);
	void testCamera(void);

private:
	// manage widgets
	void displayGPFailureDialogue(void);
	void displayGPSuccessDialogue(void);
	void displayCameraAbilities(const CameraAbilities &abilities);
	bool populateCameraListView(void);
	bool openSelectedCamera(void);
	void closeCamera(void);
	void transferCameraPortInfoFromUI(void);
	
	// camera model selection listview
	KListView *m_camSel;
	QString *m_cameraModel;

	QWidgetStack *m_settingsStack;

	// port selection radio buttons
	QRadioButton *m_serialRB;
	QRadioButton *m_parallelRB;
	QRadioButton *m_USBRB;
	QRadioButton *m_IEEE1394RB;
	QRadioButton *m_networkRB;

	// configure camera options push button
	QPushButton *m_configureCamera;

	QCheckBox *m_cacheHackCB;

	// port settings widgets
	QVButtonGroup *m_portSelectGroup;
	QComboBox *m_serialSpeedCombo;
	QComboBox *m_serialPortCombo;
	QLineEdit *m_parallelPortLineEdit;
	QLineEdit *m_networkHostLineEdit;
	QLineEdit *m_networkPortLineEdit;

	// true if libgphoto2 was initialised successfully in
	// the constructor
	bool m_gpInitialised;

	static KKameraConfig *m_instance;

	Camera *m_camera;
};

#endif

