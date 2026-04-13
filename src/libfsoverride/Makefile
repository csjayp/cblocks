CC ?= clang
PREFIX ?= /usr/local

OVERRIDES=unionfs_override.c

libfsoverride.so:
	$(CC) -Wall -fPIC -shared -o $@ $(OVERRIDES) -ldl

install: libfsoverride.so
	[ -d $(PREFIX)/lib ] || mkdir -p $(PREFIX)/lib
	install -m 0555 libfsoverride.so $(PREFIX)/lib/libfsoverride.so

clean:
	rm -fr *.so
