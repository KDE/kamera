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
#ifndef __kameraconfigdialog_h__
#define __kameraconfigdialog_h__

#include <QMap>
#include <QDialog>
#include <QTabWidget>

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
