#
# Makefile for plock
#

# Define sources, objects and program name
SRC=plock.c
OBJECTS=plock.o
PROGNAME=plock

# Compilation options
CFLAGS=--pedantic -Wall -fstack-protector -O2
LDFLAGS=-lpthread -lX11 -lXpm -lcrypt
LDPATH=-I/usr/X11R6/include -L/usr/X11R6/lib

# That's all
all: $(OBJECTS)
	gcc $(CFLAGS) $(SRC) -o $(PROGNAME) $(LDPATH) $(LDFLAGS)
	@chmod u+s $(PROGNAME) # Sticky bit for shadow functions

# Build objects
$(OBJECTS): $(SRC)
	gcc $(LDPATH) $(LDFLAGS) -o $(OBJECTS) $(SRC)

# Housework
clean:
	rm -f $(OBJECTS) $(PROGNAME)

# Can you repeat ?
re: clean all
