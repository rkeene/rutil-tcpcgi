/*
    tcpcgid.c -- Server/daemon portion of rutil_tcpcgi.
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
#include <string.h>
#define _XOPEN_SOURCE_EXTENDED
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>

#include "tcpcgid.h"
#include "tcpcgi.h"
#include "tcpnet.h"
#define CACHE_DEBUG
#include "cache.h"

#define CLOSE_POLL(idx) { \
	close(socks_poll[idx].fd); \
	tcpcgi_handledata(socks_poll[idx].fd, NULL, 0); \
	socks_poll[idx].fd=-1; \
	pollcnt--; \
	pollpos=idx; \
}


static cache_t *sess=NULL;

struct conninfo {
	cache_t *parms;
	int state;
};

struct sessioninfo {
	char password[32];
	int sockfd;
};

int tcpcgi_errmsg(int fd, char *msg) {
	PRINTERR("[fd=%i]: FAIL: %s", fd, msg);
	write(fd, "FAIL - ", strlen("FAIL - "));
	write(fd, msg, strlen(msg));
	write(fd, "\n", 1);
	return(-1);
}

int tcpcgi_write(int fd, char *msg) {
	PRINTERR("[fd=%i]: SUCCESS: %s", fd, msg);
	write(fd, "SUCCESS: ", strlen("SUCCESS: "));
	write(fd, msg, strlen(msg));
	write(fd, "\n", 1);
	return(0);
}

int tcpcgi_write_data(int fd, void *data, int datalen) {
	unsigned char datalen_chr[2];
	if (data==NULL) {
		write(fd, "\0\0", 2);
		return(0);
	}
	datalen_chr[0]=(datalen>>8)&0xff;
	datalen_chr[1]=(datalen)&0xff;
	write(fd, datalen_chr, 1); 
	write(fd, datalen_chr+1, 1); 
	write(fd, data, datalen);
	return(0);
}

void tcpcgi_free_cacheparms(void *data, void *key) {
	free(data);
	free(key);
}

void tcpcgi_free_conninfo(void *data, void *key) {
	struct conninfo *ci=data;

	if (ci) cache_destroy(ci->parms);
	free(ci);
}

void tcpcgi_free_session(void *data, void *key) {
	struct sessioninfo *si=data;

	if (si) {
		if (si->sockfd>=0) close(si->sockfd);
	}
	free(key);
	free(si);
}


/* Create a session and connect to a host. */
int tcpcgi_cmd_open(int fd, cache_t *parms) {
	struct sessioninfo *si;
	char *host, *port_str;
	char *session;
	int port;
	int sockfd;

	PRINTERR("Starting tcpcgi_cmd_open");
	host=cache_find(parms, "host", strlen("host"), NULL);
	port_str=cache_find(parms, "port", strlen("port"), NULL);
	session=cache_find(parms, "id", strlen("id"), NULL);
	if (!host) return(tcpcgi_errmsg(fd, "Need parameter: host"));
	if (!port_str) return(tcpcgi_errmsg(fd, "Need parameter: port"));
	if (!session) return(tcpcgi_errmsg(fd, "Need parameter: id"));
	si=cache_find(sess, session, strlen(session), NULL);
	if (si) return(tcpcgi_errmsg(fd, "Improper parameter: id;  Session exists"));

	port=atoi(port_str);

	/* Connect to host:port */
	PRINTERR("[%s] Connecting to \"%s\", port %i", session, host, port);
	sockfd=createconnection_tcp(host, port);
	if (sockfd<0) return(tcpcgi_errmsg(fd, "Unable to connect"));

	/* Let 'em know we did something. */
	tcpcgi_write(fd, "Connected.");

	/* Create the session info */
	si=malloc(sizeof(struct sessioninfo));
	si->sockfd=sockfd;
	si->password[0]='\0';

	/* Register the session */
	cache_add(sess, strdup(session), strlen(session), si, sizeof(struct sessioninfo), 0, tcpcgi_free_session);

	/* No data to report, so write an empty one. */
	tcpcgi_write_data(fd, NULL, 0);

	return(0);
}

