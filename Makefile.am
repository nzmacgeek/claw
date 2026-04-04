# Claw Init System — Automake input
# ---------------------------------------------------------------------------
# Built with: automake --foreign subdir-objects
# Two binaries: sbin/claw (daemon) and bin/clawctl (control tool)
# Three convenience libraries: libutil.a, libipc.a, libcore.a
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Global compiler flags
# AM_CPPFLAGS: preprocessor (-I, -D) — separate from compiler flags
# AM_CFLAGS:   per-translation-unit compiler flags
# ---------------------------------------------------------------------------
AM_CPPFLAGS = \
	-I$(top_srcdir)/include \
	-D_POSIX_C_SOURCE=200809L

AM_CFLAGS = \
	-std=c99 \
	$(WARN_CFLAGS) \
	$(SECURITY_CFLAGS)

# ---------------------------------------------------------------------------
# Convenience libraries (noinst: built but not installed)
# ---------------------------------------------------------------------------
noinst_LIBRARIES = libutil.a libipc.a libcore.a

libutil_a_SOURCES = \
	src/util/claw-string.c \
	src/util/claw-time.c \
	src/util/hashmap.c \
	src/util/log.c \
	src/util/mem.c \
	src/util/vector.c

libipc_a_SOURCES = \
	src/ipc/ipc.c

# libcore: daemon-only modules (not linked into clawctl)
libcore_a_SOURCES = \
	src/core/config/parser.c \
	src/core/dag/graph.c \
	src/core/service/service.c \
	src/core/service/supervisor.c \
	src/core/state/state.c \
	src/core/target/target.c \
	src/os/mount.c

# ---------------------------------------------------------------------------
# Binaries
# sbin_PROGRAMS -> $(sbindir)   (typically /sbin or /usr/local/sbin)
# bin_PROGRAMS  -> $(bindir)    (typically /bin  or /usr/local/bin)
# ---------------------------------------------------------------------------
sbin_PROGRAMS = claw
bin_PROGRAMS  = clawctl

claw_SOURCES = src/claw/main.c
claw_LDADD   = libcore.a libipc.a libutil.a
claw_LDFLAGS = $(BINARY_LDFLAGS)

clawctl_SOURCES = src/clawctl/main.c
clawctl_LDADD   = libipc.a libutil.a
clawctl_LDFLAGS = $(BINARY_LDFLAGS)

# ---------------------------------------------------------------------------
# Configuration data files
# dist_*_DATA: installed AND included in 'make dist' tarballs
# ---------------------------------------------------------------------------
clawconfdir        = $(sysconfdir)/claw
dist_clawconf_DATA = config/claw.conf

clawservicesdir        = $(clawconfdir)/services.d
dist_clawservices_DATA = \
	config/services.d/claw-network.service.yml \
	config/services.d/claw-syslog.service.yml

clawtargetsdir        = $(clawconfdir)/targets.d
dist_clawtargets_DATA = \
	config/targets.d/claw-basic.target.yml \
	config/targets.d/claw-early.target.yml \
	config/targets.d/claw-multiuser.target.yml \
	config/targets.d/claw-network.target.yml

# ---------------------------------------------------------------------------
# Runtime directory creation hook
# Always use $(DESTDIR) so packaging tools (dpkg, rpm) can stage into a
# temporary root rather than writing to the live filesystem.
# ---------------------------------------------------------------------------
install-data-local:
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/lib/claw
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/log/claw

# ---------------------------------------------------------------------------
# Extra files included in 'make dist' tarballs (not covered by other vars)
# ---------------------------------------------------------------------------
EXTRA_DIST = \
	autogen.sh \
	README.md \
	SPEC.md \
	LICENSE

# ---------------------------------------------------------------------------
# Lint target (not part of standard automake check machinery)
# ---------------------------------------------------------------------------
.PHONY: lint
lint:
	@command -v clang-format >/dev/null 2>&1 || \
	    { echo "clang-format not found"; exit 1; }
	find $(top_srcdir)/src $(top_srcdir)/include \
	    -name "*.c" -o -name "*.h" | xargs clang-format -i
	@echo "Code formatted"

# ---------------------------------------------------------------------------
# Test infrastructure hooks (empty for now; fill in as tests are added)
# ---------------------------------------------------------------------------
TESTS =
check_PROGRAMS =
