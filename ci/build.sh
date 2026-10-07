#!/bin/sh
#
set -e

export PATH="/usr/local/go125/bin:$PATH"
go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest
make
make test
make lint
make install
make clean
