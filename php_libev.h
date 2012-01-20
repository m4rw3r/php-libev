
/*
 * Copyright (c) 2012 Martin Wernståhl <m4rw3r@gmail.com>. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 * 
 *    1. Redistributions of source code must retain the above copyright notice, this list of
 *       conditions and the following disclaimer.
 * 
 *    2. Redistributions in binary form must reproduce the above copyright notice, this list
 *       of conditions and the following disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY Martin Wernståhl ''AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Martin Wernståhl OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation are those of the
 * authors and should not be interpreted as representing official policies, either expressed
 * or implied, of Martin Wernståhl.
 */

#ifndef PHP_LIBEV_H
#define PHP_LIBEV_H 1

#define PHP_LIBEV_EXTNAME "libev"
#define PHP_LIBEV_EXTVER  "0.1"

extern zend_module_entry libev_module_entry;
#define phpext_libev_ptr &libev_module_entry

#ifdef ZTS
#include "TSRM.h"
#endif


#define check_callable(/* zval */ zcallback, /* char * */ tmp) \
	if( ! zend_is_callable(zcallback, 0, &tmp TSRMLS_CC))      \
	{                                                          \
		php_error_docref(NULL TSRMLS_CC, E_WARNING,            \
			"'%s' is not a valid callback", tmp);              \
		efree(tmp);                                            \
		RETURN_FALSE;                                          \
	}                                                          \
	efree(tmp)


/* Returns true if the supplied *instance_ce == *ce or if any of *instance_ce's parent
   class-entries equals *ce. Ie. instanceof, but without the interface check. */
inline int instance_of_class(const zend_class_entry *instance_ce, const zend_class_entry *ce)
{
	while(instance_ce)
	{
		if (instance_ce == ce)
		{
			return 1;
		}
		instance_ce = instance_ce->parent;
	}
	
	return 0;
}


#endif /* PHP_LIBEV_H */