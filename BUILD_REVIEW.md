# Claw Build System Review & Deployment Guide

## Current State

### ✅ What's Working Well
- **Autotools integration**: Properly isolated configure probes from `-Werror` flags
- **Static linking**: Correctly uses `BINARY_LDFLAGS` to avoid breaking host detection
- **Cross-compiler support**: `--with-musl` flag allows seamless musl-gcc targeting
- **Standard directory layout**: Follows GNU conventions (sbin/bin, /etc/claw, /var/lib/claw)
- **Configuration files**: YAML config in standard locations, distributed with `make dist`
- **Library separation**: Core daemon logic cleanly separated from clawctl tool

### ⚠️ Gaps for BlueyOS Integration

#### 1. **No Standalone Build Output Documentation**
The project lacks clear guidance on:
- Where binaries end up after `make install`
- How to build for a specific sysroot directory
- Installation prefix conventions for embedded systems

**Current flow:**
```
./configure --with-musl --enable-static-binary --prefix=/usr/local
make -j$(nproc)
make install  # Installs to /usr/local/sbin, /etc/claw, /var/lib/claw
```

**Problem:** Uses `/usr/local` by default, which isn't suitable for root disk images.

#### 2. **No DESTDIR/Staging Support Documentation**
The Makefile.am correctly uses `$(DESTDIR)`, but there's no documented workflow for:
- Building into a staging directory
- Creating a sysroot tarball for packaging
- Verifying installed layout before deployment

#### 3. **Missing Build Output Artifacts**
No explicit way to:
- Generate a manifest of installed files
- Create a tarball of the installed tree
- Capture version information in deployment

#### 4. **No Version/Metadata in Binaries**
Binaries don't embed version info, making it hard to:
- Verify which version was installed
- Track deployment state
- Generate metadata for packaging systems

#### 5. **Limited Configuration Flexibility**
- Config file locations are hardcoded in source
- No compile-time option to adjust paths
- Testing different deployments requires reconfiguration

---

## Recommended Improvements

### Phase 1: Enhanced Build Workflow (Immediate)

#### 1.1 Add Build Instructions to README

**File:** `README.md`

```markdown
# Building Claw for BlueyOS

## Quick Start: Standalone Build

### Prerequisites
- musl toolchain (musl-gcc)
- autoconf >= 2.69
- automake >= 1.16.5

### Build for Root Filesystem

For deployment into BlueyOS root filesystem:

\`\`\`bash
./autogen.sh
./configure \
  --with-musl \
  --enable-static-binary \
  --prefix= \
  --sbindir=/sbin \
  --bindir=/bin \
  --sysconfdir=/etc \
  --localstatedir=/var

make -j$(nproc)
\`\`\`

### Install to Staging Sysroot

To stage installation for packaging or disk imaging:

\`\`\`bash
make install DESTDIR=/tmp/claw-sysroot
\`\`\`

This creates:
- `/tmp/claw-sysroot/sbin/claw` (PID 1 binary)
- `/tmp/claw-sysroot/bin/clawctl` (control tool)
- `/tmp/claw-sysroot/etc/claw/` (configuration)
- `/tmp/claw-sysroot/var/lib/claw/` (runtime state directory)
- `/tmp/claw-sysroot/var/log/claw/` (log directory)

### Development Build

For development with debugging symbols:

\`\`\`bash
./autogen.sh
./configure \
  --disable-static-binary \
  --disable-werror \
  CFLAGS='-O0 -g'

make -j$(nproc)
\`\`\`

Binaries available as `./claw` and `./clawctl` in build directory.
```

#### 1.2 Add Makefile Target for Sysroot Packaging

**File:** `Makefile.am` (add to end)

