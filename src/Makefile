BIN=mathopd
CC=gcc
CFLAGS=-O -Wall
CPPFLAGS=
LDFLAGS=
LDLIBS=

OBJS=	base64.o cgi.o config.o core.o dummy.o dump.o imap.o log.o main.o \
	redirect.o request.o util.o
DEPENDS=mathopd.h Makefile

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)
$(OBJS): $(DEPENDS)
.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@
clean:
	rm -f $(BIN) $(OBJS)
