# Claw Deployment Scripts

Quick reference for building, packaging, and deploying Claw to BlueyOS.

## Scripts Overview

### `build-standalone.sh`
**Build static, musl-based binaries**

```bash
./scripts/build-standalone.sh [BUILD_DIR]
```

- Configures with musl-gcc and static linking
- Uses production paths (`/sbin/claw`, `/etc/claw`, etc.)
- Optional staging installation: `STAGING_DIR=/path ./scripts/build-standalone.sh`

### `package-dimsim.sh`
**Create a dimsim .dpk package**

```bash
./scripts/package-dimsim.sh SYSROOT_DIR [OUTPUT_DIR]
```

- Takes a staged sysroot (from build-standalone.sh)
- Generates `manifest.json` with file inventory and SHA256 hashes
- Creates lifecycle scripts (preinst, postinst, prerm, postrm)
- Produces `claw-VERSION-ARCH.dpk` (tar.zst archive)

### `deploy.sh`
**Full workflow: Build → Install → Package**

```bash
./scripts/deploy.sh [BUILD_DIR] [SYSROOT_DIR] [OUTPUT_DIR]
```

Orchestrates:
1. Build standalone binary
2. Stage to sysroot
3. Package as dimsim .dpk
4. Generate deployment manifest

## Typical Workflows

### Quick Package Build (Single Command)

```bash
./scripts/deploy.sh
```

Outputs: `./dist/claw-0.1.0-x86_64.dpk`

### Standalone Binary Only (No Packaging)

```bash
./scripts/build-standalone.sh _build/
```

Outputs: `_build/claw`, `_build/clawctl`

### Custom Paths / Custom Build

```bash
mkdir _build-custom && cd _build-custom
../configure --with-musl --enable-static-binary --prefix=/custom
make -j$(nproc)
cd ..

SYSROOT_DIR=/tmp/custom-sysroot make install-sysroot
./scripts/package-dimsim.sh /tmp/custom-sysroot ./dist
```

### Make Targets (Alternative to Scripts)

```bash
# Build and stage to sysroot
make install-sysroot

# Create dimsim package
make package-dimsim

# Full workflow
make deploy-package

# Verify installed files
make show-layout
```

## Installation on BlueyOS

With dimsim package manager:

```bash
# On development machine
./scripts/deploy.sh

# Transfer package
scp dist/claw-*.dpk root@blueyos:/tmp/

# On BlueyOS system
dimsim install /tmp/claw-*.dpk
clawctl status
```

Manual extraction to root filesystem:

```bash
# Extract package
tar -xzf dist/claw-0.1.0-x86_64.dpk

# Copy to target sysroot
cp -r payload/* /mnt/blueyos-root/

# Create runtime directories
mkdir -p /mnt/blueyos-root/{run/claw,var/{lib,log}/claw}
chmod 700 /mnt/blueyos-root/{run/claw,var/{lib,log}/claw}
```

## Environment Variables

### `STAGING_DIR`
Directory to stage installation for packaging.

```bash
STAGING_DIR=/tmp/claw ./scripts/build-standalone.sh
```

### `CC`
C compiler (defaults to musl-gcc).

```bash
CC=gcc ./configure  # Use system GCC instead
```

## Troubleshooting

**Scripts not executable?**
```bash
chmod +x scripts/*.sh
```

**zstd or jq not found?**
```bash
# Ubuntu/Debian
sudo apt-get install zstandard jq

# Alpine
apk add zstd jq
```

**Autotools not found?**
```bash
./autogen.sh  # Regenerates configure
```

## Next Steps

See [DEPLOY.md](../DEPLOY.md) for comprehensive deployment guide.