```makefile
# ---------------------------------------------------------------------------
# Deployment targets
# ---------------------------------------------------------------------------

# Create a tarball of the installed sysroot for packaging
.PHONY: install-sysroot
install-sysroot:
	@echo "Installing to staging directory: $(SYSROOT_STAGING)"
	mkdir -p $(SYSROOT_STAGING)
	$(MAKE) install DESTDIR=$(SYSROOT_STAGING)
	@echo ""
	@echo "Sysroot staged at: $(SYSROOT_STAGING)"
	@echo "File structure:"
	@find $(SYSROOT_STAGING) -type f | sort

# Create deployment tarball
.PHONY: dist-sysroot
dist-sysroot: install-sysroot
	@echo "Creating sysroot tarball..."
	tar -C $(SYSROOT_STAGING) -czf claw-sysroot-$(VERSION).tar.gz .
	@echo "Created: claw-sysroot-$(VERSION).tar.gz"
	@ls -lh claw-sysroot-$(VERSION).tar.gz

# Generate file manifest for verification
.PHONY: install-manifest
install-manifest: install-sysroot
	@find $(SYSROOT_STAGING) -type f \
	  | sed "s|^$(SYSROOT_STAGING)||" \
	  | sort > claw-manifest.txt
	@echo "File manifest: claw-manifest.txt"
	@cat claw-manifest.txt
```

Update the top of Makefile.am to define defaults:

```makefile
# ---------------------------------------------------------------------------
# Deployment configuration
# ---------------------------------------------------------------------------
SYSROOT_STAGING ?= $(CURDIR)/sysroot-staging
```

### Phase 2: Version Tracking & Metadata (Short-term)

#### 2.1 Embed Version in Binaries

**File:** `configure.ac` (after `AC_INIT`)

```sh
# Extract version for embedding in code
CLAW_VERSION="$PACKAGE_VERSION"
AC_SUBST([CLAW_VERSION])

# Generate version header
AC_CONFIG_FILES([src/claw/version.h])
```

**File:** `src/claw/version.h.in`

```c
#ifndef CLAW_VERSION_H
#define CLAW_VERSION_H

#define CLAW_VERSION "@CLAW_VERSION@"
#define CLAW_COMMIT  "@GIT_COMMIT@"

#endif
```

**File:** `src/claw/main.c` (add to main)

```c
printf("Claw %s\n", CLAW_VERSION);
```

#### 2.2 Generate Deployment Manifest

**File:** `contrib/install-hooks.sh`

```bash
#!/bin/bash
# Called during 'make install' to generate metadata

set -e

DESTDIR="${1:-.}"
VERSION="$2"

# Create manifest
find "$DESTDIR" -type f | while read file; do
    relpath="${file#$DESTDIR}"
    md5sum "$file" | awk -v path="$relpath" '{print $1, path}'
done | sort > "$DESTDIR/MANIFEST.md5"

# Create metadata
cat > "$DESTDIR/METADATA.txt" <<EOF
Claw Init System Deployment
Version: $VERSION
Installed: $(date -u +%Y-%m-%dT%H:%M:%SZ)
Hostname: $(hostname)
User: $(whoami)

File Count: $(find "$DESTDIR" -type f | wc -l)
Directory Count: $(find "$DESTDIR" -type d | wc -l)
EOF
```

### Phase 3: Container/Sysroot-Ready Configuration

#### 3.1 Support Configuration Path Prefixes

**Problem:** Config paths are hardcoded in source code.

**File:** `src/core/config/parser.c` (add near top)

```c
// Support both absolute paths and SYSROOT-relative paths
// This allows testing against different sysroot deployments

#define CLAW_CONFIG_SEARCH_PATHS { \
    "/etc/claw/claw.conf",           /* production */ \
    "/etc/claw/claw.yml",            /* fallback */ \
    NULL \
}

// Or if using a prefix:
#define CLAW_CONFIG_PREFIX getenv("CLAW_CONFIG_PREFIX") ?: ""
#define CLAW_CONFIG_PATH_MAIN \
    (snprintf(path, sizeof(path), "%s/etc/claw/claw.conf", \
     CLAW_CONFIG_PREFIX), path)
```

#### 3.2 Runtime Path Configuration

**File:** `include/claw.h` (add)

```c
// Runtime configuration paths - can be overridden with environment variables
// Used for testing different sysroot layouts

struct claw_paths {
    const char *config_dir;    // Path to /etc/claw
    const char *state_dir;     // Path to /var/lib/claw
    const char *log_dir;       // Path to /var/log/claw
    const char *run_dir;       // Path to /run/claw
};

struct claw_paths* claw_get_paths(void);
```

**File:** `src/core/config/paths.c` (new)

