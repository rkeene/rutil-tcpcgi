/*
    tcpcgi.c -- Client portion of rutil_tcpcgi.
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

/*
 * tcpcgi.c -- Local listener part of reverse-utils::TCP-over-HTTP/CGI:
 *    This needs to do quite a bit of work in the background.
 *
 *    It will create a listening socket, upon connection to that socket
 *    it will send the proper HTTP request to create the socket on the
 *    other end, and wait for an ackowledgement.
 *
 *    Every second it will query the remote machine for all the sessions
 *    that exist and pass the data to the client socket.
 *
 *    When data arrives, it needs to hex-encode it and put it on the socket.
 *               -- Roy Keene [300820031505] <tcpcgi@rkeene.org>    
 */

#include <string.h>
#define _XOPEN_SOURCE_EXTENDED
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <time.h>

#include "tcpcgi.h"
#include "tcpnet.h"

extern char *optarg;
extern int optind, opterr, optopt;


struct sockinfo {
	char *session;
	char *http_host;
	char *http_path;
	int http_port;
	signed int fd;
};

int use_post=1;
int local_only=0;
int poll_time_min=TCPCGI_CLIENT_POLLTIME_MIN;
int poll_time_max=TCPCGI_CLIENT_POLLTIME_MAX;
char *userpass=NULL;
char *proxyhost=NULL;
struct sockinfo socks_info[128]; /* Needs to be num of elements as socks_poll in main() */


char *mimecode(void *databuf, int datalen) {
	unsigned char buffer[3];
	unsigned char *data=databuf;
	char mimeabet[64]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *ret;
	int outlen, retlen;
	int i,x=0;

	outlen=(int) ((((double) datalen)*1.3333333)+0.9);
	retlen=((int) ((((double) datalen)/3.00)+0.9))*4;
	ret=malloc(retlen+1);

	for (i=0; i<datalen; i+=3) {
		buffer[0]=data[i];
		buffer[1]=data[i+1];
		buffer[2]=data[i+2];
		ret[x++]=mimeabet[((buffer[0]&0xfc)>>2)%sizeof(mimeabet)];
		if (x>=outlen) break;
		ret[x++]=mimeabet[(((buffer[0]&0x3)<<4)|((buffer[1]&0xf0)>>4))%sizeof(mimeabet)];
		if (x>=outlen) break;
		ret[x++]=mimeabet[(((buffer[1]&0xf)<<2)|((buffer[2]&0xc0)>>6))%sizeof(mimeabet)];
		if (x>=outlen) break;
		ret[x++]=mimeabet[(buffer[2]&0x3f)%sizeof(mimeabet)];
		if (x>=outlen) break;
	}
	while (x!=retlen) ret[x++]='=';
	ret[x]='\0';
	return(ret);
}

