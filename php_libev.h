
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "ev_custom.h"
#include "php.h"
#include "php_ini.h"
#include "php_streams.h"
#include "php_network.h"
#include "ext/sockets/php_sockets.h"


#if LIBEV_DEBUG == 2
#  define libev_printf(...) php_printf("phplibev: " __VA_ARGS__)
#  define IF_DEBUG(x) x
#else
#  define IF_DEBUG(x)
#  define libev_printf(...)
#endif

#define PHP_LIBEV_EXTNAME "libev"
#define PHP_LIBEV_EXTVER  "0.1"

extern zend_module_entry libev_module_entry;
#define phpext_libev_ptr &libev_module_entry

#ifdef ZTS
#  include "TSRM.h"
#endif

/* Define NO_REATAIN as 1 to keep libev default behaviour, that it does not
   retain the active Events beyond their scope */
#ifndef NO_RETAIN
#  define NO_RETAIN 0
#endif

struct _event_loop_object;

typedef struct event_object {
	zend_object std;
	int         eflags;
	ev_watcher  *watcher;
	zval        *this;
	zval        *callback;
	struct _event_loop_object *loop_obj;
	struct event_object *next; /* Part of double-linked list of loop_obj->events */
	struct event_object *prev; /* Part of double-linked list of loop_obj->events */
} event_object;

typedef struct _event_loop_object {
	zend_object       std;
	struct ev_loop    *loop;
	struct event_object *events; /* Head of the doubly-linked list of associated events */
} event_loop_object;


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

#define dFILE_DESC          \
	php_socket_t file_desc; \
	zval **fd;              \
	php_stream *stream;     \
	php_socket *php_sock;

#define EXTRACT_FILE_DESC(class, method) \
	/* Attempt to get the file descriptor from the stream */                              \
	if(ZEND_FETCH_RESOURCE_NO_RETURN(stream, php_stream*,                                 \
		fd, -1, NULL, php_file_le_stream()))                                              \
	{                                                                                     \
		if(php_stream_cast(stream, PHP_STREAM_AS_FD_FOR_SELECT |                          \
			PHP_STREAM_CAST_INTERNAL, (void*)&file_desc, 1) != SUCCESS || file_desc < 0)  \
		{                                                                                 \
			/* TODO: libev-specific exception class here */                               \
			zend_throw_exception(NULL, "libev\\" #class  ":: " #method                    \
				"(): invalid stream", 1 TSRMLS_DC);                                       \
		                                                                                  \
			return;                                                                       \
		}                                                                                 \
	}                                                                                     \
	else                                                                                  \
	{                                                                                     \
		if(ZEND_FETCH_RESOURCE_NO_RETURN(php_sock, php_socket *,                          \
			fd, -1, NULL, php_sockets_le_socket()))                                       \
		{                                                                                 \
			file_desc = php_sock->bsd_socket;                                             \
		}                                                                                 \
		else                                                                              \
		{                                                                                 \
			/* TODO: libev-specific exception class here */                               \
			zend_throw_exception(NULL,                                                    \
				"libev\\" #class  ":: " #method "(): fd argument must be either valid "   \
				"PHP stream or valid PHP socket resource", 1 TSRMLS_DC);                  \
		                                                                                  \
			return;                                                                       \
		}                                                                                 \
	}


/* TODO: Is it appropriate to throw an exception here? if we do not, incomplete Event
         objects are created when parameter parsing fails */
#define PARSE_PARAMETERS(class, param_str, ...) \
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, param_str, __VA_ARGS__) != SUCCESS) { \
		/* TODO: libev exception */                                                           \
		zend_throw_exception(NULL, "Error parsing parameters to libev\\" #class               \
			"::__construct()", 0 TSRMLS_DC);                                                  \
		return;                                                                               \
	}

#define dCALLBACK              \
	zval *callback = NULL;     \
	char *callback_tmp = NULL;

#define CHECK_CALLBACK                                            \
	do { if( ! zend_is_callable(callback, 0, &callback_tmp TSRMLS_CC)) \
	{                                                             \
		zend_throw_exception_ex(NULL, 0 TSRMLS_CC,                \
			"'%s' is not a valid callback", callback_tmp);        \
		efree(callback_tmp);                                      \
		RETURN_FALSE;                                             \
	}                                                             \
	efree(callback_tmp); } while(0)

/* Used to initialize the object storage pointer in __construct
   EVENT_OBJECT_PREPARE(event_object *, zval *) */
