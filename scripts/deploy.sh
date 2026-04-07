#!/bin/bash
# Claw deployment workflow: Build → Package → Deploy
#
# This script orchestrates the complete process:
# 1. Build standalone binary (musl + static)
# 2. Install to staging sysroot
# 3. Package as dimsim .dpk
# 4. Generate checksums and deployment info

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

BUILD_DIR="${1:-./_build-standalone}"
SYSROOT_DIR="${2:-./_sysroot-claw}"
OUTPUT_DIR="${3:-./dist}"

# --- Colors ---
readonly GREEN='\033[0;32m'
readonly BLUE='\033[0;34m'
readonly NC='\033[0m'

log_step() {
    echo -e "\n${BLUE}==>${NC} $*"
}

cd "$PROJECT_ROOT"

# --- Step 1: Build ---
log_step "Building Claw standalone (musl, static)..."
STAGING_DIR="$SYSROOT_DIR" "$SCRIPT_DIR/build-standalone.sh" "$BUILD_DIR"

# --- Step 2: Package ---
log_step "Creating dimsim package..."
mkdir -p "$OUTPUT_DIR"
"$SCRIPT_DIR/package-dimsim.sh" "$SYSROOT_DIR" "$OUTPUT_DIR"

# --- Step 3: Generate checksums and manifest ---
log_step "Generating deployment manifest..."

MANIFEST_FILE="$OUTPUT_DIR/MANIFEST.txt"
cat > "$MANIFEST_FILE" << EOF
# Claw Deployment Manifest
# Generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)

## Package Contents
EOF

find "$OUTPUT_DIR" -maxdepth 1 -type f \( -name "*.dpk" -o -name "*.asc" \) | while read -r f; do
    sha256=$(sha256sum "$f" | cut -d' ' -f1)
    size=$(du -h "$f" | cut -f1)
    echo "  $(basename "$f"): $sha256 ($size)" >> "$MANIFEST_FILE"
done

cat >> "$MANIFEST_FILE" << EOF

## Installation Instructions with dimsim

1. Transfer package to BlueyOS system:
   \$ scp claw-*.dpk root@blueyos:/tmp/

2. Install using dimsim:
   \$ dimsim install /tmp/claw-*.dpk

3. Verify installation:
   \$ clawctl status
   \$ clawctl list-units

## Manual Installation (out-of-tree build to sysroot)

If you need to customize paths or build for a different architecture:

1. Configure with custom paths:
   \$ mkdir _build-custom && cd _build-custom
   \$ ../configure --with-musl \\
       --enable-static-binary \\
       --prefix=/custom/prefix \\
       --sbindir=/custom/sbin \\
       --sysconfdir=/custom/etc

2. Build and install to a staging directory:
   \$ make -j\$(nproc)
   \$ make install DESTDIR=/path/to/staging

3. Package the sysroot:
   \$ ./scripts/package-dimsim.sh /path/to/staging

## Integration into BlueyOS Disk Image Build

For inclusion during rootfs generation:

1. Extract package to a temporary sysroot:
   \$ mkdir rootfs-temp
   \$ tar -xzf claw-*.dpk -C rootfs-temp

2. Copy to your disk image build tree:
   \$ cp -r rootfs-temp/* /path/to/blueyos-rootfs/

3. Ensure critical runtime directories exist:
   \$ mkdir -p /path/to/blueyos-rootfs/{run/claw,var/{lib,log}/claw}

## Build Environment Requirements

- musl-gcc (musl C library toolchain)
- autoconf >= 2.69
- automake >= 1.16.5
- zstd (for tar.zst compression)
- jq (for JSON processing in packaging)

EOF

log_step "Deployment manifest:"
cat "$MANIFEST_FILE"

echo -e "\n${GREEN}✓ Deployment complete!${GREEN}"
echo "  Artifacts in: $OUTPUT_DIR"
echo "  Manifest: $MANIFEST_FILE"
