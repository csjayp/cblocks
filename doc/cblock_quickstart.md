# Get going with Cellblocks!

## Building cblock daemon and client

```
% make
% sudo make install
% make clean
```

First, create the root file system for your cellblock daemon

## Installing

```
% sudo zfs create ssdvol0/cblocks
% sudo zfs create ssdvol0/cblocks/instances
% sudo zfs create ssdvol0/cblocks/images
```

Modify the rc.conf to include the setup:

```
cblockd_enable=YES
cblockd_data_dir="/ssdvol0/cblocks"
cblockd_fs="zfs"
```

Next, start the daemon:

```
% sudo service cblockd start
```

Now that you know where your root directory is, you can install the support scri
pts that are required for cblockd's operation (note: sequencing is important, because cblockd will create the necessary directories for your support scripts).

```
% cd src/shell
% sudo make install DESTDIR=/ssdvol0/cblocks 
```

Create your forge image (note this must be done on the server side):

```
% cd doc/
% mdkdir forge
% sudo ../copy.sh
```
When you look in your current working directory, you will see the base
utilities and libraries needed to facilitate the operations within a
Cblockfile.

```
% tree 
.
├── bin
│   ├── cp
│   ├── fetch
│   ├── ln
│   ├── mkdir
│   ├── mktemp
│   ├── rm
│   ├── sh
│   └── tar
├── lib
│   ├── libarchive.so.7
│   ├── libbsdxml.so.4
│   ├── libbz2.so.4
│   ├── libc.so.7
│   ├── libcrypto.so.111
│   ├── libedit.so.8
│   ├── libfetch.so.6
│   ├── liblzma.so.5
│   ├── libmd.so.6
│   ├── libncursesw.so.9
│   ├── libprivatezstd.so.5
│   ├── libssl.so.111
│   ├── libthr.so.3
│   └── libz.so.6
├── libexec
│   └── ld-elf.so.1
└── libmap.conf

3 directories, 24 files
```

Next, prepare your tar file. This will be used by the daemon service to
to create your first image. With this image, you will be ready to contruct
others:

```
% tar -czvpf ../forge.tgz .
```

Now that your forge image is archived up, you can submit it to the daemon
which will convert it into your first image:

```
% sudo cblockd --zfs --data-directory /ssdvol0/cblocks --create-forge ../forge.tgz
            __ __ __    __            __
.----.-----|  |  |  |--|  .-----.----|  |--.-----.
|  __|  -__|  |  |  _  |  |  _  |  __|    <|__ --|
|____|_____|__|__|_____|__|_____|____|__|__|_____|

version 0.0.0
-- Constructing the base forge image for future forging operations
-- Forge creation returned 0
%
```

Next you can verify your image:

```
% sudo cblock images
IMAGE            TAG                      SIZE CREATED             
forge            latest                 13.36M  2021-05-24 19:23:19
%
```

Now you are ready to start creating cellblocks. As a good first example, I typically
like to create a base FreeBSD image. I generally use the Cblock file included in the
examples called "base", in this directory you will see a single file that will construct
a full freebsd image based on the distfiles:

```
% cd examples/base
% cat Cblockfile

FROM forge:latest

RUN "mkdir -p imgbuild/root"
ENV "SSL_CA_CERT_FILE" = "/etc/ca-root-nss.crt"
ADD https://download.freebsd.org/ftp/releases/amd64/12.1-RELEASE/base.txz imgbuild/root
ROOTPIVOT imgbuild

OSRELEASE 12.1-RELEASE
ENTRYPOINT [ "/bin/tcsh" ]
```

This is a very simple Cblockfile. Lets go through it line by line so we understand what is happening here:

| Step | Command | Description |
|------|---------|-------------|
| 1 | `FROM forge:latest` | Instructs the builder to use the `forge:latest` as the base image |
| 2 | `RUN "mkdir -p imgbuild/root"` | We create a root directory. It should be noted that this is only required if you are going to use the `ROOTPIVOT` command, which allows you to populate a directory (similar to when you do a `make installworld DESTDIR=/foo`. `ROOTPIVOT` instructs the builder to use this directory as the root for the new image. |
| 3 | `"SSL_CA_CERT_FILE" = "/etc/ca-root-nss.crt"`| Sets the environment variable to the path of our CA keys which gets copied over during the forging process. |
| 4 | `ADD https://download.freebsd.org/ftp/releases/amd64/12.1-RELEASE/base.txz imgbuild/root` | Similar to docker, `ADD` will add files from the ocal file system, or https to the target directory. If the file is a compressed archive, it will decompress and extract it to the target directory |
| 5 | `ROOTPIVOT imgbuild` | Instructs the builder to use the `imgbuild` directory for the image when it creates it |
| 6 | `OSRELEASE 12.1-RELEASE` | Set the OS release, this could be useful when running `-STABLE` kernels for example, but want the packages from the last release (among other things). |
| 7 | `ENTRYPOINT [ "/bin/tcsh" ]` | Finally specify the entry point for the container, in this case we are using `tcsh` |

**NOTE**: Anytime you are using the `ROOTPIVOT` function, you will need to make sure to include you `ADD` a `resolv.conf` into the new image root if you wan't to use it as a persistent container.

If you are using the base container to build subsequent containers, the build system will automatically inject the host's resolv.conf into the target container if one is not supplied.

Now lets build it:

```
% sudo cblock build -n freebsd-12.1 .
```
