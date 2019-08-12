CC = gcc
CFLAGS = -Wall -ggdb -Iinclude -I../common/include -pthread `pkg-config --cflags libdrm xkbcommon`
LDFLAGS = -lm `pkg-config --libs wayland-server libdrm libudev libinput xkbcommon`
TERM = xterm

XDG_PROTO = ../xdg-shell.xml
HDR = include/xdg-shell-server.h
PRE = include/xdg-shell-server.h
OBJ = $(wildcard ../common/build/*.o)

OUT = ../wlserv

include ../common/gener.mk

include/xdg-shell-server.h: $(XDG_PROTO)
	$(call prettify, GEN, $@, \
	    wayland-scanner server-header $< $@)

test: all
	$(call prettify, RUN, $(OUT), \
	    $(TERM) -e "$(OUT); cat >/dev/null")

userclean:
	$(call prettify, RM, $(PRE), \
	    rm -f $(PRE))

.PHONY: test

