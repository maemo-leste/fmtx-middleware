all: fmtx-object-bindings.h fmtxd fmtx_client

fmtxd: fmtx-object.c main.c audio.c dbus.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs libcal dbus-1 \
	glib-2.0 gconf-2.0 libpulse libpulse-mainloop-glib alsa) $^ -o $@

fmtx-object-bindings.h: fmtx-object.xml
	dbus-binding-tool --mode=glib-server --prefix=fmtx_object $< --output=$@

fmtx_client: fmtx_client.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs dbus-glib-1) $^ -o $@
clean:
	$(RM) *.o fmtx-object-bindings.h fmtxd fmtx_client

install:
	install -d "$(DESTDIR)/usr/include/"
	install -d "$(DESTDIR)/usr/bin/"
	install -d "$(DESTDIR)/usr/sbin/"
#	install -m 644 fmtxd.h "$(DESTDIR)/usr/include/"
	install -m 755 fmtxd "$(DESTDIR)/usr/sbin/"
	install -m 755 fmtx_client "$(DESTDIR)/usr/bin/"
