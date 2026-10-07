#!/bin/sh
#
# Build a Debian package of the cblock client from src/cblock/cblock.
# Run from the top of the tree; "make deb" builds the client first.
#
set -e

name=cblock
maintainer="Christian S.J. Peron <csjp@freebsd.org>"

# Debian versions must start with a digit and may only use '-' to
# separate the Debian revision, so v0.0.0-9-gb24fd10 becomes
# 0.0.0+9.gb24fd10.
version=$(git describe --tags | sed -e 's/^v//' -e 's/-/+/' -e 's/-/./')
arch=$(dpkg --print-architecture)

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

mkdir -p "$stage/DEBIAN" "$stage/usr/bin"
install -s -m 0755 src/cblock/cblock "$stage/usr/bin/cblock"
cat > "$stage/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: $maintainer
Depends: libc6
Section: admin
Priority: optional
Homepage: https://github.com/csjayp/cblocks
Description: client for the cblocks container daemon
 cblock builds container images and launches, attaches to and manages
 containers on a cblockd host. cblockd runs containers in FreeBSD jails.
EOF
chmod 0755 "$stage"
dpkg-deb --build --root-owner-group "$stage" "${name}_${version}_${arch}.deb"