/* Destroy a session, and close connection to destination. */
int tcpcgi_cmd_close(int fd, cache_t *parms) {
	struct sessioninfo *si;
	char *session;

	PRINTERR("Starting tcpcgi_cmd_close");
	session=cache_find(parms, "id", strlen("id"), NULL);
	if (!session) return(tcpcgi_errmsg(fd, "Need parameter: id"));

	si=cache_find(sess, session, strlen(session), NULL);
	if (!si) return(tcpcgi_errmsg(fd, "Improper parameter: id;  Session does not exists"));

	/* Delete the session (note: this also closes the socket). */
	cache_delete(sess, session, strlen(session), NULL, 1);
	tcpcgi_write(fd, "Session closed.");

	/* No data to report, so write an empty one. */
	tcpcgi_write_data(fd, NULL, 0);

	return(0);
}

int tcpcgi_cmd_get(int fd, cache_t *parms) {
	struct sessioninfo *si;
	char *session;
	char buf[8192];
	int buflen;
	int sockfd;
	int sockflags, oldsockflags;

	PRINTERR("Starting tcpcgi_cmd_get");
	session=cache_find(parms, "id", strlen("id"), NULL);
	if (!session) return(tcpcgi_errmsg(fd, "Need parameter: id"));

	si=cache_find(sess, session, strlen(session), NULL);
	if (!si) return(tcpcgi_errmsg(fd, "Improper parameter: id;  Session does not exists"));
	sockfd=si->sockfd;

	/* Put the socket into non-blocking mode. */
	oldsockflags=fcntl(sockfd, F_GETFL);
	sockflags=oldsockflags|O_NONBLOCK;
	fcntl(sockfd, F_SETFL, sockflags);

	/* Read the data*/
	buflen=read(sockfd, buf, sizeof(buf));
	if (buflen<0) {
		/* No data available, this may not be an error. */
		tcpcgi_write(fd, "No data");
		tcpcgi_write_data(fd, NULL, 0);
		return(0);
	}

	if (buflen==0) {
		/* We lost the peer, destroy the session. */
		cache_delete(sess, session, strlen(session), NULL, 1);
		tcpcgi_write(fd, "EOF reached?");
		tcpcgi_write_data(fd, NULL, 0);
		return(0);
	}

	/* Restore the sock flags */
	fcntl(sockfd, F_SETFL, oldsockflags);

	/* Let the user know we're sending good data. */
	tcpcgi_write(fd, "Data follows");

	/* No data to report, so write an empty one. */
	tcpcgi_write_data(fd, buf, buflen);

	return(0);
}

int tcpcgi_cmd_put(int fd, cache_t *parms) {
	struct sessioninfo *si;
	char *session, *data, *proto, *dest, *dest_addr;
	int dest_port;
	int proto_type;
	int datalen=0;
	int x;

	PRINTERR("Starting tcpcgi_cmd_put");
	proto=cache_find(parms, "proto", strlen("proto"), NULL);
	session=cache_find(parms, "id", strlen("id"), NULL);
	data=cache_find(parms, "data", strlen("data"), &datalen);
	if (!session) return(tcpcgi_errmsg(fd, "Need parameter: id"));
	if (!data) return(tcpcgi_errmsg(fd, "Need parameter: data"));
	if (!proto) proto="tcp";
	proto_type=SOCK_STREAM;
	if (strcmp(proto,"udp")==0) proto_type=SOCK_DGRAM;

	if (proto_type!=SOCK_DGRAM) {
		si=cache_find(sess, session, strlen(session), NULL);
		if (!si) return(tcpcgi_errmsg(fd, "Improper parameter: id;  Session does not exists"));
		while ((x=write(si->sockfd, data, datalen))>0) {
			datalen-=x;
			if (datalen==0) break;
		}
	} else {
		dest=cache_find(parms, "dest", strlen("dest"), NULL);
		if (!dest) return(tcpcgi_errmsg(fd, "Need parameter: dest"));
		if (strchr(dest,':')==NULL) return(tcpcgi_errmsg(fd, "Improper parameter: dest"));
		dest_addr=strdup(dest);
		dest_port=atoi(strchr(dest,':')+1);
		*(strchr(dest_addr,':'))='\0';
		PRINTERR("dest=%s, dest_port=%i, dest_addr=%s", dest, dest_port, dest_addr);
		/* XXX:  WORK ON UDP SUPPORT WAS TEMPORARILY SUSPENDED HERE ON 4Sep03 */
	}


	if (x<0) {
		/* We destroy the session at this point ... */
		cache_delete(sess, session, strlen(session), NULL, 1);
		PERROR("write");
		return(tcpcgi_errmsg(fd, "Could not write to socket."));
	}

	/* If we made it this far, also check for data. */
	return(tcpcgi_cmd_get(fd, parms));
}


