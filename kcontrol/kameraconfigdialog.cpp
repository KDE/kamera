
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
#include <qvbox.h>
#include <qtabwidget.h>
#include <qscrollview.h>
#include <qwhatsthis.h>

#include <klocale.h>
#include <kbuttonbox.h>
#include <stdio.h>

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

	m_tabWidget = 0;

	appendWidget(this, widget);

	KButtonBox *bbox = new KButtonBox(this);

	QPushButton *okButton = bbox->addButton(i18n("OK"));
	okButton->setDefault(true);
	connect(okButton, SIGNAL(clicked()),
		this, SLOT(slotOK()));

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
	QSlider *slider;
	QGrid *grid;
	QVGroupBox *vGroupBox;
	QVBoxLayout *tabLayout;
	QWidget *tab;
	QVBox *tabContainer;
	
	CameraWidgetType widget_type;
	const char *widget_name;
	const char *widget_info;
	const char *widget_label;
	float widget_value_float;
	int widget_value_int;
	const char *widget_value_string;
	gp_widget_get_type(widget, &widget_type);
	gp_widget_get_label(widget, &widget_label);
	gp_widget_get_info(widget, &widget_info);
	gp_widget_get_name(widget, &widget_name);
	
	QString whats_this = QString::fromLocal8Bit(widget_info);	// gphoto2 doesn't seem to have any standard for i18n

	// Add this widget to parent
	switch(widget_type) {
	case GP_WIDGET_WINDOW:
		setCaption(widget_label);
		break;
	case GP_WIDGET_SECTION:
		if (!m_tabWidget)
			m_tabWidget = new QTabWidget(parent);
		tab = new QWidget(m_tabWidget);
		tabLayout = new QVBoxLayout(tab); // attach a layouting policy (so we could also add a spacer)
		m_tabWidget->insertTab(tab, widget_label);
		tabContainer = new QVBox(tab);
		tabLayout->addWidget(tabContainer);
		newParent = tabContainer;
		break;
	case GP_WIDGET_TEXT:
		gp_widget_get_value(widget, &widget_value_string);

		grid = new QGrid(2, parent);
		new QLabel(widget_label, grid);
		lineEdit = new QLineEdit(widget_value_string, grid);
		m_wmap.insert(widget, lineEdit);

		if (!whats_this.isEmpty())
			QWhatsThis::add(grid, whats_this);

		break;
	case GP_WIDGET_RANGE:
		float widget_low;
		float widget_high;
		float widget_increment;
		gp_widget_get_range(widget, &widget_low, &widget_high, &widget_increment);
		gp_widget_get_value(widget, &widget_value_float);
	
		vGroupBox = new QVGroupBox(widget_label, parent);
		slider = new QSlider(widget_low,
				     widget_high,
				     widget_increment,
				     widget_value_float,
				     QSlider::Horizontal,
				     vGroupBox);
		m_wmap.insert(widget, slider);
		
		if (!whats_this.isEmpty())
			QWhatsThis::add(vGroupBox, whats_this);
		
		break;
	case GP_WIDGET_TOGGLE:
		gp_widget_get_value(widget, &widget_value_int);
		
		checkBox = new QCheckBox(widget_label, parent);
		checkBox->setChecked(widget_value_int);
		m_wmap.insert(widget, checkBox);

		if (!whats_this.isEmpty())
			QWhatsThis::add(checkBox, whats_this);

		break;
	case GP_WIDGET_RADIO:
		gp_widget_get_value(widget, &widget_value_string);
	
		buttonGroup = new QVButtonGroup(widget_label, parent);
		for(int i = 0; i < gp_widget_count_choices(widget); ++i) {
			const char *widget_choice;
			gp_widget_get_choice(widget, i, &widget_choice);
			
			new QRadioButton(widget_choice, buttonGroup);
			if(!strcmp(widget_value_string, widget_choice))
				buttonGroup->setButton(i);
		}
		m_wmap.insert(widget, buttonGroup);

		if (!whats_this.isEmpty())
			QWhatsThis::add(buttonGroup, whats_this);

		break;
	case GP_WIDGET_MENU:
		gp_widget_get_value(widget, &widget_value_string);
	
		comboBox = new QComboBox(FALSE, parent);
		comboBox->clear();
		for(int i = 0; i < gp_widget_count_choices(widget); ++i) {
			const char *widget_choice;
			gp_widget_get_choice(widget, i, &widget_choice);

			comboBox->insertItem(widget_choice);
			if(!strcmp(widget_value_string, widget_choice))
				comboBox->setCurrentItem(i);
		}
		m_wmap.insert(widget, comboBox);

		if (!whats_this.isEmpty())
			QWhatsThis::add(comboBox, whats_this);

		break;
	case GP_WIDGET_BUTTON:
		// TODO
		// I can't see a way of implementing this. Since there is
		// no way of telling which button sent you a signal, we
		// can't map to the appropriate widget->callback
		new QLabel(i18n("Button (not supported by KControl)"), parent);
		break;
	case GP_WIDGET_DATE:
		// TODO
		new QLabel(i18n("Date (not supported by KControl)"), parent);
		break;
	default:
		return;
	}

	// Append all this widgets children
	for(int i = 0; i < gp_widget_count_children(widget); ++i) {
		CameraWidget *widget_child;
		gp_widget_get_child(widget, i, &widget_child);
		appendWidget(newParent, widget_child);
	}
	
	switch (widget_type) {
	case GP_WIDGET_SECTION:
		tabLayout->addItem( new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding) );
		break;
	}
}