char *hexcode(void *data, int datalen) {
	char hexabet[]="0123456789abcdef";
	unsigned char *data_num=data;
	char *ret;
	int i, destpos=0;

	if (data==NULL || datalen==0) return(strdup(""));

	ret=malloc(datalen*3+1);

	for (i=0; i<datalen; i++) {
		if (data_num[i]==' ') {
			ret[destpos++]='+';
			continue;
		}
#if 1
		/* if it's a nonprintable charectar, or [%?@&=+: ], convert it */
		if (!isgraph(data_num[i]) || data_num[i]==' ' || \
		data_num[i]=='&' || data_num[i]=='=' || \
		data_num[i]==':' || data_num[i]=='@' || \
		data_num[i]=='?' || data_num[i]=='%' || \
		data_num[i]=='+' || data_num[i]=='#') {
#else
		/* If the charectar is not alphanumeric or and not [+./-], convert it to hex. */
		if (!isalnum(data_num[i]) && data_num[i]!='/' && data_num[i]!='.' && data_num[i]!='~' && data_num[i]!='+') {
#endif
			ret[destpos++]='#';
			ret[destpos++]=hexabet[(data_num[i]>>4)&0xf];
			ret[destpos++]=hexabet[data_num[i]&0xf];
			continue;
		}
		/* Otherwise, we just copy it over. */
		ret[destpos++]=data_num[i];
	}
	ret[destpos]='\0';

#ifdef PARANOID
	ret=realloc(ret, strlen(ret)+1);
#endif
	return(ret);
}

void *tcpcgi_getdata(char *http_host, int http_port, char *http_path, char *http_userpass, char *cmd, char *session, char *dest, void *data, int *datalen, signed int *status, int proto_type) {
	FILE *fp;
	char *hex_cmd=NULL, *hex_session=NULL, *hex_data=NULL, *hex_path=NULL, *hex_host=NULL;
	char *host=NULL, *port_str=NULL, *hostport;
	char *http_auth=NULL;
	char http_realhost[1024], *http_realport_tmp;
	char buf[8192], *retbuf=NULL;
	int fd;
	int sink=0;
	int datalen_val;
	int http_authlen=0;
	int http_realport=http_port;
	unsigned int datalen_loc;

	PRINTERR("Entering tcpcgi_getdata()");
	if (http_host==NULL || http_path==NULL || cmd==NULL || session==NULL) {
		PRINTERR("Invalid parameters passed.");
		if (status) *status=-1;
		return(NULL);
	}

	datalen_val=(datalen)?(*datalen):(0);
	/* Need to free the hex_* variables past this point. */
	hex_cmd=hexcode(cmd, strlen(cmd)); /* Guarenteed not NULL */
	hex_session=hexcode(session, strlen(session)); /* Guarenteed not NULL */
	hex_path=hexcode(http_path, strlen(http_path)); /* Guarenteed not NULL */
	hex_data=hexcode(data, datalen_val);
	if (dest) {
		hostport=strdup(dest);
		/* Parse the host:port value into two seperate variables */
		port_str=strchr(hostport, ':');
		if (!port_str) {
			PRINTERR("Unable to parse destination.");
			if (status) *status=-1;
			if (hex_cmd) free(hex_cmd);
			if (hex_session) free(hex_session);
			if (hex_data) free(hex_data);
			if (hex_path) free(hex_path);
			return(NULL);
		}
		*port_str=0;
		port_str++;
		host=hostport;
		hex_host=hexcode(host, strlen(host));
	}

	if (proxyhost) {
		/* This will cause a slow, steady memory leak ... */
		strcpy(http_realhost, proxyhost);
		http_realport_tmp=strchr(http_realhost, ':');
		if (http_realport_tmp) {
			*http_realport_tmp='\0';
			http_realport=atoi(http_realport_tmp+1);
		} else {
			http_realport=http_port;
		}
	} else {
		strcpy(http_realhost, http_host);
		http_realport=http_port;
	}
	PRINTERR("Creating connection to \"%s\" on port %i", http_realhost, http_realport);

	fd=createconnection_tcp(http_realhost, http_realport);

	if (fd<0) {
		PERROR("connect");
		PRINTERR("Couldn't connect!");
		if (status) *status=-1;
		if (hex_cmd) free(hex_cmd);
		if (hex_session) free(hex_session);
		if (hex_data) free(hex_data);
		if (hex_host) free(hex_host);
		if (hex_path) free(hex_path);
		return(NULL);
	}
	fp=fdopen(fd, "r+");
	if (!fp) {
		PERROR("fdopen");
		if (status) *status=-1;
		if (http_auth[0]!='\0') free(http_auth);
		if (hex_cmd) free(hex_cmd);
		if (hex_session) free(hex_session);
		if (hex_data) free(hex_data);
		if (hex_host) free(hex_host);
		if (hex_path) free(hex_path);
		return(NULL);
	}

	if (http_userpass) {
		http_authlen=128+strlen(http_userpass);
		http_auth=malloc(http_authlen);
		snprintf(http_auth, http_authlen, "Authorization: Basic %s\r\n", http_userpass);
	} else {
		http_auth="";
	}

	snprintf(buf, sizeof(buf), "action=%s&id=%s&host=%s&port=%s&data=%s&datalen=%i&ts=%i&proto=%s", hex_cmd, hex_session, hex_host, port_str, hex_data, datalen_val, (unsigned int) time(NULL), (proto_type==SOCK_DGRAM)?"udp":"tcp");


	buf[sizeof(buf)-1]='\0';
	if (use_post) {
		fprintf(fp, "POST %s HTTP/1.0\r\nHost: %s:%i\r\n%sContent-length: %i\r\n\r\n%s\r\n", hex_path, http_host, http_port, http_auth, (int) strlen(buf), buf);
		PRINTERR("POST http://%s:%i%s?[%i]%s HTTP/1.0\n", http_host, http_port, hex_path, (int) strlen(buf), buf);
	} else {
		fprintf(fp, "GET %s:%i?%s HTTP/1.0\r\nHost: %s:%i\r\n%s\r\n", hex_path, http_port, buf, http_host, http_port, http_auth);
		PRINTERR("GET http://%s:%i%s?[%i]%s HTTP/1.0\n", http_host, http_port, hex_path, (int) strlen(buf), buf);
	}
	fflush(fp);

	if (status) *status=0;
	if (datalen) *datalen=0;
	PRINTERR("Scheduling panic alarm.");
	alarm(120); /* In case of emergency, please panic. */
	if (fgets(buf, sizeof(buf), fp)==NULL) {
		if (http_auth[0]!='\0') free(http_auth);
		if (hex_cmd) free(hex_cmd);
		if (hex_session) free(hex_session);
		if (hex_data) free(hex_data);
		if (hex_host) free(hex_host);
		if (hex_path) free(hex_path);
		fclose(fp);
		return(NULL);
	}
	PRINTERR("Unscheduling panic alarm.");
	alarm(0); /* Panic no more. */
	if (strstr(buf," 40")) {
		PRINTERR("File not available error, %s", buf);
		if (http_auth[0]!='\0') free(http_auth);
		if (hex_cmd) free(hex_cmd);
		if (hex_session) free(hex_session);
		if (hex_data) free(hex_data);
		if (hex_host) free(hex_host);
		if (hex_path) free(hex_path);
		fclose(fp);
		return(NULL);
	}
	if (!strstr(buf, " 200 ")) {
		PRINTERR("Invalid response, %s", buf);
		if (http_auth[0]!='\0') free(http_auth);
		if (hex_cmd) free(hex_cmd);
		if (hex_session) free(hex_session);
		if (hex_data) free(hex_data);
		if (hex_host) free(hex_host);
		if (hex_path) free(hex_path);
		sleep(1); /* Primitive backoff */
		fclose(fp);
		return(tcpcgi_getdata(http_host, http_port, http_path, http_userpass, cmd, session, dest, data, datalen, status, SOCK_STREAM));
	}
	PRINTERR("Scheduling panic alarm.");
	alarm(240); /* In case of emergency, please panic. */
	while (1) {
		if (fgets(buf, sizeof(buf), fp)==NULL) break;
		while (buf[strlen(buf)-1]<' ' && buf[0]!='\0') buf[strlen(buf)-1]='\0';
		/* On the first blank line, assume the HTTP headers have ended. */
		if (buf[0]=='\0' && !sink) {
			sink=1; /* Only come here once. */
			if (fgets(buf, sizeof(buf), fp)==NULL) break;
			PRINTERR("statusmsg=%s", buf);
			if (strstr(buf, "stat: FAIL -")!=NULL) {
				PRINTERR("Setting status to -ERR");
				if (status) *status=-1;
			}
			datalen_loc=(fgetc(fp))<<8;
			datalen_loc|=fgetc(fp);
			if (datalen_loc>16384) continue;
			if (datalen_loc) {
				PRINTERR("Now expecting %i bytes of data.", datalen_loc);
				retbuf=malloc(datalen_loc);
				datalen_loc=fread(retbuf, 1, datalen_loc, fp);
				PRINTERR("[%i](omitted)", datalen_loc);
			}
			if (datalen) *datalen=datalen_loc;
			PRINTERR("We got all that we want, let's get outta here.");
			break;
		}
		PRINTERR("buf=%s", buf);
	}
	PRINTERR("Unscheduling panic alarm.");
	alarm(0);

	fclose(fp);
	if (http_auth[0]!='\0') free(http_auth);
	if (hex_cmd) free(hex_cmd);
	if (hex_session) free(hex_session);
	if (hex_data) free(hex_data);
	if (hex_host) free(hex_host);
	if (hex_path) free(hex_path);
	return(retbuf);
}

char *tcpcgi_gensess(void) {
	static char ret[12]={0,0,0,0,0,0,0,0,0,0,0,0};
	signed int i=0;
	char alphabet[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	int j;

	srand(time(NULL)+ret[0]+ret[1]);
	for (i=0; i<sizeof(ret); i++) {
		j=rand()%(sizeof(alphabet)-1);
		if (alphabet[j]=='\0' && i==0) { i--; continue; } /* Don't generate an empty session. */
		ret[i]=alphabet[j];
	}
	ret[sizeof(ret)-1]='\0';
	return(ret);
}

void tcpcgi_closesession(int fd, char *http_host, int http_port, char *http_path, char *session) {
	if (!session || fd<0) return;
	PRINTERR("Terminating session \"%s\".", session);

	tcpcgi_getdata(http_host, http_port, http_path, userpass, "close", session, NULL, NULL, NULL, NULL, 0);
	return;
}

/*
 * Return values here:
 *    -1  (error)
 *    0   (no error, but no data was recvd)
 *    1   (no error, data recv)
 */
int tcpcgi_handledata(char *http_host, int http_port, char *http_path, char *session, int proto_type, char *dest, int fd, void *buf, int datalen) {
	unsigned char *ret;
	unsigned char *lbuf=buf;
	char *cmd;
	int status=0;
	int dataretlen;
	int rv;

	while (datalen>1024) {
		if ((rv=tcpcgi_handledata(http_host, http_port, http_path, session, proto_type, dest, fd, lbuf, 1024))<0) {
			return(rv);
		}
		datalen-=1024;
		lbuf+=1024;
	}

	if (lbuf && datalen!=0) {
		cmd="write";
		dataretlen=datalen;
	} else {
		cmd="read";
		dataretlen=0;
	}

	ret=tcpcgi_getdata(http_host, http_port, http_path, userpass, cmd, session, dest, lbuf, &dataretlen, &status, proto_type);
	PRINTERR("[%i](omitted, again)", dataretlen);
	if (dataretlen>0 && ret && status>=0) {
		write(fd, ret, dataretlen);
		free(ret);
		status=1;
	}

	PRINTERR("Returning %i", status);
	return(status);
}



int handle_background(void) {
	int i;
	char *http_host, *http_path, *session;
	int fd, http_port;
	int ret=0;
	int rv;

	for (i=0; i<(sizeof(socks_info)/sizeof(struct sockinfo)); i++) {
		if (socks_info[i].fd<0) continue;
		http_host=socks_info[i].http_host;
		http_port=socks_info[i].http_port;
		http_path=socks_info[i].http_path;
		session=socks_info[i].session;
		fd=socks_info[i].fd;
		if ((rv=tcpcgi_handledata(http_host, http_port, http_path, session, SOCK_STREAM, NULL, fd, NULL, 0))<0) {
			PRINTERR("[fd=%i] We got an error in the background and we don't know how to handle it!", fd);
			close(fd);
			continue;
		}
		if (rv>0) ret++;
	}
	return(ret);
}

void sighandler(int sig) {
	if (sig==SIGALRM) {
		PRINTERR("Recieved SIGALRM!");
		return;
	}
	PRINTERR("Recieved weird signal %i", sig);
}

int print_help(char *msg) {
	fprintf(stderr, "Usage: tcpcgi -H host -f file -d dest [-P port] [-p port] [-m time]\n");
	fprintf(stderr, "              [-x time] [-t time] [-b amt] [-V proxy] [-U user] [-rigluvh]\n");
	fprintf(stderr, "    Options:\n");
	fprintf(stderr, "         -H host      HTTP Server which has tcpcgi.cgi.\n");
	fprintf(stderr, "         -f file      Full URL-relative on HTTP server to tcpcgi.cgi,\n");
	fprintf(stderr, "                      including tcpcgi.cgi.\n");
	fprintf(stderr, "         -d dest      Destination to for HTTP server to connect to and\n");
	fprintf(stderr, "                      proxy to (must be in the form of host:port).\n");
	fprintf(stderr, "         -P port      Port to connect to HTTP server on.\n");
	fprintf(stderr, "         -p port      Port to listen for connections on locally (%i is\n", TCPCGI_CLIENT_PORT);
	fprintf(stderr, "                      the default).\n");
	fprintf(stderr, "         -m time      Lowest amount of time (in milliseconds) to check for\n");
	fprintf(stderr, "                      data from HTTP server (%i is the default).\n", TCPCGI_CLIENT_POLLTIME_MIN);
	fprintf(stderr, "         -x time      Greatest amount of time (in milliseconds) to check\n");
	fprintf(stderr, "                      for data from HTTP server (%i is the default).\n", TCPCGI_CLIENT_POLLTIME_MAX);
	fprintf(stderr, "         -t time      Specify that the amount of time for polling be fixed\n");
	fprintf(stderr, "                      at this value (in ms).\n");
	fprintf(stderr, "         -b amt       Backoff amount, if less than 5, specifies backoff\n");
	fprintf(stderr, "                      should multiply the backoff time by this number,\n");
	fprintf(stderr, "                      otherwise it's incremented by this amount (2 is the\n"); 
	fprintf(stderr, "                      default).\n");
	fprintf(stderr, "         -V proxy     Specify a host to send all HTTP traffic through.\n");
	fprintf(stderr, "         -U user      Username and password to use for http authentication\n");
	fprintf(stderr, "                      (must be in the form of user:pass).\n");
	fprintf(stderr, "         -r           Specify that the backoff algorithm reset when remote\n");
	fprintf(stderr, "                      activity occurs, instead of decrementing.\n");
	fprintf(stderr, "         -i           Specify that the backoff algorithm should respond to\n");
	fprintf(stderr, "                      local traffic as well as remote.\n");
	fprintf(stderr, "         -g           Toggle between HTTP get and HTTP post (%s is\n", use_post?"POST":"GET");
	fprintf(stderr, "                      the default).\n");
	fprintf(stderr, "         -l           Bind to %s only for listening (%s is the\n", local_only?"ALL":"LOCALHOST", local_only?"LOCAL":"ALL");
	fprintf(stderr, "                      default).\n");
	fprintf(stderr, "         -u           Use UDP instead of TCP.\n");
	fprintf(stderr, "         -v           Print version (%s) and exit.\n", TCPCGI_VERSION);
	fprintf(stderr, "         -h           This help information.\n");
	if (msg) {
		fprintf(stderr, "\ntcpcgi: %s\n", msg);
	}
	return(1);
}

#ifdef CLOSE_POLL
#undef CLOSE_POLL
#endif
#define CLOSE_POLL(idx) { \
	if (idx!=0) { \
		tcpcgi_closesession(socks_poll[idx].fd, http_host, http_port, http_path, socks_info[idx].session); \
		close(socks_poll[idx].fd); \
		socks_poll[idx].fd=-1; \
		socks_info[idx].fd=-1; \
		pollcnt--; \
		pollpos=idx; \
	} \
}
int main(int argc, char **argv) {
	struct pollfd socks_poll[sizeof(socks_info)/sizeof(struct sockinfo)];
	struct sockaddr_in *addr;
	struct sigaction sac;
	signed int pollpos=0;
	signed int status=0;
	signed int ch;
	char buf[8192];
	char *http_host=NULL, *http_path=NULL;
	char *hostport=NULL;
	char udp_sess_wrk[128], *udp_sess, *socks_sess;
	socklen_t addrlen;
	sigset_t s;
	int masterfd, fd, clientfd;
	int pollcnt=0, pollmax;
	int num_events;
	int errcnt=0;
	int i,x, rv;
	int listen_port=TCPCGI_CLIENT_PORT;
	int poll_time=200; /* Start out with a reasonably fast poll_time. */
	int poll_backoff=2;
	int poll_backoff_reset=0, poll_backoff_ireset=0;
	int http_port=80;
	int proto_type=SOCK_STREAM;
#ifndef DEBUG
	pid_t chpid;
#endif

	while ((ch=getopt(argc, argv, "H:f:P:p:d:m:x:t:b:V:U:vgriulh"))!=-1) {
		switch (ch) {
			case 'H':
				http_host=strdup(optarg);
				break;
			case 'f':
				if (optarg[0]!='/') return(print_help("File should begin with a `/\'"));
				http_path=strdup(optarg);
				break;
			case 'P':
				http_port=atoi(optarg);
				if (http_port<=0) return(print_help("Port cannot be <= 0."));
				break;
			case 'p':
				listen_port=atoi(optarg);
				if (listen_port<=0) return(print_help("Port cannot be <= 0."));
				break;
			case 'd':
				hostport=strdup(optarg);
				if (strchr(hostport,':')==NULL) return(print_help("Destination must be in the form of host:port"));
				break;
			case 'm':
				poll_time_min=atoi(optarg);
				if (poll_time_min<=0) return(print_help("Polltime min cannot be <= 0."));
				if (poll_time<poll_time_min) poll_time=poll_time_min;
				break;
			case 'x':
				poll_time_max=atoi(optarg);
				if (poll_time_max<=0) return(print_help("Polltime max cannot be <= 0."));
				if (poll_time>poll_time_max) poll_time=poll_time_max;
				break;
			case 't':
				poll_time=poll_time_max=poll_time_min=atoi(optarg);
				if (poll_time_max<=0) return(print_help("Polltime cannot be <= 0."));
				break;
			case 'b':
				poll_backoff=atoi(optarg);
				if (poll_backoff<=0) return(print_help("Poll backoff cannot be <= 0."));
				break;
			case 'r':
				poll_backoff_reset=!poll_backoff_reset;
				break;
			case 'i':
				poll_backoff_ireset=!poll_backoff_ireset;
				break;
			case 'g':
				use_post=!use_post;
				break;
			case 'v':
				printf("Reverse utilities::TCP-over-HTTP/CGI v%s\n", TCPCGI_VERSION);
				return(0);
				break;
			case 'l':
				local_only=!local_only;
				break;
			case 'u':
				proto_type=(proto_type==SOCK_STREAM)?SOCK_DGRAM:SOCK_STREAM;
				break;
			case 'U':
				userpass=mimecode(optarg, strlen(optarg));
				break;
			case 'V':
				proxyhost=strdup(optarg);
				break;
			case ':':
			case '?':
			case 'h':
				return(print_help(NULL));
				break;
		}
	}

	/* Basic sanity checks for supplied args */
	if (http_host==NULL || http_path==NULL) {
		return(print_help("You must specify the HOST (-H) and FILE (-f) options."));
	}
	if (http_path[0]!='/') {
		return(print_help("File should begin with a `/\'"));
	}
	if (hostport==NULL) {
		return(print_help("You must specify the DEST (-d) option."));
	}
	if (poll_time_max<poll_time_min) {
		return(print_help("Polltime max cannot be less than polltime min."));
	}

	sigemptyset(&s);
	sigaddset(&s, SIGALRM);
	sac.sa_handler=sighandler;
	sac.sa_mask=s;
	sac.sa_flags=0;
	sigaction(SIGALRM, &sac, NULL);

	signal(SIGPIPE, SIG_IGN);
	masterfd=createlisten(listen_port, local_only, proto_type);
	if (masterfd<0) {
		PERROR("createlisten()");
		return(1);
	}

#ifndef DEBUG
	/* We're all out of complaints at this point, and should be ready to go in the background. */
	if ((chpid=fork())<0) { PERROR("fork"); return(1); }
	if (chpid!=0) {
		/* Parent */
		wait(NULL);
		return(0);
	}
	/* Child */
	if ((chpid=fork())<0) { return(1); }
	if (chpid!=0) return(0);
	/* Grand-child */
	setsid();
#endif

	for (i=0; i<(sizeof(socks_poll)/sizeof(struct pollfd)); i++) socks_poll[i].fd=-1;
	for (i=0; i<(sizeof(socks_info)/sizeof(struct sockinfo)); i++) {
		socks_info[i].session=NULL;
		socks_info[i].fd=-1;
	}
	socks_poll[pollpos].fd=masterfd;
	socks_poll[pollpos].events=POLLIN;
	socks_poll[pollpos].revents=0;
	pollpos++;
	pollcnt++;

	addrlen=sizeof(struct sockaddr_in);
	addr=malloc(addrlen);

	while (1) {
		for (i=0; i<(sizeof(socks_poll)/sizeof(struct pollfd)); i++) {
			if (socks_poll[i].fd!=-1) pollmax=(i+1);
		}

		num_events=poll(socks_poll, pollmax, (pollcnt>1)?(poll_time):(-1));
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
		if (num_events==0) {
			/* If we just have the listening socket in our poll group, don't adjust polltime */
			if (pollcnt==1) continue;

			/* Check background activity, and adjust polltime if needed. */
			if (handle_background()==0) {
				/* If nothing is happening in the background, slow down. */
				if (poll_backoff<5) { poll_time*=poll_backoff; } else { poll_time+=poll_backoff; }
				PRINTERR("Lengthening polltime [poll_time=%i], nothing is happening.", poll_time);
			} else {
				/* If we're active in the background, speed up! */
				if (poll_backoff<5) { poll_time/=poll_backoff; } else { poll_time-=poll_backoff; }
				if (poll_backoff_reset) poll_time=poll_time_min;
				PRINTERR("Shortening polltime [poll_time=%i], stuff is happening.", poll_time);
			}
			if (poll_time>poll_time_max) poll_time=poll_time_max;
			if (poll_time<poll_time_min) poll_time=poll_time_min;
			continue;
		}
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
			/*
			   This is special for UDP, since 
			   it will never have a connection
			   we have just guess at this by
			   the existance (or lack of...)
			   a session with a generated name
			*/
			if (proto_type==SOCK_DGRAM) {
				PRINTERR("Incoming UDP ...");
				if (fd!=masterfd) {
					PRINTERR("... not from master?! IGNORING!");
					continue;
				}
				addrlen=sizeof(struct sockaddr_in);
				recvfrom(fd, buf, 5, MSG_PEEK, (struct sockaddr *)addr, &addrlen);
				memcpy(udp_sess_wrk, &addr->sin_port, 2);
				memcpy(udp_sess_wrk+2, &addr->sin_addr.s_addr, sizeof(addr->sin_addr.s_addr));
				udp_sess=hexcode(udp_sess_wrk, 2+sizeof(addr->sin_addr.s_addr));
				PRINTERR("Session name is .. %s", udp_sess);
			}
			if (fd==masterfd && proto_type!=SOCK_DGRAM) {
				/* Handle incoming connection. */
				PRINTERR("New connection...");
				addrlen=sizeof(struct sockaddr_in);
				clientfd=accept(fd, (struct sockaddr *)addr, &addrlen);
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

				/* Create session */
				socks_info[pollpos].session=strdup(tcpcgi_gensess());
				socks_info[pollpos].http_port=http_port;
				socks_info[pollpos].http_host=http_host;
				socks_info[pollpos].http_path=http_path;
				tcpcgi_getdata(http_host, http_port, http_path, userpass, "open", socks_info[pollpos].session, hostport, NULL, NULL, &status, proto_type);
				if (status<0) {
					PRINTERR("Error creating session on remote end.");
					CLOSE_POLL(pollpos);
					continue;
				}

				/* This needs to be the last step, it actually registers the session for updates. */
				socks_info[pollpos].fd=clientfd;


				PRINTERR("... from [idx=%i, fd=%i], pollcnt=%i, pollmax=%i: session=%s", pollpos, clientfd, pollcnt, pollmax, socks_info[pollpos].session);
				pollpos=-1;
				if (num_events==0) break;
				continue;
			}
			/* Handle data from an existing connection. */
			PRINTERR("Data waiting on [idx=%i, fd=%i]...", i, fd);
			x=read(fd, buf, sizeof(buf));
			PRINTERR(".. not anymore, we read %i bytes.", x);
			if (x<0) PERROR("read");
			if (x==0) {
				PRINTERR("EOF from [idx=%i, fd=%i].", i, fd);
				CLOSE_POLL(i);
				continue;
			}
			if (proto_type==SOCK_DGRAM) {
				socks_sess=udp_sess;
			} else {
				socks_sess=socks_info[i].session;
			}

			/* Do the actual transmission */
			if ((rv=tcpcgi_handledata(http_host, http_port, http_path, socks_sess, proto_type, hostport, fd, buf, x))<0) {
				PRINTERR("Unable to put data.");
				CLOSE_POLL(i);
				continue;
			}
			if (poll_backoff_ireset) {
				/* Check background activity, and adjust polltime if needed. */
				if (handle_background()==0) {
					/* If nothing is happening in the background, slow down. */
					if (poll_backoff<5) { poll_time*=poll_backoff; } else { poll_time+=poll_backoff; }
					PRINTERR("Lengthening polltime [poll_time=%i], nothing is happening.", poll_time);
				} else {
					/* If we're active in the background, speed up! */
					if (poll_backoff<5) { poll_time/=poll_backoff; } else { poll_time-=poll_backoff; }
					if (poll_backoff_reset) poll_time=poll_time_min;
					PRINTERR("Shortening polltime [poll_time=%i], stuff is happening.", poll_time);
				}
				if (poll_time>poll_time_max) poll_time=poll_time_max;
				if (poll_time<poll_time_min) poll_time=poll_time_min;
			}
			if (num_events==0) break;
		}
	}
	return(errcnt);
}
