# The forge

The forge image is the prime image that is required to get all other images
created if you are building from scratch. Following is the tree structure of
the bare minimum utilies that are required. ca-root-nss.crt is a file
which contains a list of root TLS certs. This list came from mozilla's
nss package. These utilies are also statically linked (built from the
FreeBSD rescue system. A dynamically linked variant could also be used provided
the necessary libmap configuration is present to re-map paths to the required
shared objects into /tmp which is where these utilites live.

```
.
|-- bin
|   |-- cp
|   |-- fetch
|   |-- ln
|   |-- mkdir
|   |-- mktemp
|   |-- rm
|   |-- sh
|   `-- tar
|-- etc
|   |-- ca-root-nss.crt
|   |-- protocols
|   |-- resolv.conf
|   `-- services
|-- lib
|   |-- libarchive.so.7
|   |-- libbsdxml.so.4
|   |-- libbz2.so.4
|   |-- libc.so.7
|   |-- libcrypto.so.111
|   |-- libedit.so.7
|   |-- libfetch.so.6
|   |-- liblzma.so.5
|   |-- libncursesw.so.8
|   |-- libssl.so.111
|   |-- libthr.so.3
|   `-- libz.so.6
`-- libexec
    `-- ld-elf.so.1
```
