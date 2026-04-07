# Specifying musl Location

The build system now supports flexible musl configuration for custom installations (like kernel-integrated musl).

## BlueyOS i386-blueyos-elf Cross-Compiler

Your BlueyOS build uses a custom cross-compiler targeting `i386-blueyos-elf` (ELF format, i386 architecture).

### Standard Build

```bash
CC=i386-blueyos-elf-gcc \
CFLAGS="-O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 -nostdinc -isystem ${GCC_INCDIR} -isystem ${BLUEYOS_SYSROOT}/usr/include" \
LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
./configure \
  --host=i386-blueyos-elf \
  --enable-static-binary \
  --prefix=/ \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var

make -j$(nproc)
make install DESTDIR=/tmp/claw-stage
```

### Notes

- **`CC=i386-blueyos-elf-gcc`** — Your cross-compiler; respects `--with-musl` (doesn't override it)
- **`--host=i386-blueyos-elf`** — Target architecture
- **`--enable-static-binary`** — Static linking (default)
- **`CFLAGS/LDFLAGS`** — Your BlueyOS toolchain flags
- **`--prefix=/`** — Install to root (for integration into rootfs)

### With Custom CFLAGS/LDFLAGS

If you have standard BlueyOS build environment variables set:

```bash
CC=i386-blueyos-elf-gcc ./configure \
  --host=i386-blueyos-elf \
  --enable-static-binary \
  --prefix=/ \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var

make -j$(nproc)
```

The toolchain CFLAGS/LDFLAGS should be inherited from your build environment.

## Error Handling

**Problem**: `CC already set in environment; ignoring --with-musl`

This is expected behavior. When you set `CC=i386-blueyos-elf-gcc`, the build system respects your cross-compiler and doesn't try to find musl-gcc. This is the correct behavior for cross-compilation.

To use musl from a custom location with your cross-compiler:

```bash
# Environment variables take precedence; musl is baked into your cross-compiler
CC=i386-blueyos-elf-gcc ./configure --host=i386-blueyos-elf
```

Your cross-compiler already includes musl, so `--with-musl` is not needed.

### 1. Use System musl-gcc (Default)
```bash
./configure --with-musl --enable-static-binary
make -j$(nproc)
```
Looks for `musl-gcc` in your `$PATH`.

### 2. Specify musl Installation Directory
```bash
./configure --with-musl=/path/to/musl --enable-static-binary
make -j$(nproc)
```

Automatically searches for gcc in:
- `/path/to/musl/bin/musl-gcc` (standard layout)
- `/path/to/musl/musl-gcc` (alternate layout)

### 3. Specify Direct Path to musl-gcc Binary
```bash
./configure --with-musl=/path/to/musl-gcc --enable-static-binary
make -j$(nproc)
```

If the path is executable, it's used directly.

### 4. Use Environment Variable (Any Compiler)
```bash
CC=/path/to/compiler ./configure --enable-static-binary
make -j$(nproc)
```

Works with any C compiler (not just musl):
```bash
CC=arm-linux-musleabihf-gcc ./configure --enable-static-binary
CC=gcc ./configure --enable-static-binary
```

## BlueyOS Integration

If musl is integrated into your kernel build (e.g., `$BLUEYOS_BUILD/sysroot/musl`):

```bash
./configure \
  --with-musl=$BLUEYOS_BUILD/sysroot/musl \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var
```

Or with a full sysroot:

```bash
./configure \
  --with-musl=$BLUEYOS_BUILD/cross/bin/musl-gcc \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var
```

## Error Handling

If the specified path doesn't exist or doesn't contain musl-gcc:

```
configure: error: musl-gcc not found in /invalid/path (tried: /invalid/path/bin/musl-gcc, /invalid/path/musl-gcc)
```

Check your path and try again.

## Scripted Build

In CI/CD or deployment scripts:

```bash
#!/bin/bash
MUSL_PATH="${BLUEYOS_SYSROOT}/musl"
STAGE_DIR="${STAGE_DIR:-/tmp/claw-stage}"

./autogen.sh

mkdir -p _build-blueyos
cd _build-blueyos

../configure \
  --with-musl="$MUSL_PATH" \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var

make -j$(nproc)
make install DESTDIR="$STAGE_DIR"
```