void KameraConfigDialog::updateWidgetValue(CameraWidget *widget)
{
	QLineEdit *lineEdit;
	QCheckBox *checkBox;
	QComboBox *comboBox;
	QSlider *slider;
	QVButtonGroup *buttonGroup;
	QRadioButton *radioButton;

	float value_float;
	int value_int;

	CameraWidgetType widget_type;
	gp_widget_get_type(widget, &widget_type);

	switch(widget_type) {
	case GP_WIDGET_WINDOW:
		// nothing to do
		break;
	case GP_WIDGET_SECTION:
		// nothing to do
		break;
	case GP_WIDGET_TEXT:
		lineEdit = (QLineEdit *) m_wmap[widget];
		gp_widget_set_value(widget, (void *)lineEdit->text().local8Bit().data());
		break;
	case GP_WIDGET_RANGE:
		slider = (QSlider *) m_wmap[widget];
		value_float = slider->value();
		gp_widget_set_value(widget, (void *)&value_float);
		break;
	case GP_WIDGET_TOGGLE:
		checkBox = (QCheckBox *) m_wmap[widget];
		value_int = checkBox->isChecked() ? 1 : 0;
		gp_widget_set_value(widget, (void *)&value_int);
		break;
	case GP_WIDGET_RADIO:
		buttonGroup = (QVButtonGroup *) m_wmap[widget];
		gp_widget_set_value(widget, (void *)buttonGroup->selected()->text().local8Bit().data());
		break;
	case GP_WIDGET_MENU:
		comboBox = (QComboBox *) m_wmap[widget];
		gp_widget_set_value(widget, (void *)comboBox->currentText().local8Bit().data());
		break;
	case GP_WIDGET_BUTTON:
		// nothing to do
		break;
	case GP_WIDGET_DATE:
		// not implemented
		break;
	default:
		// nothing to do
		break;
	}
	
	// Copy child widget values
	for(int i = 0; i < gp_widget_count_children(widget); ++i) {
		CameraWidget *widget_child;
		gp_widget_get_child(widget, i, &widget_child);
		updateWidgetValue(widget_child);
	}
}

void KameraConfigDialog::slotOK()
{
	// Copy Qt widget values into CameraWidget hierarchy
	updateWidgetValue(m_widgetRoot);

	// 'ok' dialog
	accept();
}
