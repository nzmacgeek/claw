#!/bin/bash
# Update the project version metadata in the authoritative source files.
# Usage: ./scripts/bump-version.sh X.Y.Z

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

usage() {
    cat <<'EOF'
Usage: ./scripts/bump-version.sh X.Y.Z

Updates:
- configure.ac package version
- include/claw.h CLAW_VERSION_CODE
- include/claw-version.h generated header (if present)

The script also regenerates ./configure when autoconf is available.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ $# -ne 1 ]]; then
    usage >&2
    exit 1
fi

VERSION="$1"
if [[ ! "$VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    echo "error: version must be in X.Y.Z format" >&2
    exit 1
fi

major="${BASH_REMATCH[1]}"
minor="${BASH_REMATCH[2]}"
patch="${BASH_REMATCH[3]}"

for component in "$major" "$minor" "$patch"; do
    if (( component < 0 || component > 255 )); then
        echo "error: each version component must be between 0 and 255" >&2
        exit 1
    fi
done

VERSION_CODE="$(printf '0x%02x%02x%02x' "$major" "$minor" "$patch")"

replace_in_file() {
    local file="$1"
    local search="$2"
    local replace="$3"

    if ! grep -qE "$search" "$file"; then
        echo "error: pattern '$search' not found in $file" >&2
        exit 1
    fi

    sed -E -i "s|$search|$replace|" "$file"
}

cd "$PROJECT_ROOT"

replace_in_file "configure.ac" '^AC_INIT\(\[claw\], \[[^]]+\],' "AC_INIT([claw], [$VERSION],"
replace_in_file "include/claw.h" '^#define CLAW_VERSION_CODE 0x[0-9A-Fa-f]+' "#define CLAW_VERSION_CODE $VERSION_CODE"

if [[ -f "include/claw-version.h" ]]; then
    replace_in_file "include/claw-version.h" '^#define CLAW_VERSION "[^"]+"' "#define CLAW_VERSION \"$VERSION\""
fi

if command -v autoconf >/dev/null 2>&1; then
    autoconf
fi

echo "Updated project version to $VERSION"
echo "Updated CLAW_VERSION_CODE to $VERSION_CODE"