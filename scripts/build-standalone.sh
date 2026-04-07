#!/bin/bash
# Build Claw as a standalone, statically-linked binary for BlueyOS
# Usage: ./scripts/build-standalone.sh [BUILD_DIR]
#
# This script:
# - Configures for musl-gcc with static linking
# - Builds the claw daemon and clawctl control tool
# - Optionally installs to a staging sysroot (for packaging)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${1:-./_build-blueyos}"
STAGING_DIR="${STAGING_DIR:-}"

# --- Colors for output ---
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*" >&2
}

# --- Verify prerequisites ---
if ! command -v musl-gcc &> /dev/null; then
    log_error "musl-gcc not found. Install musl-tools or set CC=musl-gcc"
    exit 1
fi

if ! command -v autoreconf &> /dev/null; then
    log_error "autoreconf not found. Install autoconf and automake"
    exit 1
fi

log_info "Building Claw for BlueyOS (musl, static)"
log_info "Project root: $PROJECT_ROOT"
log_info "Build directory: $BUILD_DIR"

# --- Generate autotools files if needed ---
if [[ ! -f "$PROJECT_ROOT/configure" ]]; then
    log_info "Running autogen.sh..."
    cd "$PROJECT_ROOT"
    ./autogen.sh
    cd - > /dev/null
fi

# --- Create build directory ---
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# --- Configure for BlueyOS (musl + static, production paths) ---
log_info "Configuring with musl-gcc..."
"$PROJECT_ROOT/configure" \
    --with-musl \
    --enable-static-binary \
    --disable-werror \
    --prefix= \
    --sbindir=/sbin \
    --bindir=/bin \
    --sysconfdir=/etc \
    --localstatedir=/var \
    CC=musl-gcc

# --- Build ---
log_info "Building..."
make -j"$(nproc)"

# --- Install to staging directory if requested ---
if [[ -n "$STAGING_DIR" ]]; then
    log_info "Installing to staging directory: $STAGING_DIR"
    make install DESTDIR="$STAGING_DIR"
    log_info "✓ Build complete and staged to: $STAGING_DIR"
    log_info "Staged binaries:"
    ls -lh "$STAGING_DIR/sbin/claw" "$STAGING_DIR/bin/clawctl" 2>/dev/null || true
    log_info "Staged configs in: $STAGING_DIR/etc/claw/"
    find "$STAGING_DIR/etc/claw" -type f 2>/dev/null | head -5 || true
else
    log_info "✓ Build complete"
    log_info "Binaries:"
    ls -lh claw clawctl 2>/dev/null || true
    log_info "To install to a sysroot, re-run with STAGING_DIR set:"
    log_info "  STAGING_DIR=/tmp/claw-sysroot ./scripts/build-standalone.sh"
fi

cd - > /dev/null
