#!/bin/bash
# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
set -Eeuo pipefail -x && SCRIPTNAME=${0##*/} && die() { [ -z "$*" ] || echo "$SCRIPTNAME: $*" >&2; exit 127 ; }

grep ' Anklang ' ./README.md || die 'failed to find Anklang project'

# Usage: mkrelease.sh [RELEASE_BUILDDIR]
# Make dist tarball and build release assets from it.
BUILDDIR="${1:-/tmp/anklang/}"

# Clear assets and builddir
rm -rf assets $BUILDDIR
mkdir -p $BUILDDIR assets/

# build dist tarball and ChangeLog in assets/
make dist

# Extract release tarball and version
tar xf assets/anklang-*.tar.zst -C $BUILDDIR --strip-components=1

# Copy populated .dlcache/ to speed up builds
test -d .dlcache/ && cp -a --reflink=auto .dlcache/ $BUILDDIR/

# Make production build + pdfs + package assets
( cd $BUILDDIR/
  # Note, ase/ special cases MODE=production INSN=sse as non-native release builds
  make default MODE=production INSN=sse
  make -j`nproc` \
       all assets/pdf
  make check
  misc/mkdeb.sh
  misc/mkAppImage.sh
)

# Fetch release assets and cleanup
cp $BUILDDIR/assets/* ./assets/
test "$BUILDDIR" == /tmp/anklang/ && rm -rf /tmp/anklang/
ls -l assets/*
