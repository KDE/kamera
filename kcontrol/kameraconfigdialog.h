
#ifndef __kameraconfigdialog_h__
#define __kameraconfigdialog_h__

#include <qmap.h>
#include <kdialog.h>
#include <qtabwidget.h>

extern "C" {
	#include <gphoto2.h>
}

class KameraConfigDialog : public KDialog
{
	Q_OBJECT
public:
	KameraConfigDialog(Camera *camera, CameraWidget *widget,
			   QWidget *parent = 0, const char *name = 0);

private slots:
	void slotOK();

private:
	void appendWidget(QWidget *parent, CameraWidget *widget);
	void updateWidgetValue(CameraWidget *widget);
	
	QMap<CameraWidget *, QWidget *> m_wmap;
	CameraWidget *m_widgetRoot;
	QTabWidget *m_tabWidget;
};

#endif
