# Claw — PID 1 Init for BlueyOS

**The Magic Claw chooses who will go and who will stay.**

A systemd-like init orchestrator written in C, statically linked against musl for deployment into the BlueyOS root filesystem.

## Quick Start

**For BlueyOS deployment**, see the deployment guide: [DEPLOY.md](DEPLOY.md)

### Single Command Build + Package

```bash
./scripts/deploy.sh
# Outputs: ./dist/claw-0.1.0-x86_64.dpk
```

This builds, stages, and packages as a dimsim `.dpk` package ready for deployment.

### Development Build

For development with debugging symbols:

```bash
./autogen.sh
./configure \
  --disable-static-binary \
  --disable-werror \
  CFLAGS='-O0 -g'

make -j$(nproc)
./claw --help
./clawctl --help
```

## Building for Different Targets

### Standard glibc/host system
```bash
./configure --enable-static-binary  # or omit for dynamic linking
make -j$(nproc)
```

### Embedded/static musl build
```bash
./configure --with-musl --enable-static-binary
make -j$(nproc)
```

### Embedded/static musl build
```bash
./configure --with-musl --enable-static-binary
make -j$(nproc)
```

### Alpine Linux or other musl-based distributions
```bash
./configure \
  --with-musl \
  --enable-static-binary \
  --prefix=/usr/local
make -j$(nproc)
sudo make install
```

## BlueyOS Deployment

### Packaging as dimsim

Create a BlueyOS-ready dimsim package `.dpk`:

```bash
./scripts/deploy.sh
```

Or step-by-step:

```bash
# 1. Build standalone binary
STAGING_DIR=/tmp/claw-sysroot ./scripts/build-standalone.sh

# 2. Package as dimsim
./scripts/package-dimsim.sh /tmp/claw-sysroot ./dist

# Output: ./dist/claw-0.1.0-x86_64.dpk
```

### Installing on BlueyOS

With dimsim package manager:

```bash
# Transfer package
scp dist/claw-*.dpk root@blueyos:/tmp/

# Install
ssh root@blueyos dimsim install /tmp/claw-*.dpk

# Verify
ssh root@blueyos clawctl status
```

### Integration into Disk Image Build

Extract and copy to your BlueyOS rootfs:

```bash
tar -xzf dist/claw-0.1.0-x86_64.dpk
cp -r payload/* /mnt/blueyos-rootfs/
mkdir -p /mnt/blueyos-rootfs/{run/claw,var/{lib,log}/claw}
chmod 700 /mnt/blueyos-rootfs/{run/claw,var/{lib,log}/claw}
```

## Installation

### Install to system
```bash
make install  # Installs to /usr/local by default
```

### Install to staging directory (for packaging)
```bash
DESTDIR=/tmp/staging make install

# Outputs:
# /tmp/staging/usr/local/sbin/claw
# /tmp/staging/usr/local/bin/clawctl
# /tmp/staging/etc/claw/*
# /tmp/staging/var/lib/claw/
# /tmp/staging/var/log/claw/
```

## Creating Deployment Packages

### Dimsim Package (.dpk)

See **BlueyOS Deployment** section above, or use the convenient helper:

```bash
# Full workflow
./scripts/deploy.sh

# Or manually
./scripts/build-standalone.sh
STAGING_DIR=/tmp/claw-sysroot ./scripts/build-standalone.sh
./scripts/package-dimsim.sh /tmp/claw-sysroot ./dist
```

Produces: `claw-0.1.0-x86_64.dpk` (tar.zst format with meta/ and payload/)

### Make Targets

Standard autotools installation to a staging directory:

```bash
# Stage to sysroot for packaging
make install-sysroot

# Create tarball
make dist-sysroot

# Generate verification manifest
make install-manifest
```

## Configuration

Configuration files are installed to `$(sysconfdir)/claw`:
- `claw.conf` — Main configuration (default boot target, logging, etc.)
- `services.d/*.yml` — Service definitions
- `targets.d/*.yml` — Target definitions (boot stages, synchronization points)

### Kernel Command Line

Claw also accepts one-shot boot overrides from the kernel command line:

- `single`, `s`, `S`, `1`, `rescue`, `emergency`, or `claw.single=1` starts single-user mode and opens a root shell on the system console instead of activating the normal boot target.
- `claw.target=<target-name>` overrides `default_target` for a single boot.

See [SPEC.md](SPEC.md) for architecture and boot sequence details.

## Prerequisites & Scripts

### Build Prerequisites
- `musl-gcc` cross-compiler
- `autoconf` >= 2.69
- `automake` >= 1.16.5
- `zstd` (for packaging)
- `jq` (for JSON processing)
- POSIX make

### Deployment Scripts

See [scripts/README.md](scripts/README.md) for quick reference.

**Key scripts:**
- `scripts/build-standalone.sh` — Build static musl binary
- `scripts/package-dimsim.sh` — Create dimsim .dpk package
- `scripts/deploy.sh` — Full workflow (build → package → manifest)

For comprehensive deployment guide, see [DEPLOY.md](DEPLOY.md).

## License

See LICENSE file for details.
