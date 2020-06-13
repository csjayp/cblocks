cblock: cblockd
	make -C src/cblock

cblockd: libcblock.so 
	make -C src/cblockd

libcblock.so:
	make -C src/libcblock

install:
	make -C src/libcblock install
	make -C src/cblockd install
	make -C src/cblock install
	cp src/rc/cblockd /usr/local/etc/rc.d

clean:
	make -C src/libcblock clean
	make -C src/cblockd clean
	make -C src/cblock clean
