/*
    tcpcgi.cgi.c -- HTTP/CGI portion of rutil_tcpcgi.
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
 * tcpcgi.cgi.c -- CGI part of reverse-utils::TCP-over-HTTP/CGI:
 *    This simply needs to relay everything receieved to the daemon
 *    and relay responses to the connected client (stdout).
 *    It should convert the read values before passing them along, however.
 *                 -- Roy Keene [300820030944] <tcpcgi@rkeene.org>
 */

#include <sys/time.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>

#include "tcpcgi.h"
#ifndef TCPCGID_STANDALONE
#include "tcpcgid.h"
#endif
#include "tcpnet.h"
#define CACHE_DEBUG
#include "cache.h"

char *tcpcgi_strsep(char **stringp, const char *delim) {
        char *ret = *stringp;
        if (ret == NULL) return(NULL); /* grrr */
        if ((*stringp = strpbrk(*stringp, delim)) != NULL) {
                *((*stringp)++) = '\0';
        }
        return(ret);
}

void *dehexcode(unsigned char *text, int *len) {
	int hexabet_r[]={0,1,2,3,4,5,6,7,8,9,0,0,0,0,0,0,0,10,11,12,13,14,15};
	unsigned char *textcp;
	int hex[3]={0,0,0}, hexval;
	int srcpos,destpos=0;
	int tl=strlen(text);

	textcp=malloc(strlen(text)+1);
	for (srcpos=0; srcpos<tl; srcpos++) {
		if (text[srcpos]=='#') {
			srcpos++;
			hex[0]=toupper(text[srcpos])-'0';
			hex[1]=toupper(text[srcpos+1])-'0';
			if (hex[0]<0 || hex[0]>=(sizeof(hexabet_r)/sizeof(int)) || hex[1]<0 || hex[1]>=(sizeof(hexabet_r)/sizeof(int))) return(NULL);
			hexval=hexabet_r[hex[0]]<<4;
			hexval|=hexabet_r[hex[1]];
			textcp[destpos++]=hexval;
			srcpos++;
		} else if (text[srcpos]=='+') {
			textcp[destpos++]=' ';
		} else {
			textcp[destpos++]=text[srcpos];
		}
	}
	textcp[destpos]='\0';

	*len=destpos;
	return(textcp);
}

int main(int argc, char **argv) {
	unsigned char retbuflenpt;
	unsigned int retbuflen;
	unsigned int qssize, msg_val_len=0, msg_name_len=0;
	char *qs, *qs_s, *cl, *tok_name, *tok_val;
	char *msg_name, *msg_val;
	char retbuf[65535];
	FILE *fp;
	int qslen;
	int fd;
	int expectreply=0;

	printf("Content-type: application/octet-stream\n\n");
	fflush(stdout);

	fd=createconnection_tcp("localhost", TCPCGI_DAEMON_PORT);
	if (fd<0) {
#ifdef TCPCGID_STANDALONE
		system("tcpcgid &");
#else
		tcpcgid_main();
#endif
		sleep(1);
		fd=createconnection_tcp("localhost", TCPCGI_DAEMON_PORT);
	}
	if (fd<0) {
		PERROR("createconnection_tcp");
		printf("stat: ERROR: Could not create socket.\n");
		printf("%c%c", 0, 0);
		return(0);
	}
	fp=fdopen(fd, "r+");
	cl=getenv("CONTENT_LENGTH");

	if (!cl) {
		qs=getenv("QUERY_STRING");
		if (qs) {
			qs_s=qs=strdup(qs);
		} else {
			qs_s=NULL;
			qs="";
		}
	} else {
		PRINTERR("Content-length is %s", cl);
		qslen=atoi(cl);
		if (qslen<=0) {
			printf("stat: ERROR: internal error.\n");
			printf("%c%c", 0, 0);
			return(-1);
		}
		qs_s=qs=malloc(qssize=qslen);
		if (!qs) {
			PERROR("malloc");
			printf("stat: ERROR: internal error.\n");
			printf("%c%c", 0, 0);
			return(-1);
		}
		PRINTERR("calling read(%i, %p, %i)...", STDIN_FILENO, qs, qslen);
		qslen=read(STDIN_FILENO, qs, qslen);
		PRINTERR("Done with read(), got %i.", qslen);
		if (qslen<0) {
			qs[0]='\0';
			qslen=0;
		}
		if (qslen>0) {
			if (qs[qslen-1]=='\n') qs[qslen-1]='\0';
		}
#ifdef PARANOID
		qs=realloc(qs, qslen);
#endif
	}

	while ((tok_name=tcpcgi_strsep(&qs, "&="))!=NULL) {
		tok_val=tcpcgi_strsep(&qs, "&=");
		if (tok_val==NULL) break; 
		msg_name=dehexcode(tok_name, &msg_name_len);
		msg_val=dehexcode(tok_val, &msg_val_len);
		if (msg_name==NULL || msg_val==NULL) {
			printf("stat: ERROR: Unable to decode arguments.\n");
			printf("%c%c", 0, 0);
			return(0);
		}
		fprintf(fp, "%c%c%c", msg_name_len, (msg_val_len>>8)&0xff, msg_val_len&0xff);
		fwrite(msg_name, msg_name_len, 1, fp);
		fwrite(msg_val, msg_val_len, 1, fp);
		expectreply=1;
	}
	fflush(fp);

	if (!expectreply) {
		printf("stat: ERROR: Nothing to do\n");
		printf("%c%c", 0, 0);
		if (qs_s) free(qs_s);
		return(0);
	}

	fgets(retbuf, sizeof(retbuf), fp);
	while (retbuf[strlen(retbuf)-1]<' ') retbuf[strlen(retbuf)-1]='\0';
	printf("stat: %s\n", retbuf);
	fread(&retbuflenpt, sizeof(retbuflenpt), 1, fp);
	retbuflen=retbuflenpt<<8;
	fread(&retbuflenpt, sizeof(retbuflenpt), 1, fp);
	retbuflen|=retbuflenpt;	
	if (retbuflen!=0) {
		retbuflen=fread(retbuf, 1, retbuflen, fp);
	} else {
		retbuf[0]='\0';
	}
	fprintf(stdout, "%c%c", (retbuflen>>8)&0xff, retbuflen&0xff);
	fwrite(retbuf, 1, retbuflen, stdout);

	if (qs_s) free(qs_s);
	fclose(fp);
	return(0);
}
