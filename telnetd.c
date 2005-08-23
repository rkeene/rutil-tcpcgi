/*
    telnetd.c -- Small telnet daemon that gives a shell on a port.
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
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "tcpnet.h"

int main(void) {
	int masterfd, clientfd;

	while ((masterfd=createlisten(5011, 1, SOCK_STREAM))<0) { sleep(1); }
	while (1) {
		clientfd=accept(masterfd, NULL, NULL);
		dup2(clientfd, STDOUT_FILENO);
		dup2(clientfd, STDERR_FILENO);
		dup2(clientfd, STDIN_FILENO);
 		system("/usr/sbin/in.telnetd -h -L /bin/bash -n");
	}
	return(0);
}
