
#include <qlayout.h>
#include <qgrid.h>
#include <qlabel.h>
#include <qvgroupbox.h>
#include <qcheckbox.h>
#include <qlineedit.h>
#include <qcombobox.h>
#include <qslider.h>
#include <qvbuttongroup.h>
#include <qradiobutton.h>

#include <klocale.h>
#include <kbuttonbox.h>

#include "kameraconfigdialog.h"
#include "kameraconfigdialog.moc"

KameraConfigDialog::KameraConfigDialog(Camera *camera,
					CameraWidget *widget,
					QWidget *parent,
					const char *name) :
KDialog(parent, name, true),
m_widgetRoot(widget)
{
	QVBoxLayout *topLayout = new QVBoxLayout(this,
						 KDialog::marginHint(),
						 KDialog::spacingHint());
	topLayout->setAutoAdd(true);

	appendWidget(this, widget);

	KButtonBox *bbox = new KButtonBox(this);

	QPushButton *okButton = bbox->addButton(i18n("OK"));
	okButton->setDefault(true);
	connect(okButton, SIGNAL(clicked()),
		this, SLOT(slotOK));

	QPushButton *cancelButton = bbox->addButton(i18n("Cancel"));
	connect(cancelButton, SIGNAL(clicked()),
		this, SLOT(reject()));
}

void KameraConfigDialog::appendWidget(QWidget *parent, CameraWidget *widget)
{
	QWidget *newParent = parent;
	QVButtonGroup *buttonGroup;
	QRadioButton *radioButton;
	QCheckBox *checkBox;
	QLineEdit *lineEdit;
	QComboBox *comboBox;
	QVGroupBox *section;
	QSlider *slider;
	QGrid *grid;

	// Add this widget to parent
	switch(widget->type) {
	case GP_WIDGET_WINDOW:
		setCaption(widget->label);
		break;
	case GP_WIDGET_SECTION:
		section = new QVGroupBox(i18n(widget->label), parent);
		newParent = section;
		break;
	case GP_WIDGET_TEXT:
		grid = new QGrid(2, parent);
		new QLabel(i18n(widget->label), grid);
		lineEdit = new QLineEdit(widget->value_string, grid);
		m_wmap.insert(widget, lineEdit);
		break;
	case GP_WIDGET_RANGE:
		slider = new QSlider(widget->min,
				     widget->max,
				     widget->increment,
				     widget->value_float,
				     QSlider::Horizontal,
				     parent);
		m_wmap.insert(widget, slider);
		break;
	case GP_WIDGET_TOGGLE:
		checkBox = new QCheckBox(i18n(widget->label), parent);
		checkBox->setChecked(widget->value_int);
		m_wmap.insert(widget, checkBox);
		break;
	case GP_WIDGET_RADIO:
		buttonGroup = new QVButtonGroup(i18n("Choice"), parent);
		for(int i = 0; i < widget->choice_count; ++i) {
			new QRadioButton(widget->choice[i], buttonGroup);
			if(!strcmp(widget->value_string, widget->choice[i]))
				buttonGroup->setButton(i);
		}
		m_wmap.insert(widget, buttonGroup);
		break;
	case GP_WIDGET_MENU:
		comboBox = new QComboBox(FALSE, parent);
		comboBox->clear();
		for(int i = 0; i < widget->choice_count; ++i) {
			comboBox->insertItem(widget->choice[i]);
			if(!strcmp(widget->value_string, widget->choice[i]))
				comboBox->setCurrentItem(i);
		}
		m_wmap.insert(widget, comboBox);
		break;
	case GP_WIDGET_BUTTON:
		// I can't see a way of implementing this. Since there is
		// no way of telling which button sent you a signal, we
		// can't map to the appropriate widget->callback
		new QLabel(i18n("Button (not supported by KControl)"), parent);
		break;
	case GP_WIDGET_DATE:
		new QLabel(i18n("Date (not supported by KControl)"), parent);
		break;
	case GP_WIDGET_NONE:
	default:
		return;
	}

	// Append all this widgets children
	for(int i = 0; i < widget->children_count; ++i)
		appendWidget(newParent, widget->children[i]);
}

void KameraConfigDialog::updateWidgetValue(CameraWidget *widget)
{
	QLineEdit *lineEdit;
	QCheckBox *checkBox;
	QComboBox *comboBox;
	QSlider *slider;
	QVButtonGroup *buttonGroup;
	QRadioButton *radioButton;

	const char *value_string;
	float value_float;
	int value_int;

	switch(widget->type) {
	case GP_WIDGET_WINDOW:
		// nothing to do
		break;
	case GP_WIDGET_SECTION:
		// nothing to do
		break;
	case GP_WIDGET_TEXT:
		lineEdit = (QLineEdit *) m_wmap[widget];
		value_string = lineEdit->text().local8Bit();
		gp_widget_value_set(widget, value_string);
		break;
	case GP_WIDGET_RANGE:
		slider = (QSlider *) m_wmap[widget];
		value_float = slider->value();
		gp_widget_value_set(widget, &value_float);
		break;
	case GP_WIDGET_TOGGLE:
		checkBox = (QCheckBox *) m_wmap[widget];
		value_int = checkBox->isChecked() ? 1 : 0;
		gp_widget_value_set(widget, &value_int);
		break;
	case GP_WIDGET_RADIO:
		buttonGroup = (QVButtonGroup *) m_wmap[widget];
		value_string = buttonGroup->selected()->text().local8Bit();
		gp_widget_value_set(widget, value_string);
		break;
	case GP_WIDGET_MENU:
		comboBox = (QComboBox *) m_wmap[widget];
		value_string = comboBox->currentText().local8Bit();
		gp_widget_value_set(widget, value_string);
		break;
	case GP_WIDGET_BUTTON:
		// nothing to do
		break;
	case GP_WIDGET_DATE:
		// not implemented
		break;
	case GP_WIDGET_NONE:
	default:
		// nothing to do
		break;
	}
	
	// Copy child widget values
	for(int i = 0; i < widget->children_count; ++i)
		updateWidgetValue(widget->children[i]);
}

void KameraConfigDialog::slotOK(void)
{
	// Copy Qt widget values into CameraWidget hierarchy
	updateWidgetValue(m_widgetRoot);

	// 'ok' dialog
	accept();
}
