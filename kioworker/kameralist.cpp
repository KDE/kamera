/*
    SPDX-FileCopyrightText: 2023 Nicolas Fella <nicolas.fella@gmx.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kameralist.h"

KameraList::KameraList()
{
    gp_list_new(&m_list);
}

KameraList::~KameraList()
{
    gp_list_free(m_list);
}

KameraList::operator CameraList *()
{
    return m_list;
};
