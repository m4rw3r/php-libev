#ifndef PHP_LIBEV_H
#define PHP_LIBEV_H 1

#define PHP_LIBEV_EXTNAME "libev"
#define PHP_LIBEV_EXTVER  "0.1"

extern zend_module_entry libev_module_entry;
#define phpext_libev_ptr &libev_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif

// Not sure if needed, but makes it easier to create Namespaced classes/methods
#define ZEND_NS_ME(ns, classname, name, arg_info, flags)	ZEND_NS_FENTRY(ns, name, ZEND_MN(classname##_##name), arg_info, flags)


#endif /* PHP_LIBEV_H */