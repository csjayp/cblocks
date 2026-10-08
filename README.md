```
            __ __ __    __            __
.----.-----|  |  |  |--|  .-----.----|  |--.-----.
|  __|  -__|  |  |  _  |  |  _  |  __|    <|__ --|
|____|_____|__|__|_____|__|_____|____|__|__|_____|
```

![dystopian cellblocks](media/cellblocks2.png "dystopian cellblocks")

# Introduction

This project is a lightweight container building and runtime environment built on FreeBSD jails, designed for high performance and a minimal memory footprint. It supports both UFS/unionfs and ZFS storage backends and assigns each container its own PTY, providing a persistent, attachable console for interactive access and debugging. The system is implemented in C, Shell, and Go, combining low-level efficiency with scriptable flexibility.

The container build system features a domain-specific language with a syntax closely resembling Dockerfiles, making it intuitive for users familiar with container workflows while remaining tightly integrated with the FreeBSD ecosystem. At start up, container orchestration is handled by `cblock_warden`, which allows you to declaratively specify containers to launch at startup, define port mappings, volumes, and network modes. Networking leverages FreeBSD’s native mechanisms, supporting both bridge and NAT modes through the PF firewall, and includes support for OS auditing and mtree-based snapshots for file integrity monitoring (FIM).

For deep observability, the environment also publishes cell block–specific DTrace providers, allowing administrators to trace and troubleshoot container lifecycle events and performance characteristics in real time. Future development will focus on OCI compliance, extending interoperability with existing container ecosystems while preserving the performance, security, and simplicity inherent to the FreeBSD jail model.

## Prequisites

In addition to the C compilers, the Go toolchain is required for building components of the system written in Go. Certain utilities are also necessary for full functionality: setaudit is used to apply and pin audit configurations within a container, and subcalc is required to configure container networking. All of these tools are available through the FreeBSD Ports Collection, ensuring easy installation and integration into the build environment.

```
% pkg install subcalc setaudit go125
```

## Building cblock daemon and client

```
% git clone https://github.com/csjayp/cblocks.git
% cd cblocks
% make
```

## Building the client only

The `cblock` client can be built on its own, for example on a Linux host or
CI runner that talks to a remote cblock daemon. This builds `libcblock` and
the `cblock` client, but not the daemon, `cblock_warden` or `libfsoverride`.
`libcblock` is linked statically, so only the `cblock` binary is installed.

The client builds on FreeBSD, macOS and Linux (glibc or musl) with no
extra configuration. Besides a C compiler and `make`, the Cblockfile parser
requires `flex` and `bison`. On Debian or Ubuntu:

```
% sudo apt-get install build-essential flex bison
% make client-only
% sudo make client-only-install
```

On Alpine, musl does not provide `<sys/queue.h>`, so also install
`bsd-compat-headers`. `cblock build` also needs GNU tar, since BusyBox tar
does not support the options it uses:

```
% sudo apk add build-base flex bison bsd-compat-headers tar
```

## Installing

First, install the binaries and create the root file system for your cellblock daemon:

```
% sudo make install
% make clean
```

## Configuring

For UFS you can do:

```
% mkdir /usr/cblocks
% mkdir /usr/cblocks/instances
% mkdir /usr/cblocks/images
```

Modify the rc.conf to include the setup (make sure to substitute the ZFS path with your own):

```
cblockd_enable=YES
cblockd_data_dir="/usr/cblocks"
cblockd_fs="ufs"
```

Alternatively for ZFS:

```
% sudo zfs create zroot/cblocks
% sudo zfs create zroot/cblocks/instances
% sudo zfs create zroot/cblocks/images
```

Modify the rc.conf to include the setup (make sure to substitute the ZFS path with your own):

```
cblockd_enable=YES
cblockd_data_dir="/zroot/cblocks"
cblockd_fs="zfs"
```

Next, start the daemon:

```
% sudo service cblockd start
```

Now that you know where your root directory is, you can install the support scripts
that are required for cblockd's operation. cblockd creates the `lib` directory the
scripts are installed into when it starts; if you are installing the scripts before
cblockd has been started, create it first:

```
% sudo mkdir -p /zroot/cblocks/lib
% cd src/shell
% sudo make install DESTDIR=/zroot/cblocks
```

## Permissions