void *tcpcgi_cmd_list[][2]={	{"open", tcpcgi_cmd_open},
				{"close", tcpcgi_cmd_close},
				{"write", tcpcgi_cmd_put},
				{"read", tcpcgi_cmd_get},
                           };

int tcpcgi_handledata(int fd, unsigned char *data, unsigned int datalen) {
	static cache_t *c=NULL;
	cache_t *parms;
	struct conninfo *ci;
	unsigned char *msg_name, *msg_val;
	unsigned char *cmd;
	int (*func)(int, cache_t *, cache_t **);
	int cmd_len;
	int msg_val_len, msg_name_len;
	int i;

	if (!c) c=cache_create(11);
	if (!sess) sess=cache_create(11);
	if (!parms) parms=cache_create(5);

	if (datalen==0 && data==NULL) {
		PRINTERR("Cleaning up cache entries for [fd=%i].", fd);
		cache_delete(c, &fd, sizeof(fd), NULL, 1);
		return(0);
	}
	PRINTERR("Got data from [fd=%i], datalen=%i", fd, datalen);

	ci=cache_find(c, &fd, sizeof(fd), NULL);
	if (!ci) {
		ci=malloc(sizeof(struct conninfo));
		ci->parms=cache_create(5);
		ci->state=0; /* XXX */
	}
	cache_add(c, &fd, sizeof(fd), ci, sizeof(struct conninfo), 0, tcpcgi_free_conninfo);
	PRINTERR("Reading in data name+value pairs...");
	/* Read the pairs into ci->parms */
	for (i=0; i<datalen; ) {
		msg_name_len=data[i];
		msg_val_len=(data[i+1]<<8)|(data[i+2]);
		msg_name=malloc(msg_name_len+1);
		msg_val=malloc(msg_val_len+1);
		memcpy(msg_name, data+i+3, msg_name_len);
		memcpy(msg_val, data+i+3+msg_name_len, msg_val_len);
		/*
		 * These two lines allow us to treat the parameters as
		 * null-terminated strings if we wish.
		 */
		msg_name[msg_name_len]='\0';
		msg_val[msg_val_len]='\0';
		PRINTERR("%s = [%i]\"%s\"", msg_name, msg_val_len, msg_val);
		cache_add(ci->parms, msg_name, msg_name_len, msg_val, msg_val_len, 0, tcpcgi_free_cacheparms);

		i+=(msg_name_len+msg_val_len+3);
	}

	/*
	 * Find the `cmd' keyword and determine what function should
	 * handle it, then call that function with 3 arguments:
	 * fd, ci->parms, &sess
	 */
	cmd=cache_find(ci->parms, "action", strlen("action"), &cmd_len);
	for (i=0; i<(sizeof(tcpcgi_cmd_list)/sizeof(void *))/2; i++) {
		if (memcmp(cmd, tcpcgi_cmd_list[i][0], cmd_len)==0) {
			func=tcpcgi_cmd_list[i][1];
			return(func(fd, ci->parms, &sess));
		}
	}
	/* Invalid/unknown command */
	return(tcpcgi_errmsg(fd, "Invalid command"));
	
}

