PHP_ARG_WITH(libev, for libev support,
[  --with-libev           Include libev support])

if test "$PHP_LIBEV" != "no"; then
  
  m4_include([libev/libev.m4])
  
  PHP_ADD_INCLUDE(libev)
  
  AC_DEFINE([EV_H], "ev_custom.h", [Custom wrapper for ev.h])
  
  PHP_ADD_EXTENSION_DEP(libev, sockets, true)
  PHP_SUBST(LIBEV_SHARED_LIBADD)
  PHP_NEW_EXTENSION(libev, libev.c libev/ev.c, $ext_shared)
fi