cblockd listens on a UNIX socket, `/var/run/cblock.sock` by default. `make
install` creates a `cblock` group, and the rc script makes the socket readable
and writable by that group (`cblockd_sock_group`, `cblock` by default). To let
a user run `cblock`, add them to the group:

```
% sudo pw groupmod cblock -m alice
```

The user has to log in again for the new group to take effect.

Members of the `cblock` group effectively have root on the host: cblockd runs
as root, and can be asked to launch containers that mount host directories.
Only add users you would give root to.

Set `cblockd_sock_group=""` in rc.conf to allow root only. If you use
`cblockd_flags`, add `--sock-group cblock` to it yourself.

### Remote access over SSH

The client can reach cblockd on another host through ssh, the same way
`DOCKER_HOST=ssh://` works. It runs `ssh` and connects to the socket on the
remote side with `nc -U`, so ssh handles all authentication and nothing is
exposed on the network. The remote user must be in the `cblock` group.

```
% export CBLOCK_HOST=ssh://alice@cblocks.example.com
% cblock instances
```

The host can also be given with `--host`. The full form is
`ssh://[user@]host[:port][/socket/path]`; the socket path defaults to
`/var/run/cblock.sock`. Keys, host keys, jump hosts and other options come
from your ssh configuration.

## UFS Performance Tuning

When using the UFS backend, cblocks uses unionfs to layer container images. Union
vnodes are created lazily on first file access and are subject to normal vnode
recycling when idle, so pressure scales with what containers are actively doing
rather than image size. Each accessed file requires roughly two vnodes: one union
vnode and one UFS vnode for the lower (image) layer. The lower layer vnode is
shared across all containers using the same base image, so for N containers the
cost per accessed file is 1 shared lower vnode plus N union vnodes. Files that are
written to incur a third vnode for the upper (per-instance) layer. During startup
and any operation that walks the filesystem (package installs, find, rsync) union
vnodes accumulate quickly, and running several busy containers against the same base
image can exhaust the default vnode limit more quickly than a non-union workload.

### Checking vnode pressure

```
% sysctl vfs.numvnodes vfs.vnode.param.limit vfs.freevnodes vfs.wantfreevnodes
```

If `vfs.numvnodes` is above 80% of `vfs.vnode.param.limit`, or `vfs.freevnodes`
is consistently below `vfs.wantfreevnodes`, the system is under vnode pressure and
containers may experience degraded lookup performance as the kernel races to recycle
vnodes.

### Calculating a new limit

Each vnode consumes 448 bytes. A reasonable budget is 4-5% of physical RAM:

```
% python3 -c "import os; mem=int(os.popen('sysctl -n hw.physmem').read()); print(int(mem * 0.05 / 448))"
```

### Applying the new limit

To apply immediately:

```
% sysctl kern.maxvnodes=<new_value>
```

To make it permanent, add to `/etc/sysctl.conf`:

```
kern.maxvnodes=<new_value>
```

### Example

On a host with ~4GB RAM the default limit is around 176,000 vnodes. With cblocks
running several containers backed by UFS/unionfs, a value of 350,000 is more
appropriate and costs roughly 75MB of additional kernel memory.

## Setting up networking

Networks are plain FreeBSD interfaces configured by the administrator in
`rc.conf(5)` and `pf.conf(5)`. Containers are attached to them by interface
name with `cblock launch --network <interface>`. The interface type selects
the networking mode:

* A bridge: the container gets its own VNET network stack attached to the
  bridge with an epair. Address configuration (e.g. DHCP) is up to the
  container.
* A loopback with an address: NAT mode. The address and prefix define the
  subnet containers are allocated from, each container address is added as an
  alias on the loopback, and the jail is bound to it.

Example `/etc/rc.conf`:

```
cloned_interfaces="bridge0 lo1"

# Bridged network "l2net" on re0
ifconfig_bridge0_name="l2net"
ifconfig_l2net="addm re0 up"

# NAT network "natnet", containers get addresses from 10.0.0.0/24
ifconfig_lo1_name="natnet"
ifconfig_natnet="inet 10.0.0.1/24"
ifconfig_natnet_descr="re0"

gateway_enable="YES"
pf_enable="YES"
```

NAT networks require PF. Add an outbound NAT rule for each NAT network and the
anchor cblocks uses to load port mappings to `/etc/pf.conf`:

```
nat on re0 from (natnet:network) to any -> (re0:0)
rdr-anchor "cblock-rdr/*"
```

