dnl Process this file with autoconf to produce configure.
dnl Claw Init System — PID 1 orchestrator for BlueyOS

AC_PREREQ([2.69])
AC_INIT([claw], [0.1.0], [], [claw])
AC_CONFIG_SRCDIR([src/claw/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

dnl ---------------------------------------------------------------------------
dnl Parse explicit toolchain / sysroot options BEFORE AM_INIT_AUTOMAKE so that
dnl the auto-detection code below can respect them, and so that host_alias can
dnl be set in time for AM_INIT_AUTOMAKE to activate cross-compilation mode.
dnl ---------------------------------------------------------------------------

dnl --with-sysroot=PATH — pass --sysroot to the compiler and linker
CLAW_SYSROOT=""
AC_ARG_WITH([sysroot],
  [AS_HELP_STRING([--with-sysroot=PATH],
    [pass --sysroot=PATH to the compiler and linker])],
  [AS_IF([test "x$withval" = "xno"], [CLAW_SYSROOT="disabled"], [CLAW_SYSROOT="$withval"])])

dnl --with-musl-prefix=PATH — musl-gcc under PATH/bin or PATH/usr/bin
musl_prefix=""
AC_ARG_WITH([musl-prefix],
  [AS_HELP_STRING([--with-musl-prefix=PATH],
    [prefix containing musl-gcc under bin/ or usr/bin/ when using a bundled musl toolchain])],
  [AS_IF([test "x$withval" = "xno"], [musl_prefix=""], [musl_prefix="$withval"])])

dnl --with-musl[=PATH] — use musl-gcc; optional binary, toolchain prefix, or sysroot root
want_musl="no"
musl_locator=""
AC_ARG_WITH([musl],
  [AS_HELP_STRING([--with-musl@<:@=PATH@:>@],
    [use musl-gcc as the C compiler; optionally supply a musl-gcc binary, toolchain prefix, or sysroot root])],
  [AS_IF([test "x$withval" = "xno"], [want_musl="no"], [
    want_musl="yes"
    AS_IF([test -n "$withval" -a "x$withval" != "xyes"], [musl_locator="$withval"])
  ])])

dnl ---------------------------------------------------------------------------
dnl BlueyOS cross-toolchain auto-detection
dnl
dnl Trigger condition: BOTH of these are true
dnl   1. /opt/blueyos-cross/bin/i386-blueyos-elf-gcc is executable
dnl   2. /opt/blueyos-sysroot/usr/include/ exists
dnl AND none of CC, --host, --with-sysroot, --with-musl-prefix have been given.
dnl
dnl What it does:
dnl   • Finds the GCC library directory inside /opt/blueyos-cross
dnl   • Generates a GCC specs override (blueyos-cross.specs) in the build dir
dnl     — this fixes startfile names (crt1.o not crt0.o), nostdinc, lib paths
dnl   • Sets CC, CFLAGS, LDFLAGS, CLAW_SYSROOT
dnl   • Sets host_alias=i386-blueyos-elf before AM_INIT_AUTOMAKE so that
dnl     automake correctly activates cross-compilation mode
dnl
dnl To disable: pass --with-sysroot=no, set CC, or pass --host explicitly
dnl ---------------------------------------------------------------------------
BLUEYOS_CROSS_PREFIX="${BLUEYOS_CROSS:-/opt/blueyos-cross}"
BLUEYOS_CROSS_DEFAULT_SYSROOT="${BLUEYOS_SYSROOT:-/opt/blueyos-sysroot}"
BLUEYOS_CROSS_CC="$BLUEYOS_CROSS_PREFIX/bin/i386-blueyos-elf-gcc"
claw_using_cross_specs="no"

AS_IF([test -z "$CC"],
  [AS_IF([test -z "$host_alias" -o "x$host_alias" = "xi386-blueyos-elf"],
    [AS_IF([test "x$CLAW_SYSROOT" != "xdisabled"],
      [AS_IF([test -z "$musl_prefix" -a "x$want_musl" = "xno"],
        [AS_IF([test -x "$BLUEYOS_CROSS_CC" -a -d "$BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/include"],
          [dnl Find the GCC library directory (take highest version)
           blueyos_gcclibdir=""
           for _bld in "$BLUEYOS_CROSS_PREFIX/lib/gcc/i386-blueyos-elf"/*/; do
             AS_IF([test -f "${_bld}libgcc.a"], [blueyos_gcclibdir="$_bld"])
           done
           blueyos_gcclibdir="${blueyos_gcclibdir%/}"

           AS_IF([test -n "$blueyos_gcclibdir"],
             [AC_MSG_NOTICE([Auto-detected BlueyOS cross toolchain:])
              AC_MSG_NOTICE([  compiler : $BLUEYOS_CROSS_CC])
              AC_MSG_NOTICE([  GCC lib  : $blueyos_gcclibdir])
              AC_MSG_NOTICE([  sysroot  : $BLUEYOS_CROSS_DEFAULT_SYSROOT])
              AC_MSG_NOTICE([  (pass --with-sysroot=no or set CC to override)])

              dnl Generate a GCC specs override in the build directory.
              dnl
              dnl The cross-compiler was built --without-headers so it has no
              dnl default sysroot and looks for "crt0.o" (unavailable in musl).
              dnl This specs file fixes:
              dnl   *cpp_options / *cc1  — add correct -isystem include dirs
              dnl   *startfile           — use crt1.o (musl) not crt0.o (glibc)
              dnl   *link_libgcc         — add sysroot and GCC lib to lib search
              dnl   *link                — sysroot + -nostdlib
              cat > blueyos-cross.specs << '_BLUEYOS_SPECS_'
%rename cpp_options old_cpp_options
_BLUEYOS_SPECS_
              cat >> blueyos-cross.specs << _BLUEYOS_SPECS_
*cpp_options:
-nostdinc -isystem $BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/include -isystem $blueyos_gcclibdir/include -isystem include%s %(old_cpp_options)

*cc1:
%(cc1_cpu) -nostdinc -isystem $BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/include -isystem $blueyos_gcclibdir/include -isystem include%s

*startfile:
%{!shared: $BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/lib/crt1.o} $BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/lib/crti.o crtbegin.o%s

*endfile:
crtend.o%s $BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/lib/crtn.o

*link_libgcc:
-L$BLUEYOS_CROSS_DEFAULT_SYSROOT/usr/lib -L$blueyos_gcclibdir -L .%s

*libgcc:
libgcc.a%s

*link:
--sysroot=$BLUEYOS_CROSS_DEFAULT_SYSROOT -nostdlib %{shared:-shared} %{static:-static} %{rdynamic:-export-dynamic}

*lib:
%{!shared:-lc}

*link_ssp:


_BLUEYOS_SPECS_
              dnl Generate a thin compiler wrapper that embeds the -specs= flag.
              dnl
              dnl Using a wrapper instead of putting -specs= in CFLAGS/LDFLAGS
              dnl avoids duplicate loading when autoconf concatenates both into
              dnl a single compiler invocation during configure probe programs.
              claw_specs_path="`pwd`/blueyos-cross.specs"
              claw_cc_wrapper="`pwd`/blueyos-cc"
              cat > "$claw_cc_wrapper" << _WRAPPER_EOF
#!/bin/sh
exec $BLUEYOS_CROSS_CC -specs=$claw_specs_path "\$@"
_WRAPPER_EOF
              chmod +x "$claw_cc_wrapper"

              CC="$claw_cc_wrapper"
              CLAW_SYSROOT="$BLUEYOS_CROSS_DEFAULT_SYSROOT"
              host_alias="i386-blueyos-elf"
              dnl Force cross-compilation mode.  host_alias is set above so
              dnl AC_CANONICAL_HOST will compute host != build, but cross_compiling
              dnl is also read directly by AC_PROG_CC before that comparison
              dnl takes effect.  Setting it explicitly here is safe and ensures
              dnl AC_PROG_CC does not try to run i386 test binaries on the host.
              cross_compiling=yes
              claw_using_cross_specs="yes"
             ],
             [AC_MSG_WARN([BlueyOS cross compiler found but no GCC libdir under $BLUEYOS_CROSS_PREFIX/lib/gcc/i386-blueyos-elf/; skipping auto-detect])]
           )
          ])
       ])
    ])
  ])
])

dnl Set CLAW_SYSROOT back to empty if it was only set to "disabled" sentinel
AS_IF([test "x$CLAW_SYSROOT" = "xdisabled"], [CLAW_SYSROOT=""])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AM_MAINTAINER_MODE([disable])
AM_SILENT_RULES([yes])

dnl ---------------------------------------------------------------------------
dnl Compiler selection — musl-gcc locator
dnl
dnl Only runs if CC is still unset (auto-detect or explicit CC have priority).
dnl ---------------------------------------------------------------------------
AS_IF([test -z "$CC"], [
  AS_IF([test "x$want_musl" = "xyes" -o -n "$musl_prefix"], [
    AS_IF([test -n "$musl_locator"], [
      claw_musl_candidate=""
      for claw_try in \
        "$musl_locator" \
        "$musl_locator/bin/musl-gcc" \
        "$musl_locator/usr/bin/musl-gcc"; do
        AS_IF([test -x "$claw_try"], [
          claw_musl_candidate="$claw_try"
          break
        ])
      done
      AS_IF([test -n "$claw_musl_candidate"], [
        CC="$claw_musl_candidate"
        AC_MSG_NOTICE([--with-musl=$musl_locator: using $CC])
      ], [
        AC_MSG_ERROR([musl-gcc not found in $musl_locator (tried: $musl_locator, $musl_locator/bin/musl-gcc, $musl_locator/usr/bin/musl-gcc)])
      ])
    ], [
      AS_IF([test -n "$musl_prefix"], [
        claw_musl_candidate=""
        for claw_try in \
          "$musl_prefix/bin/musl-gcc" \
          "$musl_prefix/usr/bin/musl-gcc"; do
          AS_IF([test -x "$claw_try"], [
            claw_musl_candidate="$claw_try"
            break
          ])
        done
        AS_IF([test -n "$claw_musl_candidate"], [
          CC="$claw_musl_candidate"
          AC_MSG_NOTICE([--with-musl-prefix=$musl_prefix: using $CC])
        ], [
          AC_MSG_ERROR([musl-gcc not found under --with-musl-prefix=$musl_prefix (tried: $musl_prefix/bin/musl-gcc, $musl_prefix/usr/bin/musl-gcc)])
        ])
      ], [
        CC=musl-gcc
        AC_MSG_NOTICE([--with-musl: using musl-gcc from PATH])
      ])
    ])
  ])
], [
  AS_IF([test "x$want_musl" = "xyes" -o -n "$musl_prefix"], [
    AC_MSG_NOTICE([CC already set; ignoring --with-musl and --with-musl-prefix])
  ])
])

dnl ---------------------------------------------------------------------------
dnl Infer sysroot from a sysroot-style musl compiler when configure is given
dnl /path/to/sysroot or /path/to/sysroot/usr rather than an explicit sysroot.
dnl ---------------------------------------------------------------------------
AS_IF([test -z "$CLAW_SYSROOT"], [
  claw_inferred_sysroot=""
  case "$CC" in
    */usr/bin/musl-gcc)
      claw_inferred_sysroot=`printf '%s\n' "$CC" | sed 's,/usr/bin/musl-gcc$,,'`
      ;;
  esac
  AS_IF([test -n "$claw_inferred_sysroot" -a -d "$claw_inferred_sysroot/usr/include"], [
    CLAW_SYSROOT="$claw_inferred_sysroot"
    AC_MSG_NOTICE([Inferred sysroot from musl compiler: $CLAW_SYSROOT])
  ])
])

dnl ---------------------------------------------------------------------------
dnl Apply --sysroot to CFLAGS/LDFLAGS if we're NOT using a specs override
dnl (specs already encodes --sysroot in the *link spec above)
dnl ---------------------------------------------------------------------------
AS_IF([test -n "$CLAW_SYSROOT" -a "x$claw_using_cross_specs" = "xno"], [
  CFLAGS="$CFLAGS --sysroot=$CLAW_SYSROOT"
  LDFLAGS="$LDFLAGS --sysroot=$CLAW_SYSROOT"
  AC_MSG_NOTICE([Using sysroot: $CLAW_SYSROOT])
])

AC_PROG_CC
AM_PROG_AR
AC_PROG_RANLIB

dnl ---------------------------------------------------------------------------
dnl Header checks — must run before any -Werror flags are set in CFLAGS,
dnl as autoconf probe code may trigger harmless diagnostics.
dnl ---------------------------------------------------------------------------
AC_CHECK_HEADERS([sys/types.h sys/wait.h sys/select.h signal.h])
AC_CHECK_HEADERS([sys/socket.h sys/un.h sys/stat.h sys/mount.h])
AC_CHECK_HEADERS([fcntl.h unistd.h errno.h time.h])

dnl ---------------------------------------------------------------------------
dnl Static binary option
dnl
dnl We do NOT put -static into LDFLAGS globally — that would cause configure's
dnl own link-test programs to be linked statically, which fails on hosts that
dnl do not have the static libc installed.  Instead we collect the flag into
dnl BINARY_LDFLAGS and apply it only in Makefile.am (per-program LDFLAGS).
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE([static-binary],
  [AS_HELP_STRING([--enable-static-binary],
    [link claw and clawctl as static executables @<:@default: yes@:>@])],
  [enable_static_binary=$enableval],
  [enable_static_binary=yes])

BINARY_LDFLAGS=""
AS_IF([test "x$enable_static_binary" = "xyes"],
  [BINARY_LDFLAGS="-static"
   AC_MSG_NOTICE([Static linking enabled (BINARY_LDFLAGS=$BINARY_LDFLAGS)])])
AC_SUBST([BINARY_LDFLAGS])

dnl ---------------------------------------------------------------------------
dnl Sysroot/compiler sanity check
dnl
dnl A foreign sysroot can compile trivial probe programs while still failing
dnl at link time if CC targets the host instead of the sysroot. Validate a
dnl representative libc call here so configure fails with an actionable error.
dnl ---------------------------------------------------------------------------
AS_IF([test -n "$CLAW_SYSROOT"], [
  AC_MSG_CHECKING([whether $CC can link against $CLAW_SYSROOT])
  claw_save_LDFLAGS="$LDFLAGS"
  AS_IF([test -n "$BINARY_LDFLAGS"], [
    LDFLAGS="$LDFLAGS $BINARY_LDFLAGS"
  ])
  AC_LINK_IFELSE([
    AC_LANG_PROGRAM([
      [#include <time.h>]
    ], [
      time_t now = time((time_t *)0);
      return localtime(&now) == 0;
    ])
  ], [
    AC_MSG_RESULT([yes])
  ], [
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([
Compiler/sysroot mismatch for $CLAW_SYSROOT.
With the BlueyOS cross toolchain this should work automatically.
If you passed --with-sysroot explicitly, ensure CC targets that sysroot:

  CC=i386-blueyos-elf-gcc ./configure --host=i386-blueyos-elf \\
      --with-sysroot=/opt/blueyos-sysroot ...

Or let configure auto-detect by omitting CC, --host, --with-sysroot, and
--with-musl-prefix when /opt/blueyos-cross and /opt/blueyos-sysroot exist.
])
  ])
  LDFLAGS="$claw_save_LDFLAGS"
])

dnl ---------------------------------------------------------------------------
dnl Stack-protector probe
dnl
dnl -fstack-protector-strong requires GCC >= 4.9.  Fall back gracefully so
dnl the build works on older or stripped-down toolchains.
dnl ---------------------------------------------------------------------------
AX_CHECK_COMPILE_FLAG([-fstack-protector-strong],
  [SECURITY_CFLAGS="-fstack-protector-strong"],
  [SECURITY_CFLAGS=""
   AC_MSG_WARN([-fstack-protector-strong not supported; stack protection omitted])])
AC_SUBST([SECURITY_CFLAGS])

dnl ---------------------------------------------------------------------------
dnl Warning flags — added AFTER all AC_CHECK_* macros so configure probes are
dnl not disrupted by -Werror converting their incidental warnings into errors.
dnl ---------------------------------------------------------------------------
AC_ARG_ENABLE([werror],
  [AS_HELP_STRING([--enable-werror],
    [treat compiler warnings as errors @<:@default: yes@:>@])],
  [enable_werror=$enableval],
  [enable_werror=yes])

WARN_CFLAGS="-Wall -Wextra"
AS_IF([test "x$enable_werror" = "xyes"],
  [WARN_CFLAGS="$WARN_CFLAGS -Werror"])
AC_SUBST([WARN_CFLAGS])
AC_SUBST([CLAW_SYSROOT])

dnl ---------------------------------------------------------------------------
dnl Output
dnl ---------------------------------------------------------------------------
AC_CONFIG_FILES([Makefile include/claw-version.h])
AC_OUTPUT

AC_MSG_RESULT([
  Claw $VERSION configured
  -------------------------
  Compiler:         $CC
  Host:             ${host_alias:-'(native)'}
  Cross-compile:    $cross_compiling
  Specs override:   ${claw_using_cross_specs}
  CFLAGS:           $CFLAGS
  compile flags:    -std=c99 $WARN_CFLAGS $SECURITY_CFLAGS
  Static binary:    $enable_static_binary
  BINARY_LDFLAGS:   $BINARY_LDFLAGS
  musl prefix:      ${musl_prefix:-'(none)'}
  sysroot:          ${CLAW_SYSROOT:-'(none)'}

  Directories:
  prefix:           $prefix
  sbindir:          $sbindir
  bindir:           $bindir
  sysconfdir:       $sysconfdir
  localstatedir:    $localstatedir

  Run 'make' to build, 'make install' to install.
])

