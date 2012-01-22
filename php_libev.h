
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

/* Used to initialize the object storage pointer in __construct
   event_object_prepare(event_object *, zval *) */
#define event_object_prepare(event_object_ptr, zcallback)                                 \
	event_object_ptr = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC); \
	zval_add_ref(&zcallback);                                                             \
	event_object_ptr->callback = zcallback;                                               \
	event_object_ptr->this     = getThis();                                               \
	event_object_ptr->evloop   = NULL
	
/* Allocates and initializes the watcher of the specified type (io, time, ...)
   and passes __VA_ARGS__ after the callback parameter of ev_TYPE_init
   event_create_watcher(event_object *, TYPE, ...) */
#define event_create_watcher(event_object, type, ...)    \
	assert( ! event_object->watcher);                    \
	event_object->watcher = emalloc(sizeof(ev_##type));  \
	memset(event_object->watcher, 0, sizeof(ev_##type)); \
	event_object->watcher->data = event_object;          \
	ev_##type##_init((ev_##type *)event_object->watcher, event_callback, __VA_ARGS__)

/* "Returns" true if the event watcher is active */
#define event_is_active(event_object)   ev_is_active(event_object->watcher)
/* "Returns" true if the event watcher is pending */
#define event_is_pending(event_object)  ev_is_pending(event_object->watcher)
/* "Returns" true if the event is associated with a loop */
#define event_has_loop(event_object)    (event_object->evloop)

/* Is true if event_object is registered with event_loop_object */
#define event_is_in_loop(event_object, event_loop_object) \
	(event_object->evloop && (event_object->evloop->loop == event_loop_object->loop))

/* Protects event_objects from garbage collection by increasing their
   refcount and storing them in the event_loop_object's doubly-linked
   list, also sets event_object->evloop to event_loop_object */
#define _loop_ref_add(event_object, event_loop_object)       \
	assert(event_object->this);                              \
	assert( ! event_object->evloop);                         \
	assert( ! event_object->next);                           \
	assert( ! event_object->prev);                           \
	zval_add_ref(&event_object->this);                       \
	event_object->evloop = event_loop_object;                \
	if( ! event_loop_object->events) {                       \
		event_object->next = NULL;                           \
		event_object->prev = NULL;                           \
		event_loop_object->events = event_object;            \
	}                                                        \
	else                                                     \
	{                                                        \
		event_object->next = event_loop_object->events;      \
		event_object->prev = NULL;                           \
		event_loop_object->events->prev = event_object;      \
		event_loop_object->events = event_object;            \
	}

/* Removes garbage collection protection by removing the event from the
   doubly linked list, nulling the event_object->evloop and finally calling
   zval_ptr_dtor */
#define _loop_ref_del(event_object)                                      \
	assert(event_object->evloop);                                        \
	assert(event_object->watcher);                                       \
	assert( ! event_is_active(event_object));                            \
	assert( ! event_is_pending(event_object));                           \
	if(event_object->next)                                               \
	{                                                                    \
		if(event_object->prev)                                           \
		{                                                                \
			/* Middle of the doubly-linked list */                       \
			event_object->prev->next = event_object->next;               \
			event_object->next->prev = event_object->prev;               \
		}                                                                \
		else                                                             \
		{                                                                \
			/* First of the doubly-linked list */                        \
			assert(event_object->evloop->events);                        \
			event_object->evloop->events = event_object->next;           \
			event_object->next->prev = NULL;                             \
		}                                                                \
	}                                                                    \
	else if(event_object->prev)                                          \
	{                                                                    \
		/* Last of the doubly-linked list */                             \
		assert(event_object->prev->next);                                \
		event_object->prev->next = NULL;                                 \
	}                                                                    \
	else                                                                 \
	{                                                                    \
		/* Only elment of the doubly-linked list */                      \
		assert(event_object->evloop->events);                            \
		event_object->evloop->events = NULL;                             \
	}                                                                    \
	event_object->next   = NULL;                                         \
	event_object->prev   = NULL;                                         \
	event_object->evloop = NULL;                                         \
	zval_ptr_dtor(&event_object->this)

#if LIBEV_DEBUG > 1
#  define loop_ref_add(event_object, event_loop_object)       \
	_loop_ref_add(event_object, event_loop_object);           \
	libev_printf("Increased refcount on Event 0x%lx to %d\n", \
		(unsigned long)((size_t) event_object->this),         \
		Z_REFCOUNT_P(event_object->this));

#  define loop_ref_del(event_object)                           \
	libev_printf("Decreasing refcount on Event 0x%lx to %d\n", \
		(unsigned long)((size_t) event_object->this),          \
		Z_REFCOUNT_P(event_object->this) - 1);                 \
	_loop_ref_del(event_object);
#else
#  define loop_ref_add(event_object, event_loop_object) _loop_ref_add(event_object, event_loop_object)
#  define loop_ref_del(event_object) _loop_ref_del(event_object)
#endif


#endif /* PHP_LIBEV_H */