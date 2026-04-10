dnl Process this file with autoconf to produce configure.
dnl Claw Init System — PID 1 orchestrator for BlueyOS

AC_PREREQ([2.69])
AC_INIT([claw], [0.1.0], [], [claw])
AC_CONFIG_SRCDIR([src/claw/main.c])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall -Werror])
AM_SILENT_RULES([yes])

dnl Optional musl toolchain prefix and compiler sysroot.
musl_prefix=""
AC_ARG_WITH([musl-prefix],
  [AS_HELP_STRING([--with-musl-prefix=PATH],
    [prefix containing bin/musl-gcc when using the bundled musl toolchain])],
  [AS_IF([test "x$withval" = "xno"], [musl_prefix=""], [musl_prefix="$withval"])])

CLAW_SYSROOT=""
AC_ARG_WITH([sysroot],
  [AS_HELP_STRING([--with-sysroot=PATH],
    [pass --sysroot=PATH to the compiler and linker])],
  [AS_IF([test "x$withval" = "xno"], [CLAW_SYSROOT=""], [CLAW_SYSROOT="$withval"])])

AS_IF([test -n "$CLAW_SYSROOT"], [
  CFLAGS="$CFLAGS --sysroot=$CLAW_SYSROOT"
  LDFLAGS="$LDFLAGS --sysroot=$CLAW_SYSROOT"
  AC_MSG_NOTICE([Using sysroot: $CLAW_SYSROOT])
])

dnl ---------------------------------------------------------------------------
dnl Compiler selection
dnl Handle --with-musl BEFORE AC_PROG_CC — it reads CC exactly once at call time.
dnl
dnl Logic:
dnl   - If CC is already set in environment, respect it (cross-compiler setup)
dnl   - If CC is not set, use --with-musl / --with-musl-prefix to find musl-gcc
dnl   - If neither, use system default
dnl ---------------------------------------------------------------------------
want_musl="no"
musl_locator=""
AC_ARG_WITH([musl],
  [AS_HELP_STRING([--with-musl@<:@=PATH@:>@],
    [use musl-gcc as the C compiler; optionally specify a musl-gcc binary or toolchain prefix])],
  [
    AS_IF([test "x$withval" = "xno"], [
      want_musl="no"
    ], [
      want_musl="yes"
      AS_IF([test -n "$withval" && test "x$withval" != "xyes"], [
        musl_locator="$withval"
      ])
    ])
  ], [])

AS_IF([test -z "$CC"], [
  AS_IF([test "x$want_musl" = "xyes" || test -n "$musl_prefix"], [
    AS_IF([test -n "$musl_locator"], [
      AS_IF([test -x "$musl_locator"], [
        CC="$musl_locator"
        AC_MSG_NOTICE([--with-musl=$musl_locator: using $CC])
      ], [
        AS_IF([test -x "$musl_locator/bin/musl-gcc"], [
          CC="$musl_locator/bin/musl-gcc"
          AC_MSG_NOTICE([--with-musl=$musl_locator: using $CC])
        ], [
          AC_MSG_ERROR([musl-gcc not found in $musl_locator (tried: $musl_locator, $musl_locator/bin/musl-gcc)])
        ])
      ])
    ], [
      AS_IF([test -n "$musl_prefix"], [
        AS_IF([test -x "$musl_prefix/bin/musl-gcc"], [
          CC="$musl_prefix/bin/musl-gcc"
          AC_MSG_NOTICE([--with-musl-prefix=$musl_prefix: using $CC])
        ], [
          AC_MSG_ERROR([musl-gcc not found under --with-musl-prefix=$musl_prefix (tried: $musl_prefix/bin/musl-gcc)])
        ])
      ], [
        CC=musl-gcc
        AC_MSG_NOTICE([--with-musl: using musl-gcc from PATH])
      ])
    ])
  ])
], [
  AS_IF([test "x$want_musl" = "xyes" || test -n "$musl_prefix"], [
    AC_MSG_NOTICE([CC already set in environment; ignoring --with-musl and --with-musl-prefix])
  ])
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
  [BINARY_LDFLAGS="-static -lc"
   AC_MSG_NOTICE([Static linking enabled (BINARY_LDFLAGS=$BINARY_LDFLAGS)])])
AC_SUBST([BINARY_LDFLAGS])

dnl ---------------------------------------------------------------------------
dnl Stack-protector probe
dnl
dnl -fstack-protector-strong requires GCC >= 4.9.  Fall back gracefully so
dnl the build works on older or stripped-down toolchains (e.g. some musl cross
dnl compilers).
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
  Compiler:       $CC
  CFLAGS:         $CFLAGS
  compile flags:  -std=c99 $WARN_CFLAGS $SECURITY_CFLAGS
  Static binary:  $enable_static_binary
  BINARY_LDFLAGS: $BINARY_LDFLAGS
  musl prefix:    ${musl_prefix:-'(none)'}
  sysroot:        ${CLAW_SYSROOT:-'(none)'}

  Directories:
  prefix:         $prefix
  sbindir:        $sbindir
  bindir:         $bindir
  sysconfdir:     $sysconfdir
  localstatedir:  $localstatedir

  Run 'make' to build, 'make install' to install.
])
