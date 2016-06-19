CC = gcc
CFLAGS = -Wall -lusb-1.0 -O2 -mtune=native
FILE = usb.c
BIN = usb

all: clean bin

debug: clean
	$(CC) $(FILE) $(CFLAGS) -o $(BIN) -DDEBUG

clean:
	if [ -f $(BIN) ] ; then rm $(BIN) ; fi ;

bin:
	$(CC) $(FILE) $(CFLAGS) -o $(BIN) 
