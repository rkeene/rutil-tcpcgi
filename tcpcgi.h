/*
    tcpcgi.h -- Common definitions for rutil_tcpcgi.
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
#ifndef TCPCGI_H
#define TCPCGI_H
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

#ifdef EBUG
#define DEBUG EBUG
#endif

#ifndef TCPCGI_DAEMON_PORT
#define TCPCGI_DAEMON_PORT 9114
#endif

#ifndef TCPCGI_CLIENT_PORT
#define TCPCGI_CLIENT_PORT 9111
#endif

#ifndef TCPCGI_CLIENT_POLLTIME_MIN
#define TCPCGI_CLIENT_POLLTIME_MIN 200
#endif

#ifndef TCPCGI_CLIENT_POLLTIME_MAX
#define TCPCGI_CLIENT_POLLTIME_MAX 60000
#endif

#ifndef TCPCGI_DAEMON_MAXERR
#define TCPCGI_DAEMON_MAXERR 10
#endif

#ifdef DEBUG
#define PRINT_LINE fprintf(stderr, "%s:%i:%s(): ", __FILE__, __LINE__, __func__); 
#define PRINTERR(x...) { PRINT_LINE; fprintf(stderr, x); fprintf(stderr, "\n"); fflush(stderr); }
#define PERROR(x) { PRINT_LINE; perror(x); fflush(stderr); }
#define DPERROR(x) PERROR(x)
#define CHECKPOINT PRINTERR("*** CHECKPOINT REACHED ***")
#else
#define PRINT_LINE /**/
#define PRINTERR(x...) /**/
#define PERROR(x) perror(x);
#define DPERROR(x) /**/
#define CHECKPOINT /**/
#endif

#define TCPCGI_VERSION "0.1.17"

#endif
