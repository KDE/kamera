/*
    SPDX-FileCopyrightText: 2001 The Kompany
    SPDX-FileCopyrightText: 2002-2003 Ilya Konstantinov <kde-devel@future.shiny.co.il>
    SPDX-FileCopyrightText: 2002-2003 Marcus Meissner <marcus@jet.franken.de>
    SPDX-FileCopyrightText: 2003 Nadeem Hasan <nhasan@nadmm.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef __kameraconfigdialog_h__
#define __kameraconfigdialog_h__

#include <QMap>
#include <QDialog>
class QTabWidget;

extern "C" {
    #include <gphoto2.h>
}

class KameraConfigDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KameraConfigDialog(Camera *camera, CameraWidget *widget,
               QWidget *parent = nullptr);

private Q_SLOTS:
    void slotOk();

private:
    void appendWidget(QWidget *parent, CameraWidget *widget);
    void updateWidgetValue(CameraWidget *widget);

    QMap<CameraWidget *, QWidget *> m_wmap;
    CameraWidget *m_widgetRoot = nullptr;
    QTabWidget *m_tabWidget = nullptr;
};

#endif
