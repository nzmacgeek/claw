#!/bin/bash
# contrib/install-hooks.sh — post-install metadata generator
#
# Called after 'make install' to produce a checksum manifest and a
# deployment metadata file inside the installation tree.
#
# Usage:
#   contrib/install-hooks.sh <DESTDIR> <VERSION>
#
# Example (invoked by the Makefile install-manifest target):
#   contrib/install-hooks.sh /tmp/claw-sysroot 0.1.0
#
# Outputs (relative to DESTDIR):
#   MANIFEST.md5   — MD5 checksum : relative-path pairs for every installed file
#   METADATA.txt   — human-readable deployment record

set -euo pipefail

DESTDIR="${1:-.}"
VERSION="${2:-unknown}"

# Normalize: strip any trailing slash so path stripping is consistent
DESTDIR="${DESTDIR%/}"

if [[ ! -d "$DESTDIR" ]]; then
    echo "error: DESTDIR '$DESTDIR' does not exist" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# MANIFEST.md5
# ---------------------------------------------------------------------------
MANIFEST="$DESTDIR/MANIFEST.md5"
echo "Generating $MANIFEST ..."

find "$DESTDIR" -type f \
    ! -name "MANIFEST.md5" \
    ! -name "METADATA.txt" \
    | sort \
    | while read -r file; do
        relpath="${file#"$DESTDIR"}"
        relpath="${relpath#/}"   # strip leading slash -> true relative path
        md5sum "$file" | awk -v path="$relpath" '{print $1 "  " path}'
    done > "$MANIFEST"

echo "  $(wc -l < "$MANIFEST") files recorded."

# ---------------------------------------------------------------------------
# METADATA.txt
# ---------------------------------------------------------------------------
METADATA="$DESTDIR/METADATA.txt"
echo "Generating $METADATA ..."

cat > "$METADATA" <<EOF
Claw Init System — Deployment Record
======================================
Version:    $VERSION
Installed:  $(date -u +%Y-%m-%dT%H:%M:%SZ)
Hostname:   $(hostname 2>/dev/null || echo unknown)
User:       $(whoami 2>/dev/null || echo unknown)

Files:      $(find "$DESTDIR" -type f ! -name "MANIFEST.md5" ! -name "METADATA.txt" | wc -l)
Dirs:       $(find "$DESTDIR" -type d | wc -l)
EOF

echo "  Done."
echo ""
echo "Deployment metadata written to:"
echo "  $MANIFEST"
echo "  $METADATA"
