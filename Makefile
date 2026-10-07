all: cblockd cblock libfsoverride.so warden

cblock: cblockd
	make -C src/cblock

client-only: libcblock
	make -C src/cblock

cblockd: libcblock
	make -C src/cblockd

libfsoverride.so:
	make -C src/libfsoverride

libcblock:
	make -C src/libcblock

warden: cblockd
	make -C src/warden

install:
	make -C src/libfsoverride install
	make -C src/cblockd install
	make -C src/cblock install
	make -C src/warden install
	cp src/rc/cblockd /usr/local/etc/rc.d

client-only-install:
	make -C src/cblock install

deb: client-only
	sh tools/mkdeb.sh

clean:
	rm -f cblock_*.deb
	make -C src/libcblock clean
	make -C src/libfsoverride clean
	make -C src/cblockd clean
	make -C src/cblock clean
	make -C src/warden clean

test:
	make -C src/warden test

lint:
	make -C src/warden lint

forge:
	cd tools && ./genforge.sh
