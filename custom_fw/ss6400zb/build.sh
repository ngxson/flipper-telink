#!/bin/sh
# Build bin/SS6400ZB.bin in the tc32-build docker image (see ../zg226z/Dockerfile).
# custom_fw/ is mounted so the shared SDK in ../zg226z/SDK is visible.
# Always a clean build: the makefile does not track header dependencies.
set -e
cd "$(dirname "$0")/.."
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src/ss6400zb tc32-build sh -c \
	'mkdir -p tools/linux && ln -sfn /opt/tc32/tc32 tools/linux/tc32 && make -s clean "$@" >/dev/null && make -s "$@"' sh "$@"