```c
#include "claw.h"
#include <stdlib.h>
#include <string.h>

static struct claw_paths _paths = {
    .config_dir = "/etc/claw",
    .state_dir  = "/var/lib/claw",
    .log_dir    = "/var/log/claw",
    .run_dir    = "/run/claw",
};

struct claw_paths* claw_get_paths(void) {
    // Allow override via environment for testing
    const char *prefix = getenv("CLAW_PREFIX");
    if (prefix) {
        static struct claw_paths override_paths;
        static char config_dir[256], state_dir[256], log_dir[256], run_dir[256];

        snprintf(config_dir, sizeof(config_dir), "%s/etc/claw", prefix);
        snprintf(state_dir, sizeof(state_dir), "%s/var/lib/claw", prefix);
        snprintf(log_dir, sizeof(log_dir), "%s/var/log/claw", prefix);
        snprintf(run_dir, sizeof(run_dir), "%s/run/claw", prefix);

        override_paths.config_dir = config_dir;
        override_paths.state_dir  = state_dir;
        override_paths.log_dir    = log_dir;
        override_paths.run_dir    = run_dir;

        return &override_paths;
    }

    return &_paths;
}
```

---

## Recommended Build Workflows

### For BlueyOS Root Filesystem Deployment

```bash
# 1. Bootstrap
./autogen.sh

# 2. Configure for root filesystem (prefix=/)
./configure \
  --with-musl \
  --enable-static-binary \
  --prefix=/ \
  --exec-prefix=/usr \
  --sbindir=/usr/sbin \
  --bindir=/usr/bin \
  --sysconfdir=/etc \
  --localstatedir=/var

# 3. Build
make -j$(nproc)

# 4. Stage for imaging
mkdir -p /tmp/blueyos-root
make install DESTDIR=/tmp/blueyos-root

# 5. Verify layout
find /tmp/blueyos-root -type f | sort

# 6. Integrate into disk image
# (copy files from /tmp/blueyos-root into disk image build)
```

### For Packaging (dimsim or other)

```bash
# 1-3. Bootstrap and build (as above)

# 4. Generate metadata and manifest
make install-manifest SYSROOT_STAGING=/tmp/claw-pkg

# 5. Create package tarball
make dist-sysroot SYSROOT_STAGING=/tmp/claw-pkg

# 6. Package system integration
tar -xzf claw-sysroot-0.1.0.tar.gz -C /your/sysroot/build

# Verify with manifest
while read hash file; do
    md5sum "/your/sysroot/build$file" | awk -v expected="$hash" \
        '$1 != expected { print "MISMATCH: " $0; exit 1 }'
done < claw-manifest.txt
```

### For Container/Testing

```bash
# Mount sysroot at alternate location
mkdir -p /tmp/test-claw-sysroot
make install DESTDIR=/tmp/test-claw-sysroot

# Run with CLAW_PREFIX environment variable
CLAW_PREFIX=/tmp/test-claw-sysroot /tmp/test-claw-sysroot/usr/sbin/claw
```

---

## Checklist for Deployment Integration

- [ ] Add version embedding to binaries
- [ ] Create `install-manifest` Make target
- [ ] Create `dist-sysroot` Make target
- [ ] Document recommended configure flags in README
- [ ] Add `claw_get_paths()` for runtime configuration flexibility
- [ ] Test staging with `DESTDIR`
- [ ] Verify file permissions after install
- [ ] Verify symlinks are preserved
- [ ] Test cold boot with staged sysroot

---

## File Structure After `make install DESTDIR=/tmp/claw-sysroot`

```
/tmp/claw-sysroot/
├── usr/
│   ├── sbin/
│   │   └── claw                    # PID 1 init daemon
│   └── bin/
│       └── clawctl                 # Management tool
├── etc/
│   └── claw/
│       ├── claw.conf              # Main configuration
│       ├── services.d/            # Service definitions
│       │   ├── claw-network.service.yml
│       │   └── claw-syslog.service.yml
│       └── targets.d/             # Target definitions
│           ├── claw-basic.target.yml
│           ├── claw-early.target.yml
│           ├── claw-multiuser.target.yml
│           └── claw-network.target.yml
└── var/
    ├── lib/
    │   └── claw/                  # Runtime state (created by install-data-local)
    └── log/
        └── claw/                  # Logs (created by install-data-local)
```

---

## Next Steps

1. **Immediate**: Add README documentation with build examples
2. **Short-term**: Implement version embedding
3. **Medium-term**: Add Make targets for packaging workflows
4. **Long-term**: Consider multi-target support (glibc, Alpine musl-based containers, etc.)