int MAIN_FUNC(void) {
	struct pollfd socks_poll[128];
	struct sockaddr addr;
	char buf[8192];
	socklen_t addrlen;
	int masterfd, fd, clientfd;
	int pollcnt=0, pollpos=0, pollmax;
	int num_events;
	int errcnt=0;
	int i,x;
#if !defined(TCPCGID_STANDALONE) || !defined(DEBUG)
	pid_t chpid;
#endif

#ifdef TCPCGID_STANDALONE
	/* If we're in stand-alone mode, do this before we fork, in case of failure */
	/* Note, this code is duplicated below with the OPPOSITE condition */
	signal(SIGPIPE, SIG_IGN);

	masterfd=createlisten(TCPCGI_DAEMON_PORT, 1, SOCK_STREAM);
	if (masterfd<0) {
		PERROR("createlisten()");
		return(-1);
	}
#endif

#if defined(TCPCGID_STANDALONE) && defined(DEBUG)
	PRINTERR("Not forking.");
#else
	/* We're all out of complaints at this point, and should be ready to go in the background. */
	if ((chpid=fork())<0) { PERROR("fork"); return(1); }
	if (chpid!=0) {
		/* Parent [old process] */
		wait(NULL);
		return(0);
	}
	/* Child [new process] */
	/* Beyond here, we need to exit() instead of return (unless standalone mode) */
	if ((chpid=fork())<0) { RET_FROM_MAIN(1); }
	if (chpid!=0) RET_FROM_MAIN(0);
	/* Grand-child */
	setsid();
#endif

#ifndef TCPCGID_STANDALONE
	/* If we're not in stand-alone mode, do this now (since we didn't do it above). */
	signal(SIGPIPE, SIG_IGN);

	masterfd=createlisten(TCPCGI_DAEMON_PORT, 1, SOCK_STREAM);
	if (masterfd<0) {
		PERROR("createlisten()");
		RET_FROM_MAIN(-1);
	}
#endif

	for (i=0; i<(sizeof(socks_poll)/sizeof(struct pollfd)); i++) socks_poll[i].fd=-1;
	socks_poll[pollpos].fd=masterfd;
	socks_poll[pollpos].events=POLLIN;
	socks_poll[pollpos].revents=0;
	pollpos++;
	pollcnt++;
	while (1) {
		for (i=0; i<(sizeof(socks_poll)/sizeof(struct pollfd)); i++) {
			if (socks_poll[i].fd!=-1) pollmax=(i+1);
		}
		num_events=poll(socks_poll, pollmax, -1);
		if (num_events<0) {
			if (errcnt==TCPCGI_DAEMON_MAXERR) {
				fprintf(stderr, "Maximum error count (%i) reached, aborting...\n", TCPCGI_DAEMON_MAXERR);
				break;
			}
			errcnt++;
			PERROR("poll()");
			continue;
		}
		errcnt=0;
		if (num_events==0) continue;
		for (i=0; i<pollmax; i++) {
			if (socks_poll[i].revents&(POLLERR|POLLHUP|POLLNVAL)) {
				/* Socket is broke, close it. */
				CLOSE_POLL(i);
				PRINTERR("Closing socket [idx=%i, fd=%i], due to errors.", i, fd);
				num_events--;
				if (num_events==0) break;
				continue;
			}
			if ((socks_poll[i].revents&POLLIN)!=POLLIN) continue; 
			num_events--;
			fd=socks_poll[i].fd;
			socks_poll[i].events=POLLIN;
			socks_poll[i].revents=0;
			if (fd==masterfd) {
				/* Handle incoming connection. */
				PRINTERR("New connection...");
				addrlen=sizeof(addr);
				clientfd=accept(fd, &addr, &addrlen);
				if (clientfd<0) {
					PERROR("accept");
					continue;
				}
				if (pollpos==-1) {
					for (x=0; x<pollmax; x++) {
						if (socks_poll[x].fd==-1) {
							pollpos=x;
							break;
						}
					}
				}
				if (pollpos==-1) pollpos=pollmax;
				if (pollpos>=(sizeof(socks_poll)/sizeof(struct pollfd))) {
					PRINTERR("Ran out of available socks_poll[] entries.");
					close(fd);
					continue;
				}
				socks_poll[pollpos].fd=clientfd;
				socks_poll[pollpos].events=POLLIN|POLLERR|POLLHUP|POLLNVAL;
				socks_poll[pollpos].revents=0;
				pollcnt++;
				PRINTERR("... from [idx=%i, fd=%i], pollcnt=%i, pollmax=%i", pollpos, clientfd, pollcnt, pollmax);
				pollpos=-1;
			} else {
				/* Handle data from an existing connection. */
				PRINTERR("Data waiting on [idx=%i, fd=%i]...", i, fd);
				x=read(fd, buf, sizeof(buf));
				if (x<0) PERROR("read");
				if (x==0) {
					PRINTERR("EOF from [idx=%i, fd=%i].", i, fd);
					CLOSE_POLL(i);
					continue;
				}
				if (tcpcgi_handledata(fd, buf, x)<0) {
					/* If we got an error, report no data. */
					tcpcgi_write_data(fd, NULL, 0);
					continue;
				}
			}
			if (num_events==0) break;
		}
	}
	RET_FROM_MAIN(errcnt);
}
