/*
    tcpcgid.h -- Headers and definitions for the server/daemon portion
                 of rutil_tcpcgi.
    Copyright (C) 2003  Roy Keene

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
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

	email: tcpcgi@rkeene.org

*/
#ifndef TCPCGID_H
#define TCPCGID_H

#include "tcpcgi.h"

#ifdef TCPCGID_STANDALONE
#define RET_FROM_MAIN(x) return(x)
#define MAIN_FUNC(x...) main(x)
#else
#define RET_FROM_MAIN(x) exit(x)
#define MAIN_FUNC(x...) tcpcgid_main(x)
#endif

int MAIN_FUNC(void);

#endif
