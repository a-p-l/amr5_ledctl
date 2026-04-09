# Makefile for amr5_ledctl (Linux)
#
# Requires: x86_64 Linux, GCC, root at runtime
#
# Build:   make
# Clean:   make clean
# Install: make install   (copies to /usr/local/sbin)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-result
TARGET   = amr5_ledctl
SRCS     = amr5_ledctl.c
PREFIX  ?= /usr/local

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/sbin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/sbin/$(TARGET)