Use the parenthesized `(natnet:network)` form so the ruleset still loads if
the interface does not exist yet.

The `:0` in `(re0:0)` makes PF use only the interface's primary address. With
a plain `(re0)`, PF rotates between all of the interface's addresses, so on
hosts with more than one (common on cloud VMs, e.g. a public address plus a
private VPC address) some outbound connections leave with the private
address and never get a reply.

These are translation rules, so they must come before any filter rules
(`block`, `pass`, `match`) in `/etc/pf.conf`. If they are appended after
them, PF rejects the whole file with "Rules must be in order", nothing in it
is loaded, and port mappings silently do not work. Check the file with
`pfctl -nf /etc/pf.conf`, which should print nothing, then load it with
`pfctl -f /etc/pf.conf`. `pfctl -s nat` should list both rules.

Port mappings marked public are redirected from the interface named in the
loopback's description (`ifconfig_natnet_descr`), or from the interface
holding the default route if no description is set. Non-public port mappings
are only reachable from the host via localhost.

```
% sudo cblock launch --name nginx --network natnet --port 443:443:public
% sudo cblock launch --name nginx --network l2net
```

NOTE: IMPORTANT: If you have your bridged network bound to your external interface, you will be exposed
to attack from the internet and will probably want to configure a firewall!

## Creating the base Forge image

The forge image provides the toolchain that enables all operations defined within a Cblockfile. It serves as the base (layer 0) image, which must be present before building any other cellblocks. This image is created on the server using the following steps:

```
% sudo make forge
```
The command above does a bunch of operations but it basically reads various libraries and
utilities and organizes them into an image the has the following hierarchy, then boostraps
or loads the image into your cblock daemon.

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

Next you can verify your image:

```
% sudo cblock images
IMAGE            TAG                      SIZE CREATED             
forge            latest                 13.36M  2021-05-24 19:23:19
%
```

### Creating base FreeBSD image

With the environment set up, the next step is to begin creating cellblocks. A good starting point is to build a base FreeBSD image. The included example, named "base", provides a sample Cblockfile located in the examples directory. This file defines the instructions needed to construct a complete FreeBSD image from the standard distribution files.

```
% cd ../../examples/base/
% cat Cblockfile

FROM forge:latest

RUN "mkdir -p imgbuild/root"
ENV "SSL_CA_CERT_FILE" = "/etc/ca-root-nss.crt"
ADD https://download.freebsd.org/ftp/releases/amd64/12.2-RELEASE/base.txz imgbuild/root
ROOTPIVOT imgbuild

OSRELEASE 12.2-RELEASE
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
| 7 | `ENTRYPOINT [ "/bin/tcsh" ]` | Finally specify the entry point for the cellblock, in this case we are using `tcsh` |

**NOTE**: Anytime you are using the `ROOTPIVOT` function, you will need to make sure to include you `ADD` a `resolv.conf` into the new image root if you wan't to use it as a persistent cellblock.

If you are using the base cellblock to build subsequent cellblocks, the build system will automatically inject the host's resolv.conf into the target container if one is not supplied.

Now lets build it:

```
% sudo cblock build -n freebsd-13_4 .
-- Preparing local build context...
-- Transmitting build context to cblock daemon (2560) bytes...
-- Bootstrapping build stages 1 through 1
-- Executing stage (1/1) : FROM forge:latest
-- Step 1/4 : RUN mkdir -p imgbuild/root
-- Step 2/4 : ENV SSL_CA_CERT_FILE=/etc/ca-root-nss.crt
-- Step 3/4 : ADD https://download.freebsd.org/ftp/releases/amd64/12.2-RELEASE/base.txz imgbuild/root
-- Step 4/4 : ROOTPIVOT imgbuild
-- Build Stage(s) complete. Writing container image...
-- Cleaning up ephemeral images and build artifacts
-- build occured in 150 seconds: status code 0
%
```
That now you should have a base image, you can view your images by typing:
```
% sudo cblock images
IMAGE            TAG                      SIZE CREATED
freebsd-13_4     latest                975.58M  2021-05-27 00:36:20
forge            latest                 10.17M  2021-05-27 00:32:08
%
```

To remove an image, give its name and tag (the tag defaults to `latest`):
```
% sudo cblock images --remove freebsd-13_4:latest
Removed freebsd-13_4:latest
%
```
If the image has other tags, only this tag is removed. The image itself is
removed with its last tag, and cblockd refuses to remove an image that a
running cellblock or build is using.

### Launching your Cellblock

Now we are ready to launch the container. Note with `--host-networking` the cblock daemon
will lookup the source address associated with your default outbound interface, and use
that. So take care if you cblock daemon is directory connected to the internet.

```
% sudo cblock launch --name freebsd-13_4 --host-networking 
cellblock: container launched: instance: 7d19953ce1
root@7d19953ce1:/ # id
uid=0(root) gid=0(wheel) groups=0(wheel),5(operator)
root@7d19953ce1:/ # 
```

### Detaching from your Cellblock

Each cellblock instance has a TTY attached to it. WHen you launch your cellblock you will be
connected to the console by default. If you want to launch them asynchronously, you can use
the `--no-attach` option on launch. If you want to dis-connect from the console and return to
doing other things, simply press `ctrl+q` and you will be dropped back to the shell. Your
cellblock will continue running. Should you want to re-connect to it, grab the instance ID
and use the console sub command:

```
% sudo cblock instances
INSTANCE    IMAGE           TTY          PID     TYPE                UP
a5ec8053ea  freebsd-13_4    /dev/pts/2   2374    assembled         276s
% sudo cblock console --name a5ec8053ea
root@a5ec8053ea:/ # 
```

### Volumes

A volume mounts a host file system into a cellblock when it launches:

```
--volume TYPE:HOST:CONTAINER:ro|rw
```

* `TYPE` is a file system type for `mount -t`: `nullfs` for a host
  directory, or `ufs` for a device such as a zvol.
* `HOST` is the host directory or device.
* `CONTAINER` is where it appears inside the cellblock. It is created if it
  does not exist. It may not contain `..`, or pass through a symlink in the
  image that points outside the cellblock.
* `ro` mounts it read-only, `rw` read-write.

Paths may not contain `:` or `,`. `--tmpfs`, `--procfs` and `--fdescfs`
mount an in-memory `/tmp`, `/proc` and `/dev/fd`.

```
% sudo cblock launch --name nginx --network natnet \
    --volume nullfs:/storage/www:/usr/local/www:ro
