/*
    SPDX-FileCopyrightText: 2023 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KAMERALIST_H
#define KAMERALIST_H

#include <QObject>

extern "C" {
#include <gphoto2-list.h>
}

class KameraList
{
public:
    KameraList();
    ~KameraList();

    operator CameraList *();

    Q_DISABLE_COPY(KameraList)

private:
    CameraList *m_list;
};

#endif
