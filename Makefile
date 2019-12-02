CFLAGS=-Ilmic
LDFLAGS=-lwiringPi -ljsoncpp
CC=g++

PREFIX = /usr/local

thethingsnetwork-send-v1: thethingsnetwork-send-v1.cpp
	cd lmic && $(MAKE)
	$(CC) $(CFLAGS) -o thethingsnetwork-send-v1 thethingsnetwork-send-v1.cpp lmic/*.o $(LDFLAGS)

all: thethingsnetwork-send-v1

.PHONY: clean

clean:
	rm -f *.o thethingsnetwork-send-v1

.PHONY: install

install: thethingsnetwork-send-v1
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	sudo cp $< $(DESTDIR)$(PREFIX)/bin/ttn-obis-logger

.PHONY: uninstall

uninstall:
	sudo rm -f $(DESTDIR)$(PREFIX)/bin/ttn-obis-logger

