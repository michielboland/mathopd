BIN=mathopd
CC=gcc
CFLAGS=-O -pipe -Wall
CPPFLAGS=
LDFLAGS=
LDLIBS=-lcrypt
PREFIX=/usr/local
SBINDIR=$(PREFIX)/sbin

OBJS=	base64.o cgi.o config.o core.o dummy.o dump.o imap.o log.o main.o \
	redirect.o request.o util.o
DEPENDS=mathopd.h $(MAKEFILE)

all: $(BIN)
install: $(BIN)
	install -c $(BIN) $(SBINDIR)
$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)
$(OBJS): $(DEPENDS)
.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
clean:
	rm -f $(BIN) $(OBJS)
