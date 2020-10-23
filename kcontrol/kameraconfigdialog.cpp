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

#include "kameraconfigdialog.h"

#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QFrame>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QScrollArea>

#include <KLocalizedString>

#include <QLoggingCategory>
Q_DECLARE_LOGGING_CATEGORY(KAMERA_KCONTROL)

KameraConfigDialog::KameraConfigDialog(Camera */*camera*/,
                    CameraWidget *widget,
                    QWidget *parent) :
    QDialog(parent),
    m_widgetRoot(widget)
{
    QDialogButtonBox *buttonBox = new QDialogButtonBox(
            QDialogButtonBox::Ok|QDialogButtonBox::Cancel, this);

    QWidget *mainWidget = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout;
    setLayout(mainLayout);
    mainLayout->addWidget(mainWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    QPushButton *okButton = buttonBox->button(QDialogButtonBox::Ok);
    okButton->setDefault(true);
    okButton->setShortcut(Qt::CTRL | Qt::Key_Return);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &KameraConfigDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &KameraConfigDialog::reject);
    okButton->setDefault(true);
    setModal( true );

    QFrame *main = new QFrame(this);
    mainLayout->addWidget(main);

    // Sets a layout for the frame, which is the parent of the GP_WIDGET_WINDOW
    QVBoxLayout *topLayout = new QVBoxLayout(main);
    topLayout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = 0;

    appendWidget(main, widget);

    connect(okButton, &QPushButton::clicked, this,  &KameraConfigDialog::slotOk);
    mainLayout->addWidget(buttonBox);
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
    const char *widget_value_string = NULL;
    gp_widget_get_type(widget, &widget_type);
    gp_widget_get_label(widget, &widget_label);
    gp_widget_get_info(widget, &widget_info);
    gp_widget_get_name(widget, &widget_name);

    QString whats_this = QString::fromLocal8Bit(widget_info);	// gphoto2 doesn't seem to have any standard for i18n

    // Add this widget to parent
    switch(widget_type) {
    case GP_WIDGET_WINDOW:
        {
            setWindowTitle(QString::fromLocal8Bit(widget_label));
            break;
        }
    case GP_WIDGET_SECTION:
        {
            if (!m_tabWidget) {
                m_tabWidget = new QTabWidget(parent);
                parent->layout()->addWidget(m_tabWidget);
            }
            QWidget *tab = new QWidget;
            // widgets are to be aligned vertically in the tab
            QVBoxLayout *tabLayout = new QVBoxLayout(tab);
            tabLayout->setContentsMargins(0, 0, 0, 0);
            m_tabWidget->addTab(tab, QString::fromLocal8Bit(widget_label));

            // Add scroll area
            QScrollArea *scrollArea = new QScrollArea(tab);
            scrollArea->setWidgetResizable(true);
            scrollArea->setFrameShape(QFrame::NoFrame);
            tabLayout->addWidget(scrollArea);

            // Add a container widget to hold the page
            QWidget *tabContainer = new QWidget(tab);
            // Add a layout for later parent->layout()->... calls
            new QVBoxLayout(tabContainer);

            // Set the container as the widget to be managed by the ScrollArea
            scrollArea->setWidget(tabContainer);
            scrollArea->show();

            newParent = tabContainer;
            break;
        }
    case GP_WIDGET_TEXT:
    case GP_WIDGET_RANGE:
    case GP_WIDGET_TOGGLE:
        {
            // Share the QGridLayout code
            QWidget *grid = new QWidget(parent);
            QGridLayout *gridLayout = new QGridLayout(grid);
            grid->setLayout(gridLayout);
            parent->layout()->addWidget(grid);

            QLabel *label;
            if (widget_type == GP_WIDGET_TEXT)
            {
                gp_widget_get_value(widget, &widget_value_string);

                label = new QLabel(QString::fromLocal8Bit(widget_label)+':', grid);
                QLineEdit *lineEdit = new QLineEdit(widget_value_string, grid);

                gridLayout->addWidget(lineEdit, 0, 1, Qt::AlignRight);
                m_wmap.insert(widget, lineEdit);
            }
            else if (widget_type == GP_WIDGET_RANGE)
            {
                float widget_low;
                float widget_high;
                float widget_increment;
                gp_widget_get_range(widget, &widget_low, &widget_high, &widget_increment);
                gp_widget_get_value(widget, &widget_value_float);

                label = new QLabel(QString::fromLocal8Bit(widget_label)+':', grid);
                QSlider *slider = new QSlider(Qt::Horizontal, grid);

                gridLayout->addWidget(slider, 0, 1, Qt::AlignRight);
                m_wmap.insert(widget, slider);
            }
            else if (widget_type == GP_WIDGET_TOGGLE)
            {
                gp_widget_get_value(widget, &widget_value_int);

                label = new QLabel(QString::fromLocal8Bit(widget_label), grid);
                QCheckBox *checkBox = new QCheckBox(grid);
                checkBox->setChecked(widget_value_int);

                gridLayout->addWidget(checkBox, 0, 1, Qt::AlignRight);
                m_wmap.insert(widget, checkBox);
                break;
            }
            gridLayout->addWidget(label, 0, 0, Qt::AlignLeft);
            break;
        }
    case GP_WIDGET_RADIO:
        {
            gp_widget_get_value(widget, &widget_value_string);

            int count = gp_widget_count_choices(widget);
            // KDE4 code used Q3V/HBoxGroup to specify alignment based on count
            // For fewer than 5 options, align them horizontally
            QBoxLayout *layout;
            if (count < 5) {
                layout = new QHBoxLayout;
            } else {
                layout = new QVBoxLayout;
            }
            QGroupBox *buttonGroup = new QGroupBox(
                        QString::fromLocal8Bit(widget_label), parent);
            parent->layout()->addWidget(buttonGroup);

            for(int i = 0; i < count; ++i) {
                const char *widget_choice;
                gp_widget_get_choice(widget, i, &widget_choice);

                QRadioButton *newestButton = new QRadioButton(widget_choice);
                if(widget_value_string && !strcmp(widget_value_string, widget_choice)) {
                    newestButton->setChecked(true);
                }
                layout->addWidget(newestButton);
            }
            m_wmap.insert(widget, buttonGroup);

            buttonGroup->setLayout(layout);

            if (!whats_this.isEmpty()) {
                buttonGroup->setWhatsThis( whats_this);
            }

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

                comboBox->addItem(widget_choice);
                if(widget_value_string && !strcmp(widget_value_string, widget_choice))
                    comboBox->setCurrentIndex(i);
            }
            m_wmap.insert(widget, comboBox);

            if (!whats_this.isEmpty()) {
                comboBox->setWhatsThis( whats_this);
            }

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

    if (widget_type == GP_WIDGET_SECTION) {
        // Get latest tab
        QWidget *tab = m_tabWidget->widget(m_tabWidget->count()-1);
        QScrollArea *scrollArea =
                dynamic_cast<QScrollArea *>(tab->children().at(1));
        if (scrollArea) {
            QVBoxLayout *vbox_layout =
                dynamic_cast<QVBoxLayout *>(scrollArea->widget()->layout());
            if (vbox_layout) {
                vbox_layout->addStretch();
            }
        }
    }
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
            QGroupBox *buttonGroup = static_cast<QGroupBox *>(m_wmap[widget]);
            for (auto button : buttonGroup->children()) {
                QRadioButton *radButton = static_cast<QRadioButton *>(button);
                if (radButton->isChecked()) {
                    gp_widget_set_value(widget,
                            (void *)radButton->text().toLocal8Bit().data());
                    break;
                }
            }
            break;
        }
    case GP_WIDGET_MENU:
        {
            QComboBox *comboBox = static_cast<QComboBox *>(m_wmap[widget]);
            gp_widget_set_value(widget,
                    (void *)comboBox->currentText().toLocal8Bit().data());

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
