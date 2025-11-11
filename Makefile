all: cblockd cblock warden

cblock: cblockd
	make -C src/cblock

cblockd: libcblock.so
	make -C src/cblockd

libcblock.so:
	make -C src/libcblock

warden: cblockd
	make -C src/warden

install:
	make -C src/libcblock install
	make -C src/cblockd install
	make -C src/cblock install
	make -C src/warden install
	cp src/rc/cblockd /usr/local/etc/rc.d

clean:
	make -C src/libcblock clean
	make -C src/cblockd clean
	make -C src/cblock clean
	make -C src/warden clean

test:
	make -C src/warden test

forge:
	cd tools && ./genforge.sh
