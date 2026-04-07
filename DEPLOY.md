# Claw Deployment Guide

This guide covers building, packaging, and deploying Claw to BlueyOS.

## Quick Start: Complete Build → Package → Deploy

```bash
./scripts/deploy.sh
```

This orchestrates:
1. **Build** standalone binary (musl + static linking)
2. **Install** to staging sysroot
3. **Package** as dimsim `.dpk`
4. **Generate** deployment manifest

Output artifacts go to `./dist/`

---

## Building Standalone

For direct deployment into a BlueyOS root disk image:

```bash
./scripts/build-standalone.sh
```

This:
- Uses `musl-gcc` for static compatibility
- Enables static binary linking (no external libc dependencies)
- Configures for BlueyOS paths:
  - Binaries: `/sbin/claw`, `/bin/clawctl`
  - Config: `/etc/claw/`
  - Runtime: `/var/lib/claw/`, `/var/log/claw/`, `/run/claw/`

### Build Output

In `_build-blueyos/`:
- `claw` — PID 1 daemon
- `clawctl` — Control tool

Both are static ELF binaries, self-contained and ready to copy.

### Custom Build Directory

```bash
./scripts/build-standalone.sh /custom/build/path
```

### With Staging Installation

To build AND install to a sysroot for packaging:

```bash
STAGING_DIR=/tmp/claw-sysroot ./scripts/build-standalone.sh
```

This produces a complete directory tree ready for packaging:
```
/tmp/claw-sysroot/
├── sbin/claw
├── bin/clawctl
├── etc/claw/
│   ├── claw.conf
│   ├── services.d/
│   └── targets.d/
└── var/
    ├── lib/claw/       (created by install hook)
    └── log/claw/       (created by install hook)
```

---

## Packaging as dimsim

### From Staged Sysroot

```bash
./scripts/package-dimsim.sh /tmp/claw-sysroot ./dist
```

Produces: `./dist/claw-0.1.0-x86_64.dpk`

### What's Inside

The `.dpk` is a `tar.zst` archive with:

```
meta/
├── manifest.json     # Package metadata, file inventory, hashes
└── scripts/
    ├── preinst       # Pre-installation checks
    ├── postinst      # Post-installation setup (creates /var/lib/claw, etc.)
    ├── prerm         # Pre-removal (stops daemon)
    └── postrm        # Post-removal cleanup

payload/
├── sbin/claw         # Binaries
├── bin/clawctl
├── etc/claw/         # Configuration files
└── var/...           # Runtime directory structure
```

### Manifest Details

`manifest.json` includes:
- **name, version, arch** — Package identification
- **depends, recommends, conflicts, provides** — Dependency metadata
- **files** — Complete inventory with:
  - `path` — Absolute path on target system
  - `sha256` — Content hash
  - `size` — File size in bytes
  - `mode` — Unix permissions (e.g., "0755" for executables)

---

## Deployment Methods

### Method 1: dimsim Installation (Recommended)

On a running BlueyOS system with dimsim package manager:

```bash
# Transfer package
scp claw-0.1.0-x86_64.dpk root@blueyos:/tmp/

# Install
ssh root@blueyos dimsim install /tmp/claw-0.1.0-x86_64.dpk

# Verify
ssh root@blueyos clawctl status
```

The dimsim package manager handles:
- Running lifecycle scripts (preinst, postinst, etc.)
- Verifying file checksums
- Managing dependencies
- Uninstallation

### Method 2: Manual Extraction to Target

Extract the package archive directly to a target sysroot:

```bash
# On development machine
mkdir claw-extracted
tar -xzf claw-0.1.0-x86_64.dpk -C claw-extracted

# Copy unpacked files to BlueyOS root
rsync -av claw-extracted/payload/ /path/to/blueyos-rootfs/

# Or if building disk image:
cp -r claw-extracted/payload/* /mnt/blueyos-rootfs/
```

Then create runtime directories:
```bash
mkdir -p /mnt/blueyos-rootfs/{run/claw,var/{lib,log}/claw}
chmod 700 /mnt/blueyos-rootfs/{run/claw,var/{lib,log}/claw}
```

### Method 3: Integration into Disk Image Build

For disk image generation during CI/CD:

