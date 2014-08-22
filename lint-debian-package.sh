#!/bin/bash

# Exit if a command fails
set -e

unset DEB_BUILD_OPTIONS

make dist

SRCDIR=$(pwd)
WORKDIR=$(mktemp -d)
trap "rm -rf $WORKDIR" EXIT

cd "$WORKDIR"

mv "$SRCDIR"/cantera-term-31.tar.gz cantera-term_31.orig.tar.gz
tar zxpvf cantera-term_31.orig.tar.gz
cd cantera-term-31
cp -va "$SRCDIR"/debian .
dpkg-buildpackage -us -uc -rfakeroot
cd ..
lintian --suppress-tags new-package-should-close-itp-bug *.changes
