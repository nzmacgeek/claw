#!/bin/bash
# Create a dimsim .dpk package from a Claw sysroot
# Usage: ./scripts/package-dimsim.sh SYSROOT_DIR [OUTPUT_DIR]
#
# This script:
# - Verifies the sysroot contains required Claw files
# - Generates manifest.json with file inventory and checksums
# - Creates lifecycle scripts (stubs for now)
# - Packages as tar.zst with meta/ and payload/ structure

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

SYSROOT_DIR="${1:-.}"
OUTPUT_DIR="${2:-.}"

# --- Colors ---
readonly RED='\033[0;31m'
readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }

# --- Verify prerequisites ---
if ! command -v zstd &> /dev/null; then
    log_error "zstd not found. Install zstandard compression"
    exit 1
fi

if [[ ! -d "$SYSROOT_DIR" ]]; then
    log_error "Sysroot directory not found: $SYSROOT_DIR"
    exit 1
fi

# --- Verify Claw files in sysroot ---
CLAW_BIN="$SYSROOT_DIR/sbin/claw"
CLAWCTL_BIN="$SYSROOT_DIR/bin/clawctl"
CLAW_CONF_DIR="$SYSROOT_DIR/etc/claw"

if [[ ! -f "$CLAW_BIN" ]] || [[ ! -f "$CLAWCTL_BIN" ]]; then
    log_error "Required binaries not found in sysroot"
    log_error "  Missing: $CLAW_BIN or $CLAWCTL_BIN"
    exit 1
fi

if [[ ! -d "$CLAW_CONF_DIR" ]]; then
    log_error "Configuration directory not found: $CLAW_CONF_DIR"
    exit 1
fi

log_info "Packaging Claw from sysroot: $SYSROOT_DIR"

# --- Create temporary working directory ---
WORK_DIR=$(mktemp -d)
trap "rm -rf '$WORK_DIR'" EXIT

META_DIR="$WORK_DIR/meta"
PAYLOAD_DIR="$WORK_DIR/payload"
mkdir -p "$META_DIR/scripts" "$PAYLOAD_DIR"

# --- Copy payload (preserve directory structure relative to /) ---
log_info "Copying files to payload..."
(cd "$SYSROOT_DIR" && tar -c --exclude=./dev --exclude=./proc --exclude=./sys .) | \
    (cd "$PAYLOAD_DIR" && tar -xf -)

# --- Generate manifest.json ---
log_info "Generating manifest.json..."

# Package metadata
PKG_NAME="claw"
PKG_VERSION="0.1.0"
PKG_ARCH="x86_64"  # TODO: detect from musl-gcc
PKG_DESC="Claw init system daemon and control tool for BlueyOS"
PKG_MAINTAINER="BlueyOS"
PKG_HOMEPAGE="https://github.com/nzmacgeek/claw"

# Generate files array: each entry has path, sha256, size, mode
FILES_JSON="["

first=true
while IFS= read -r -d '' filepath; do
    # Make path relative to payload root
    relpath="${filepath#$PAYLOAD_DIR}"
    relpath="${relpath#/}"

    # Skip certain paths
    [[ "$relpath" =~ ^(var/lib/claw|var/log/claw)/?$ ]] && continue

    if [[ -L "$filepath" ]]; then
        # Skip symlinks for now
        continue
    fi

    if [[ -f "$filepath" ]]; then
        sha256=$(sha256sum "$filepath" | cut -d' ' -f1)
        size=$(stat -f%z "$filepath" 2>/dev/null || stat -c%s "$filepath")
        perms=$(stat -f%A "$filepath" 2>/dev/null || stat -c%a "$filepath")

        if [[ "$first" == true ]]; then
            first=false
        else
            FILES_JSON+=","
        fi

        FILES_JSON+="{\"path\":\"/$relpath\",\"sha256\":\"$sha256\",\"size\":$size,\"mode\":\"0$perms\"}"
    fi