```

In the `cblock_warden` manifest:

```yaml
cellblocks:
  - image: nginx
    network: natnet
    volumes:
      - type: nullfs
        origin: /storage/www
        mountpoint: /usr/local/www
        perms: ro
```

Anyone who can launch a cellblock can mount any host path into it. This is
one reason access to cblockd is root-equivalent (see Permissions).

### Secrets

Keep secrets such as keys and passwords out of images and build contexts:
anything in an image is readable by everyone who can launch it. Instead,
keep them in a host directory and give each cellblock its own, mounted
read-only:

```
% sudo install -d -m 0700 /usr/local/cblocks/secrets
% sudo install -d -m 0750 -g 80 /usr/local/cblocks/secrets/nginx
% sudo install -m 0400 -o 80 -g 80 tls.key /usr/local/cblocks/secrets/nginx/
% sudo cblock launch --name nginx --network natnet \
    --volume nullfs:/usr/local/cblocks/secrets/nginx:/run/secrets:ro
```

* The top directory keeps users on the host out. Inside the cellblock,
  the per-cellblock directory appears as `/run/secrets` with its own
  owner and mode, so give its group to the service.
* Cellblocks share user and group IDs with the host, so use the numeric
  IDs the service runs as inside the image (80 is `www` on FreeBSD).
* Point the service at the file, for example `ssl_certificate_key
  /run/secrets/tls.key;` for nginx. Many programs also accept a
  `*_FILE` setting naming a file to read a password from. Avoid
  environment variables for secrets: `ps -e` shows them, and every child
  process inherits them.
* nullfs shows the host directory as it is now, so a secret replaced on
  the host is visible in the running cellblock straight away. The
  service may need a restart or reload to read it again.
* The files are on the host's disk. To keep them in memory only, mount a
  tmpfs on the secrets directory at boot.

Secrets from HashiCorp Vault or OpenBao work the same way: run Vault Agent
(or OpenBao Agent) on the host, have its templates write each cellblock's
secrets into that cellblock's directory, and mount it as above. The agent
handles authentication, renewal and rotation, and nothing in the
cellblock needs a Vault token.

Secrets are not available during `cblock build`. Do not put them in the
build directory: the whole build context is copied to the cblockd host.