```bash
# In your disk image build script
CLAW_DPK="claw-0.1.0-x86_64.dpk"

# Extract to build directory
mkdir -p "$ROOTFS_BUILD_DIR"
tar -xzf "$CLAW_DPK" -C "$ROOTFS_BUILD_DIR"

# Copy files to rootfs
cp -r "$ROOTFS_BUILD_DIR/payload"/* "$BLUEYOS_ROOTFS/"

# Create runtime directories
mkdir -p "$BLUEYOS_ROOTFS"/{run/claw,var/{lib,log}/claw}
chmod 700 "$BLUEYOS_ROOTFS"/{run/claw,var/{lib,log}/claw}

# Optionally run postinst hooks in chroot
chroot "$BLUEYOS_ROOTFS" /meta/scripts/postinst || true
```

---

## Build Configuration

### Standard (Default)

```bash
./configure \
  --with-musl \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var
```

- **Static linking**: All dependencies included in binaries
- **musl**: Minimal C library, ideal for embedded systems
- **Production paths**: Standard Linux FHS layout

### Custom musl Installation

If your musl is in a non-standard location (e.g., part of your BlueyOS build system):

```bash
./configure \
  --with-musl=/path/to/blueyos/sysroot/musl \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var
```

See [MUSL-PATHS.md](MUSL-PATHS.md) for all musl specification methods.

### Platform-Specific Builds

#### x86_64 (Default)

```bash
./configure --with-musl --enable-static-binary
```

#### ARM/ARM64

If cross-compiling:

```bash
export CC=arm-linux-musleabihf-gcc  # or aarch64-linux-musl-gcc
./configure --with-musl --enable-static-binary --host=arm-linux-musl
```

The package name automatically adjusts: `claw-0.1.0-arm.dpk`

---

## Verification

### Verify Static Linking

```bash
file _build-blueyos/claw
# Output: ELF 64-bit LSB executable, x86-64, statically linked, ...

ldd _build-blueyos/claw 2>&1
# Output: "not a dynamic executable" (good!)
```

### Verify Package Contents

```bash
tar -tzf dist/claw-0.1.0-x86_64.dpk | head -20

# View manifest
tar -xzf dist/claw-0.1.0-x86_64.dpk meta/manifest.json -O | jq .
```

### Verify Checksums in Manifest

```bash
tar -xzf dist/claw-0.1.0-x86_64.dpk
sha256sum payload/sbin/claw
# Compare with manifest.json entry for "sbin/claw"
```

---

## Troubleshooting

### Build Fails: musl-gcc Not Found

Install musl development tools:
```bash
# Ubuntu/Debian
sudo apt-get install musl-tools

# Alpine Linux
apk add musl-dev
```

Or set compiler manually:
```bash
CC=musl-gcc ./configure --with-musl
```

### Build Fails: Autotools Issues

Regenerate configure script:
```bash
./autogen.sh
```

### Package Creation Fails: Missing Commands

Install dependencies:
```bash
# For zstd compression
sudo apt-get install zstandard

# For JSON processing (in packaging script)
sudo apt-get install jq

# For path utilities
sudo apt-get install coreutils
```

### Static Binary Too Large

Normal for statically-linked init system. Claw (~2-3 MB) is reasonable. If concerned, use `strip`:

```bash
strip _build-blueyos/claw _build-blueyos/clawctl
```

---

## Directory Structure

The build system expects:

```
claw/
├── configure.ac
├── Makefile.am
├── autogen.sh
├── src/
│   ├── claw/main.c
│   ├── clawctl/main.c
│   ├── core/...
│   ├── ipc/...
│   ├── os/...
│   └── util/...
├── include/
│   └── claw/...
├── config/
│   ├── claw.conf
│   ├── services.d/*.yml
│   └── targets.d/*.yml
└── scripts/           <- Deployment scripts
    ├── build-standalone.sh
    ├── package-dimsim.sh
    └── deploy.sh
```

---

## CI/CD Integration

### GitLab CI Example

```yaml
stages:
  - build
  - package
  - deploy

build:claw:
  stage: build
  image: alpine:latest
  before_script:
    - apk add autoconf automake musl-dev zstandard jq
  script:
    - ./scripts/deploy.sh
  artifacts:
    paths:
      - dist/
```

### GitHub Actions Example

```yaml
name: Build Claw Package
on: [push]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y musl-tools zstandard jq
      - name: Build and package
        run: ./scripts/deploy.sh
      - name: Upload artifacts
        uses: actions/upload-artifact@v3
        with:
          name: claw-packages
          path: dist/
```

---

## Next Steps

1. **Test installation** on a BlueyOS system or VM
2. **Verify service startup** after installation
3. **Document system-specific configuration** for BlueyOS targets/services
4. **Automate deployment** in your CI/CD pipeline
5. **Build OS image** with integrated Claw package
