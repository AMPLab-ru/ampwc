
ampwc
=====

The repository contains various pieces for source code for
simple tiling manager, which will never be born.

Running
-------

    # make test

or

    # WAYLAND_DEBUG=1 ./wlserv
    # WAYLAND_DISPLAY=wayland-0 WAYLAND_DEBUG=1 ./wlclient

If you want run compositor as regular user, you should add SUID bit to server binary
    # chown root:root ./wlserv
    # chmod a+xs ./wlserv

Dependencies
------------

* Wayland libs
* libdrm
* libinput
* libudev
* xkbcommon
