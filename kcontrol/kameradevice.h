
#ifndef __kameradevice_h__
#define __kameradevice_h__

#include <qmap.h>
#include <kdialogbase.h>
#include <config.h>

class KConfig;
class QString;
class KListView;
class QWidgetStack;
class QVButtonGroup;
class QVGroupBox;
class QComboBox;
class QLineEdit;
class QRadioButton;

class KCamera : public QObject {
	friend class KameraDeviceSelectDialog;
	Q_OBJECT
public:
	KCamera(const QString &name);
	~KCamera();
	void invalidateCamera();
	bool configure();
	void load(KConfig *m_config);
	void save(KConfig *m_config);
	bool test();
	QStringList supportedPorts();

	Camera* camera();
	QString name() const { return m_name ; }
	QString model() const { return m_model; }
	QString path() const { return m_path; }
	QString portName();

	QString summary();
	CameraAbilities abilities();

	void setName(const QString &name);
	void setModel(const QString &model);
	void setPath(const QString &path);

	bool isTestable() const;
	bool isConfigurable();

signals:
	void error(const QString &message);
	void error(const QString &message, const QString &details);
	
protected:
	bool initInformation();
	bool initCamera();
//	void doConfigureCamera(Camera *camera, CameraWidget *widgets);
//	int frontend_prompt(Camera *camera, CameraWidget *widgets);

	Camera *m_camera;
//	KConfig *m_config;
	QString m_name; // the camera's real name
	QString m_model;
	QString m_path;
	CameraAbilities m_abilities;
	CameraAbilitiesList *m_abilitylist;
};

class KameraDeviceSelectDialog : public KDialogBase
{
	Q_OBJECT
public:
	KameraDeviceSelectDialog(QWidget *parent, KCamera *device);
	void save();
	void load();
protected slots:
	void slot_setModel(QListViewItem *item);
	void slot_error(const QString &message);
	void slot_error(const QString &message, const QString &details);
protected:
	KCamera *m_device;
	
	bool populateCameraListView(void);
	void setPortType(int type);

	// port settings widgets
	KListView *m_modelSel;
	QLineEdit *m_nameEdit;
	QWidgetStack *m_settingsStack;
	QVButtonGroup *m_portSelectGroup;
	QVGroupBox *m_portSettingsGroup;
	QComboBox *m_serialPortCombo;
	// port selection radio buttons
	QRadioButton *m_serialRB;
	QRadioButton *m_USBRB;
};

#endif
