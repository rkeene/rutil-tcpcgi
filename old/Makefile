CC = gcc
CFLAGS = -Wall
LDFLAGS =
BINS = tcpcgid tcpcgi tcpcgi.cgi telnetd

all: $(BINS)

tcpcgid: tcpcgid.o tcpnet.o cache.o
tcpcgi: tcpcgi.o tcpnet.o
tcpcgi.cgi: tcpcgi.cgi.o tcpnet.o
telnetd: telnetd.o tcpnet.o

tcpcgid.o: tcpcgid.c tcpcgi.h tcpnet.h cache.h
tcpnet.o: tcpnet.c tcpnet.h
cache.o: cache.c cache.h
tcpcgi.o: tcpcgi.c tcpcgi.h
tcpcgi.cgi.o: tcpcgi.cgi.c tcpcgi.h
telnetd.o: telnetd.c tcpnet.h


clean:
	rm -f *.o $(BINS)
