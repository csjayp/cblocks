# Releasing the Linux client package

This describes how to cut a new `.deb` of the `cblock` client. The package
contains only `/usr/bin/cblock`, with `libcblock` linked in statically, and
depends only on `libc6`.

The package version comes from `git describe --tags`, so the release is
defined by its tag:

| Commit | Package version |
|---|---|
| Tagged `v0.1.0` | `0.1.0` |
| 9 commits after `v0.1.0` | `0.1.0+9.gb24fd10` |

Snapshot builds sort after the tag they are based on and before the next
release, so `apt` upgrades in the expected order.

## 1. Choose the version

Tags are `vMAJOR.MINOR.PATCH`, for example `v0.1.0`. The new version must be
higher than the last release. List the existing tags with:

```
% git fetch --tags origin
% git tag --sort=-v:refname | head
```

If the release changes the wire protocol, make sure `CBLOCK_PROTO_VERSION` in
`src/include/cblock/libcblock.h` was bumped, and that `cblockd` on the
servers is upgraded along with the client.

## 2. Tag the release

Tag the commit on `master` that is being released. Use an annotated tag,
like the existing `v0.0.0`, so the tag records who made the release and
when:

```
% git checkout master
% git pull origin master
% git tag -a v0.1.0 -m "cblock v0.1.0"
% git push origin v0.1.0
```

If the tag is wrong and has not been published as a release yet, delete it
locally and on GitHub, then tag again:

```
% git tag -d v0.1.0
% git push origin :refs/tags/v0.1.0
```

## 3. Build the package

Build from a fresh clone of the tag, never from a working tree. `git describe`
does not notice uncommitted changes, so a package built from a dirty tree
would carry the release version without matching the tagged source.

Build on the oldest distribution you want to support. A package built
against an older glibc installs on newer releases, but not the reverse.
Ubuntu 22.04 is the current baseline.

### What `make deb` does

`make deb` is the only command that produces a package. It runs two steps:

1. **`make client-only`** builds `libcblock.a` and links it into
   `src/cblock/cblock`. This needs a C compiler, `make`, `flex` and `bison`.
2. **`tools/mkdeb.sh`** packages that binary:
   1. Works out the version from `git describe --tags`. It strips the
      leading `v` and rewrites the dashes, because Debian versions must
      start with a digit and use `-` only for the Debian revision.
   2. Gets the architecture (`amd64`, `arm64`, ...) from
      `dpkg --print-architecture`, so a package is always built for the
      machine it was built on.
   3. Creates a temporary staging directory and installs the binary
      into it as `usr/bin/cblock`, stripped of debug symbols.
   4. Writes `DEBIAN/control`, which holds the package name, version,
      architecture, maintainer, dependencies (`libc6` only) and
      description. The package name, maintainer and description are set
      at the top of `tools/mkdeb.sh`.
   5. Runs `dpkg-deb --build --root-owner-group` on the staging
      directory. `--root-owner-group` makes the files in the package owned
      by root, so the build does not need to run as root.
   6. Leaves `cblock_<version>_<arch>.deb` at the top of the tree and
      removes the staging directory.

Both steps need `git` (for the version) and `dpkg-deb`, which is part of
`dpkg` on every Debian or Ubuntu system. That is why the package has to be
built on Debian or Ubuntu, either directly or in a container. `make clean`
removes any `.deb` files.

### In Docker (from macOS or any host)

Docker gives you a clean Ubuntu 22.04 system on any host. The command below
mounts your checkout read-only at `/src` and an output directory at `/out`.
Inside the container it:

1. Installs the build tools.
2. Tells `git` to trust the mounted repository. It is owned by a different
   user than root, and `git` refuses to read it otherwise.
3. Clones the tag from `/src` into `/b`. This makes a clean checkout of
   exactly the tagged commit; nothing is fetched from the network.
4. Runs `make deb` and copies the package to `/out`.

The container is deleted afterwards (`--rm`), so only the package is left.

From the top of your cblocks checkout:

```
% mkdir -p out
% docker run --rm -v "$PWD":/src:ro -v "$PWD/out":/out ubuntu:22.04 sh -c '
    set -e
    apt-get update
    apt-get install -y build-essential flex bison git
    git config --global --add safe.directory "*"
    git clone --branch v0.1.0 /src /b
    cd /b
    make deb
    cp cblock_*.deb /out/'
```

The package is written to `out/`, for example
`out/cblock_0.1.0_amd64.deb`.

To build for arm64 as well, run the same command with
`--platform linux/arm64` after `docker run`. On an x86 host this uses
emulation and is slow, but it works with Docker Desktop.

### On a Debian or Ubuntu host

Install the build tools, clone the tag into a new directory, and build. The
package is written to the top of the clone, for example
`cblocks/cblock_0.1.0_amd64.deb`.

```
% sudo apt-get install build-essential flex bison git
% git clone --branch v0.1.0 https://github.com/csjayp/cblocks.git
% cd cblocks
% make deb
```

## 4. Check the package

Make sure the version has no `+N.gHASH` suffix. If it does, the build was
not done on the tagged commit.

```
% dpkg-deb -I out/cblock_0.1.0_amd64.deb
% dpkg-deb -c out/cblock_0.1.0_amd64.deb
```

Then install it in a clean container and run it:

```
% docker run --rm -v "$PWD/out":/out ubuntu:22.04 sh -c '
    dpkg -i /out/cblock_0.1.0_amd64.deb && cblock'
```

`cblock` with no arguments prints its usage. For a full check, run a command
against a test daemon, for example over an SSH tunnel:

```
% ssh -fNL 3333:/var/run/cblock.sock user@testhost
% cblock -s 127.0.0.1 -p 3333 instances
```

## 5. Publish

Create a GitHub release for the tag and attach the packages.

With the `gh` CLI:

```
% gh release create v0.1.0 out/cblock_0.1.0_*.deb \
    --title "cblock v0.1.0" --notes "Release notes here"
```

Or on the web: go to **Releases → Draft a new release**, choose the
`v0.1.0` tag, and upload the `.deb` files.

Users install it with:

```
% curl -LO https://github.com/csjayp/cblocks/releases/download/v0.1.0/cblock_0.1.0_amd64.deb
% sudo apt-get install ./cblock_0.1.0_amd64.deb
```
