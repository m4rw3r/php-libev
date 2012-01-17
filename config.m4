PHP_ARG_WITH(libev, for libev support,
[  --with-libev           Include libev support])

if test "$PHP_LIBEV" != "no"; then
  SEARCH_PATH="/usr /usr/local"
  SEARCH_FOR="/include/ev.h"
  
  if test "$PHP_LIBEV" = "yes"; then
    AC_MSG_CHECKING([for libev headers in default path])
    for i in $SEARCH_PATH ; do
      if test -r $i/$SEARCH_FOR; then
        LIBEV_DIR=$i
        AC_MSG_RESULT(found in $i)
      fi
    done
  else
    AC_MSG_CHECKING([for libev headers in $PHP_LIBEV])
    if test -r $PHP_LIBEV/$SEARCH_FOR; then
      LIBEV_DIR=$PHP_LIBEV;
      AC_MSG_RESULT([found])
    fi
  fi
  
  if test -z "$LIBEV_DIR"; then
    AC_MSG_RESULT([not found])
    AC_MSG_ERROR([Cannot find libev headers])
  fi
  
  PHP_ADD_INCLUDE($LIBEV_DIR/include)
  
  LIBNAME=ev
  LIBSYMBOL=ev_loop_new
  
  if test "x$PHP_LIBDIR" = "x"; then
    PHP_LIBDIR=lib
  fi
  
  PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  [
    PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $LIBEV_DIR/$PHP_LIBDIR, LIBEV_SHARED_LIBADD)
  ],[
    AC_MSG_ERROR([wrong libevent version {1.4.+ is required} or lib not found])
  ],[
    -L$LIBEV_DIR/$PHP_LIBDIR 
  ])
  
  PHP_ADD_EXTENSION_DEP(libev, sockets, true)
  PHP_SUBST(LIBEV_SHARED_LIBADD)
  PHP_NEW_EXTENSION(libev, libev.c, $ext_shared)
fi
  