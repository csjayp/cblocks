#!/bin/sh
#
set -e

export PATH="/usr/local/go125/bin:$PATH"
export ASSUME_ALWAYS_YES=YES

pkg install go125
go install github.com/golangci/golangci-lint/cmd/golangci-lint@lates
make
make test
make lint
make install
make clean
