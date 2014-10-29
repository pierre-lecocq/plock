PROGNAME=plock

SRC=plock.c
OBJECTS=plock.o

CFLAGS=--pedantic -Wall -fstack-protector -O2
LDFLAGS=-lpthread -lX11 -lXpm

#LDPATH=-I/usr/X11R6/include -L/usr/X11R6/lib
LDPATH=-I/opt/X11/include -L/opt/X11/lib

all: $(OBJECTS)
	gcc $(CFLAGS) plock.c -o plock $(LDPATH) $(LDFLAGS) 

$(OBJECTS): $(SRC)
	gcc $(LDPATH) $(LDFLAGS) -o $(OBJECTS) $(SRC)

clean:
	rm -f $(OBJECTS) $(PROGNAME)

re: clean all
