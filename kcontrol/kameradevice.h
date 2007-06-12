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
#ifndef __kameradevice_h__
#define __kameradevice_h__

#include <qmap.h>
#include <kdialog.h>
#include <Q3ListViewItem>
class KConfig;
class QString;
class K3ListView;
class Q3WidgetStack;
class Q3VButtonGroup;
class QComboBox;
class QLineEdit;
class QRadioButton;
class Q3GroupBox;

class KCamera : public QObject {
	friend class KameraDeviceSelectDialog;
	Q_OBJECT
public:
	KCamera(const QString &name, const QString &path);
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

class KameraDeviceSelectDialog : public KDialog
{
	Q_OBJECT
public:
	KameraDeviceSelectDialog(QWidget *parent, KCamera *device);
	void save();
	void load();
protected slots:
	void slot_setModel(Q3ListViewItem *item);
	void slot_error(const QString &message);
	void slot_error(const QString &message, const QString &details);
protected:
	KCamera *m_device;
	
	bool populateCameraListView(void);
	void setPortType(int type);

	// port settings widgets
	K3ListView *m_modelSel;
	QLineEdit *m_nameEdit;
	Q3WidgetStack *m_settingsStack;
	Q3VButtonGroup *m_portSelectGroup;
	Q3GroupBox *m_portSettingsGroup;
	QComboBox *m_serialPortCombo;
	// port selection radio buttons
	QRadioButton *m_serialRB;
	QRadioButton *m_USBRB;
};

#endif
