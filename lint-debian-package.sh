#!/bin/bash

# Exit if a command fails
set -e

unset DEB_BUILD_OPTIONS

make dist

SRCDIR=$(pwd)
WORKDIR=$(mktemp -d)
trap "rm -rf $WORKDIR" EXIT

cd "$WORKDIR"

mv "$SRCDIR"/cantera-term-32.tar.gz cantera-term_32.orig.tar.gz
tar zxpvf cantera-term_32.orig.tar.gz
cd cantera-term-32
cp -va "$SRCDIR"/debian .
dpkg-buildpackage -us -uc -rfakeroot
cd ..
lintian --suppress-tags new-package-should-close-itp-bug *.changes
