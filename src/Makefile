BIN=mathopd
OBJS=main.o config.o core.o request.o imap.o cgi.o dump.o
DEPENDS=mathopd.h Makefile

CC=gcc
CFLAGS=-O -Wall
CPPFLAGS=
LDFLAGS=
LDLIBS=
RM=rm -f

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)
$(OBJS): $(DEPENDS)
.c.o:
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $<
clean:
	$(RM) $(BIN) $(OBJS)