#define EVENT_OBJECT_PREPARE(event_object_ptr, zcallback)                                 \
	event_object_ptr = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC); \
	zval_add_ref(&zcallback);                                                             \
	event_object_ptr->callback = zcallback;                                               \
	/* Do not increase refcount for $this here, as otherwise we have a cycle */           \
	event_object_ptr->this     = getThis();                                               \
	IF_DEBUG(libev_printf("Allocated event 0x%lx\n", (size_t) event_object_ptr->this));   \
	event_object_ptr->loop_obj   = NULL

#define event_io_init(event,fd,events) \
	do{ assert(event->watcher); ev_io_init((ev_io *)event->watcher, event_callback, fd, events); } while(0)
#define event_timer_init(event,after,repeat) \
	do{ assert(event->watcher); ev_timer_init((ev_timer *)event->watcher, event_callback, after, repeat); } while(0)
#define event_periodic_init(event, ofs, ival, rcb) \
	do{ assert(event->watcher); ev_periodic_init((ev_periodic *)event->watcher, event_callback, ofs, ival, rcb); } while(0)
#define event_signal_init(event, signum) \
	do{ assert(event->watcher); ev_signal_init((ev_signal *)event->watcher, event_callback, signum); } while(0)
#define event_child_init(event, pid, trace) \
	do{ assert(event->watcher); ev_child_init((ev_child *)event->watcher, event_callback, pid, trace); } while(0)
#define event_stat_init(event, path, interval) \
	do{ assert(event->watcher); ev_stat_init((ev_stat *)event->watcher, event_callback, path, interval); } while(0)
#define event_idle_init(event) \
	do{ assert(event->watcher); ev_idle_init((ev_idle *)event->watcher, event_callback); } while(0)
#define event_prepare_init(event) \
	do{ assert(event->watcher); ev_prepare_init((ev_prepare *)event->watcher, event_callback); } while(0)
#define event_check_init(event) \
	do{ assert(event->watcher); ev_check_init((ev_check *)event->watcher, event_callback); } while(0)
#define event_embed_init(event, other) \
	do{ assert(event->watcher); ev_embed_init((ev_embed *)event->watcher, event_callback); } while(0)
#define event_fork_init(event) \
	do{ assert(event->watcher); ev_fork_init((ev_fork *)event->watcher, event_callback); } while(0)
#define event_cleanup_init(event) \
	do{ assert(event->watcher); ev_cleanup_init((ev_cleanup *)event->watcher, event_callback); } while(0)
#define event_async_init(event) \
	do{ assert(event->watcher); ev_async_init((ev_async *)event, event_callback); }while(0)

#define event_is_pending(event_object)  ev_is_pending(event_object->watcher)
#define event_is_active(event_object) ev_is_active(event_object->watcher)

#define event_priority(event)  ev_priority(event->watcher)
#define event_set_priority(event, pri)  ev_set_priority(event->watcher, pri)

#define event_periodic_at(event)  ev_periodic_at((ev_periodic *)event->watcher)

#define event_feed_event(loop_obj, event, revents) \
	ev_feed_event(loop_obj->loop, event->watcher, revents)
#define event_feed_fd_event(loop_obj, fd, revents) \
	ev_feed_fd_event(loop_obj->loop, fd, revents)
#define event_feed_signal_event(loop_obj, signum) \
	ev_feed_signal_event(loop_obj->loop, signum)
#define event_invoke(loop_obj, event, revents) \
	ev_invoke(loop_obj->loop, event->watcher, signum)
#define event_clear_pending(loop_obj, event) \
	ev_clear_pending(loop_obj->loop, event->watcher)
#define event_timer_again(event) \
	if(event->loop_obj) { ev_timer_again(event->loop_obj->loop, (ev_timer*)event->watcher); }
#define event_timer_remaining(event) \
	ev_timer_remaining(event->loop_obj->loop, (ev_timer *)event->watcher)
#define event_embed_sweep(event) \
	if(event->loop_obj) { ev_embed_sweep(event->loop_obj->loop, (ev_embed *) event->watcher); }
#define event_async_send(event) \
	if(event->loop_obj) { ev_async_send(event->loop_obj->loop, (ev_async *) event->watcher); }

/* "Returns" true if the event is associated with a loop */
#define event_has_loop(event_object)    (event_object->loop_obj)

/* Is true if event_object is registered with ev_loop */
#define event_in_loop(loop_obj, event_obj) \
	(event_obj->loop_obj && (event_obj->loop_obj->loop == loop_obj->loop))


