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
#include <qlayout.h>
#include <q3grid.h>
#include <qlabel.h>
#include <Q3GroupBox>
#include <qcheckbox.h>
#include <qradiobutton.h>
#include <qlineedit.h>
#include <qcombobox.h>
#include <qslider.h>
#include <Q3ButtonGroup>

#include <kvbox.h>
#include <qtabwidget.h>

//Added by qt3to4:
#include <QFrame>
#include <QVBoxLayout>

#include <klocale.h>

#include "kameraconfigdialog.h"
#include "kameraconfigdialog.moc"

KameraConfigDialog::KameraConfigDialog(Camera */*camera*/,
					CameraWidget *widget,
					QWidget *parent,
					const char *name) :
    KDialog(parent),
    m_widgetRoot(widget)
{
    setButtons( Ok|Cancel );
    setDefaultButton( Ok );
    setModal( true );

    QFrame *main = new QFrame( this );
    setMainWidget( main );
    QVBoxLayout *topLayout = new QVBoxLayout(main);
    topLayout->setSpacing(spacingHint());
    topLayout->setMargin(0);

    m_tabWidget = 0;

    appendWidget(main, widget);
    connect(this,SIGNAL(okClicked()),this,SLOT(slotOk()));
}

void KameraConfigDialog::appendWidget(QWidget *parent, CameraWidget *widget)
{
	QWidget *newParent = parent;

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
		{
			setCaption(widget_label);

			break;
		}
	case GP_WIDGET_SECTION:
		{
			if (!m_tabWidget) {
				m_tabWidget = new QTabWidget(parent);
				parent->layout()->addWidget(m_tabWidget);
			}
			QWidget *tab = new QWidget(m_tabWidget);
			// widgets are to be aligned vertically in the tab
			QVBoxLayout *tabLayout = new QVBoxLayout(tab, marginHint(),
				spacingHint());
			m_tabWidget->insertTab(tab, widget_label);
			KVBox *tabContainer = new KVBox(tab);
			tabContainer->setSpacing(spacingHint());
			tabLayout->addWidget(tabContainer);
			newParent = tabContainer;

			tabLayout->addStretch();

			break;
		}
	case GP_WIDGET_TEXT:
		{
			gp_widget_get_value(widget, &widget_value_string);

			Q3Grid *grid = new Q3Grid(2, Qt::Horizontal, parent);
			parent->layout()->addWidget(grid);
			grid->setSpacing(spacingHint());
			new QLabel(QString::fromLocal8Bit( widget_label )+':', grid);
			QLineEdit *lineEdit = new QLineEdit(widget_value_string, grid);
			m_wmap.insert(widget, lineEdit);

			if (!whats_this.isEmpty())
				grid->setWhatsThis( whats_this);

			break;
		}
	case GP_WIDGET_RANGE:
		{
			float widget_low;
			float widget_high;
			float widget_increment;
			gp_widget_get_range(widget, &widget_low, &widget_high, &widget_increment);
			gp_widget_get_value(widget, &widget_value_float);

			Q3GroupBox *groupBox = new Q3GroupBox(1, Qt::Horizontal,widget_label, parent);
			parent->layout()->addWidget(groupBox);
			QSlider *slider = new QSlider(
				( int )widget_low,
				( int )widget_high,
				( int )widget_increment,
				( int )widget_value_float,
				Qt::Horizontal,
				groupBox );
			m_wmap.insert(widget, slider);

			if (!whats_this.isEmpty())
				groupBox->setWhatsThis( whats_this);

			break;
		}
	case GP_WIDGET_TOGGLE:
		{
			gp_widget_get_value(widget, &widget_value_int);

			QCheckBox *checkBox = new QCheckBox(widget_label, parent);
			parent->layout()->addWidget(checkBox);
			checkBox->setChecked(widget_value_int);
			m_wmap.insert(widget, checkBox);

			if (!whats_this.isEmpty())
				checkBox->setWhatsThis( whats_this);

			break;
		}
	case GP_WIDGET_RADIO:
		{
			gp_widget_get_value(widget, &widget_value_string);

			int count = gp_widget_count_choices(widget);

			// for less than 5 options, align them horizontally
			Q3ButtonGroup *buttonGroup;
			if (count > 4)
				buttonGroup = new Q3VButtonGroup(widget_label, parent);
			else
				buttonGroup = new Q3HButtonGroup(widget_label, parent);
			parent->layout()->addWidget(buttonGroup);

			for(int i = 0; i < count; ++i) {
				const char *widget_choice;
				gp_widget_get_choice(widget, i, &widget_choice);

				new QRadioButton(widget_choice, buttonGroup);
				if(!strcmp(widget_value_string, widget_choice))
					buttonGroup->setButton(i);
			}
			m_wmap.insert(widget, buttonGroup);

			if (!whats_this.isEmpty())
				buttonGroup->setWhatsThis( whats_this);

			break;
		}
	case GP_WIDGET_MENU:
		{
			gp_widget_get_value(widget, &widget_value_string);

			QComboBox *comboBox = new QComboBox(parent);
			parent->layout()->addWidget(comboBox);
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
				comboBox->setWhatsThis( whats_this);

			break;
		}
	case GP_WIDGET_BUTTON:
		{
			// TODO
			// I can't see a way of implementing this. Since there is
			// no way of telling which button sent you a signal, we
			// can't map to the appropriate widget->callback
			QLabel *label = new QLabel(i18n("Button (not supported by KControl)"), parent);
			parent->layout()->addWidget(label);

			break;
		}
	case GP_WIDGET_DATE:
		{
			// TODO
			QLabel * label = new QLabel(i18n("Date (not supported by KControl)"), parent);
			parent->layout()->addWidget(label);

			break;
		}
	default:
		return;
	}

	// Append all this widgets children
	for(int i = 0; i < gp_widget_count_children(widget); ++i) {
		CameraWidget *widget_child;
		gp_widget_get_child(widget, i, &widget_child);
		appendWidget(newParent, widget_child);
	}

	// Things that must be done after all children were added
