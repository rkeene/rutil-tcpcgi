CC = gcc
CFLAGS = -Wall
LDFLAGS =
BINS = tcpcgi tcpcgi.cgi telnetd
TCPCGI_CGI_OBJS = tcpcgid.o cache.o
OS = `uname -s`
PREFIX=$(prefix)
prefix=/usr/local
include rules/incpre.inc

pre:
	@rules/incpre.sh
	@$(MAKE) all_real
	@echo "" > rules/incpre.inc

all: pre

all_real: $(BINS)

tcpcgid: tcpcgid.o cache.o tcpnet.o
	$(CC) $(LDFLAGS) -o $@ $^
tcpcgi: tcpcgi.o tcpnet.o
	$(CC) $(LDFLAGS) -o $@ $^
tcpcgi.cgi: tcpcgi.cgi.o tcpnet.o $(TCPCGI_CGI_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
telnetd: telnetd.o tcpnet.o
	$(CC) $(LDFLAGS) -o $@ $^

tcpcgid.o: tcpcgid.c tcpcgi.h tcpnet.h cache.h tcpcgid.h
tcpnet.o: tcpnet.c tcpnet.h
cache.o: cache.c cache.h
tcpcgi.o: tcpcgi.c tcpcgi.h tcpcgid.h
tcpcgi.cgi.o: tcpcgi.cgi.c tcpcgi.h
telnetd.o: telnetd.c tcpnet.h

.PHONY: clean install
clean:
	rm -f *.o $(BINS) tcpcgid rules/incpre.inc
	echo "" > rules/incpre.inc

install:
	-mkdir -p $(prefix)/bin/
	-mkdir -p $(prefix)/share/doc/tcpcgi/
	-cp USAGE FAQ README $(prefix)/share/doc/tcpcgi/
	cp tcpcgi.cgi tcpcgi $(prefix)/bin/