#if NO_RETAIN == 1
#  define EVENT_INCREF(event_object)
#  define EVENT_DTOR(event_object)
#else
	#if LIBEV_DEBUG > 1
#       define EVENT_INCREF(event_object) \
			do { libev_printf("Increased refcount on Event 0x%lx to %d\n", \
			(unsigned long)((size_t) event_object->this),         \
			Z_REFCOUNT_P(event_object->this));   \
			zval_add_ref(&event_object->this); } while(0)
#       define EVENT_DTOR(event_object) \
			do { libev_printf("Decreasing refcount on Event 0x%lx to %d\n", \
			(unsigned long)((size_t) event_object->this),          \
			Z_REFCOUNT_P(event_object->this) - 1);                 \
			zval_ptr_dtor(&event_object->this); } while(0)
#   else
#       define EVENT_INCREF(event_object) \
			do { zval_add_ref(&event_object->this); } while(0)
#       define EVENT_DTOR(event_object) \
			do { zval_ptr_dtor(&event_object->this); } while(0)
#   endif
#endif

/* Protects event_objects from garbage collection by increasing their
   refcount and storing them in the event_loop_object's doubly-linked
   list, also sets event_object->loop_obj to event_loop_object */
#define EVENT_LOOP_REF_ADD(event_object, event_loop_object)  \
	if( ! event_has_loop(event_object)) {                    \
		assert(event_object->this);                          \
		assert( ! event_object->next);                       \
		assert( ! event_object->prev);                       \
		EVENT_INCREF(event_object);                          \
		event_object->loop_obj = event_loop_object;          \
		if( ! event_loop_object->events) {                   \
			event_object->next = NULL;                       \
			event_object->prev = NULL;                       \
			event_loop_object->events = event_object;        \
		}                                                    \
		else                                                 \
		{                                                    \
			event_object->next = event_loop_object->events;  \
			event_object->prev = NULL;                       \
			event_loop_object->events->prev = event_object;  \
			event_loop_object->events = event_object;        \
		}                                                    \
	}

/* Removes garbage collection protection by removing the event from the
   doubly linked list, nulling the event_object->loop_obj and finally calling
   zval_ptr_dtor */
#define EVENT_LOOP_REF_DEL(event_object)                                 \
	if(event_object->loop_obj) {                                         \
		assert( ! event_is_active(event_object));                        \
		assert( ! event_is_pending(event_object));                       \
		if(event_object->next)                                           \
		{                                                                \
			if(event_object->prev)                                       \
			{                                                            \
				/* Middle of the doubly-linked list */                   \
				event_object->prev->next = event_object->next;           \
				event_object->next->prev = event_object->prev;           \
			}                                                            \
			else                                                         \
			{                                                            \
				/* First of the doubly-linked list */                    \
				assert(event_object->loop_obj->events);                  \
				event_object->loop_obj->events = event_object->next;     \
				event_object->next->prev = NULL;                         \
			}                                                            \
		}                                                                \
		else if(event_object->prev)                                      \
		{                                                                \
			/* Last of the doubly-linked list */                         \
			assert(event_object->prev->next);                            \
			event_object->prev->next = NULL;                             \
		}                                                                \
		else                                                             \
		{                                                                \
			/* Only elment of the doubly-linked list */                  \
			assert(event_object->loop_obj->events);                      \
			event_object->loop_obj->events = NULL;                       \
		}                                                                \
		event_object->next     = NULL;                                   \
		event_object->prev     = NULL;                                   \
		event_object->loop_obj = NULL;                                   \
		EVENT_DTOR(event_object);                                        \
	}


#define EVENT_WATCHER_ACTION(event_object, loop_obj, action, type)         \
	if(instance_of_class(event_object->std.ce, type##_event_ce))           \
	{                                                                      \
		IF_DEBUG(libev_printf("Calling ev_" #type "_" #action "\n"));      \
		ev_##type##_##action(loop_obj->loop, (ev_##type *)event_object->watcher); \
	}

#define EVENT_STOP(event)                                              \
	if(event_has_loop(event) && (event_is_active(event) || event_is_pending(event))) { \
		EVENT_WATCHER_ACTION(event, event->loop_obj, stop, io)             \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, timer)     \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, periodic)  \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, signal)    \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, child)     \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, stat)      \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, idle)      \
		else EVENT_WATCHER_ACTION(event, event->loop_obj, stop, async)     \
	}


#endif /* PHP_LIBEV_H */