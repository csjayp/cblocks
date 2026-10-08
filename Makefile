all: cblockd cblock libfsoverride.so cblock_warden

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

cblock_warden: cblockd
	make -C src/cblock_warden

install:
	make -C src/libfsoverride install
	make -C src/cblockd install
	make -C src/cblock install
	make -C src/cblock_warden install
	cp src/rc/cblockd /usr/local/etc/rc.d
	cp src/rc/cblock_warden /usr/local/etc/rc.d
	pw groupshow cblock >/dev/null 2>&1 || pw groupadd cblock

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
	make -C src/cblock_warden clean

test:
	make -C src/cblock_warden test

lint:
	make -C src/cblock_warden lint

forge:
	cd tools && ./genforge.sh
