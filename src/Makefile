BIN = mathopd
CC = gcc
CFLAGS = -O -Wall
CPPFLAGS = 
LDFLAGS = 
LIBS = -lcrypt
PREFIX = /usr/local
SBINDIR = $(PREFIX)/sbin

OBJS = base64.o cgi.o config.o core.o dump.o imap.o log.o main.o \
	redirect.o request.o util.o
DEPENDS = mathopd.h Makefile

all: $(BIN)
install: $(BIN)
	install -c $(BIN) $(SBINDIR)
$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBS)
$(OBJS): $(DEPENDS)
.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
clean:
	rm -f $(BIN) $(OBJS)
.PHONY: install clean