/*
	switch (widget_type) {
	case GP_WIDGET_SECTION:
		{
			tabLayout->addItem( new QSpacerItem(0, 0, QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding) );
			break;
		}
	}
*/
}

void KameraConfigDialog::updateWidgetValue(CameraWidget *widget)
{
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
		{
			QLineEdit *lineEdit = static_cast<QLineEdit *>(m_wmap[widget]);
			gp_widget_set_value(widget, (void *)lineEdit->text().toLocal8Bit().data());

			break;
		}
	case GP_WIDGET_RANGE:
		{
			QSlider *slider = static_cast<QSlider *>(m_wmap[widget]);
			float value_float = slider->value();
			gp_widget_set_value(widget, (void *)&value_float);

			break;
		}
	case GP_WIDGET_TOGGLE:
		{
			QCheckBox *checkBox = static_cast<QCheckBox *>(m_wmap[widget]);
			int value_int = checkBox->isChecked() ? 1 : 0;
			gp_widget_set_value(widget, (void *)&value_int);

			break;
		}
	case GP_WIDGET_RADIO:
		{
			Q3ButtonGroup *buttonGroup = static_cast<Q3VButtonGroup *>(m_wmap[widget]);
			gp_widget_set_value(widget, (void *)buttonGroup->selected()->text().toLocal8Bit().data());

			break;
		}
	case GP_WIDGET_MENU:
		{
			QComboBox *comboBox = static_cast<QComboBox *>(m_wmap[widget]);
			gp_widget_set_value(widget, (void *)comboBox->currentText().toLocal8Bit().data());

			break;
		}
	case GP_WIDGET_BUTTON:
		// nothing to do
		break;
	case GP_WIDGET_DATE:
		{
			// not implemented
			break;
		}
	}

	// Copy child widget values
	for(int i = 0; i < gp_widget_count_children(widget); ++i) {
		CameraWidget *widget_child;
		gp_widget_get_child(widget, i, &widget_child);
		updateWidgetValue(widget_child);
	}
}

void KameraConfigDialog::slotOk()
{
	// Copy Qt widget values into CameraWidget hierarchy
	updateWidgetValue(m_widgetRoot);

	// 'ok' dialog
	accept();
}