done < <(find "$PAYLOAD_DIR" -type f -print0)

FILES_JSON+="]"

# Build manifest
MANIFEST="{
  \"name\": \"$PKG_NAME\",
  \"version\": \"$PKG_VERSION\",
  \"arch\": \"$PKG_ARCH\",
  \"description\": \"$PKG_DESC\",
  \"maintainer\": \"$PKG_MAINTAINER\",
  \"homepage\": \"$PKG_HOMEPAGE\",
  \"depends\": [],
  \"recommends\": [],
  \"conflicts\": [],
  \"provides\": [\"claw\", \"clawctl\"],
  \"scripts\": [\"preinst\", \"postinst\", \"prerm\", \"postrm\"],
  \"files\": $(echo "$FILES_JSON" | jq -c .)
}"

echo "$MANIFEST" | jq . > "$META_DIR/manifest.json"
log_info "✓ manifest.json created ($(jq '.files | length' "$META_DIR/manifest.json") files)"

# --- Generate lifecycle scripts ---
log_info "Creating lifecycle scripts..."

# preinst: pre-installation checks
cat > "$META_DIR/scripts/preinst" << 'EOF'
#!/bin/bash
# Pre-installation hook: verify system state
set -e

# Check if running as root
if [[ $EUID -ne 0 ]]; then
    echo "Claw must be installed as root" >&2
    exit 1
fi

# Warn if claw is already running
if pgrep -x "claw" > /dev/null 2>&1; then
    echo "WARNING: claw daemon already running. Package may require system restart." >&2
fi

echo "Claw pre-installation checks passed"
exit 0
EOF
chmod 755 "$META_DIR/scripts/preinst"

# postinst: post-installation setup
cat > "$META_DIR/scripts/postinst" << 'EOF'
#!/bin/bash
# Post-installation hook: setup runtime directories and permissions
set -e

# Create runtime directories if they don't exist
mkdir -p /var/lib/claw /var/log/claw /run/claw

# Set permissions
chmod 700 /var/lib/claw /var/log/claw /run/claw

echo "Claw installed successfully"
echo "To start the daemon: systemctl start claw (or use clawctl)"
exit 0
EOF
chmod 755 "$META_DIR/scripts/postinst"

# prerm: pre-removal
cat > "$META_DIR/scripts/prerm" << 'EOF'
#!/bin/bash
# Pre-removal hook: stop claw gracefully
set -e

if command -v clawctl &> /dev/null; then
    echo "Stopping claw daemon..."
    clawctl stop-all 2>/dev/null || pkill claw || true
    sleep 1
fi

echo "Claw pre-removal complete"
exit 0
EOF
chmod 755 "$META_DIR/scripts/prerm"

# postrm: post-removal
cat > "$META_DIR/scripts/postrm" << 'EOF'
#!/bin/bash
# Post-removal hook: cleanup
set -e

# Don't remove /var/lib/claw, /var/log/claw — user might want to keep state
echo "Claw removed"
exit 0
EOF
chmod 755 "$META_DIR/scripts/postrm"

# --- Package as tar.zst ---
log_info "Creating .dpk archive..."

PKG_FILENAME="${PKG_NAME}-${PKG_VERSION}-${PKG_ARCH}.dpk"
PKG_PATH="$OUTPUT_DIR/$PKG_FILENAME"

mkdir -p "$OUTPUT_DIR"

(cd "$WORK_DIR" && tar -cf - meta payload) | zstd -19 -o "$PKG_PATH"

log_info "✓ Package created: $PKG_PATH"
log_info "  Size: $(du -h "$PKG_PATH" | cut -f1)"
log_info "  Format: tar.zst (meta/ + payload/ structure)"

# --- Verification ---
log_info "Verifying package contents..."
tar -tzf "$PKG_PATH" | head -20 | sed 's/^/  /'
echo "  ..."

log_info "Package ready for installation with dimsim"
exit 0
