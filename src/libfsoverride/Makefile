CC ?= clang

OVERRIDES=unionfs_override.c

libfsoverride.so:
	$(CC) -Wall -fPIC -shared -o $@ $(OVERRIDES) -ldl

clean:
	rm -fr *.so
