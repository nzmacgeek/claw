#!/bin/sh
# autogen.sh — Bootstrap the autotools build system.
# Run this once after cloning the repository (where configure is not present).
# Requires: autoconf >= 2.69, automake >= 1.13, aclocal.
set -e

echo "Bootstrapping claw build system..."
autoreconf --force --install --verbose
echo ""
echo "Done. Now run one of:"
echo "  ./configure"
echo "  ./configure --with-musl --enable-static-binary"
echo "  ./configure --disable-static-binary --disable-werror CFLAGS='-O0 -g'"
