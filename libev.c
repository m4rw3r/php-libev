
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

/* Debug-level, 1 = assert, 2 = assert + debug messages */
#define LIBEV_DEBUG 0

#if LIBEV_DEBUG == 2
#  define libev_printf(...) php_printf("phplibev: " __VA_ARGS__)
#  define IF_DEBUG(x) x
#else
#  define IF_DEBUG(x)
#  define libev_printf(...)
#endif

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "php_streams.h"
#include "php_network.h"
#include "ext/sockets/php_sockets.h"
#include "php_libev.h"

#include <ev.h>
#include <signal.h>

/* Override PHP's default debugging behaviour
   if we only want to debug this extension */
#if LIBEV_DEBUG > 0
#  undef NDEBUG
#endif
#include <assert.h>

#ifdef COMPILE_DL_LIBEV
ZEND_GET_MODULE(libev)
#endif


/* Defining class API */
zend_class_entry *event_ce;
zend_class_entry *io_event_ce;
zend_class_entry *timer_event_ce;
zend_class_entry *periodic_event_ce;
zend_class_entry *signal_event_ce;
zend_class_entry *child_event_ce;
zend_class_entry *stat_event_ce;
zend_class_entry *idle_event_ce;
zend_class_entry *event_loop_ce;


zend_object_handlers event_object_handlers;
zend_object_handlers stat_event_object_handlers;
zend_object_handlers event_loop_object_handlers;

struct _event_loop_object;

typedef struct _event_object {
	zend_object    std;
	ev_watcher     *watcher;
	zval           *this;     /* No need to free *object, PHP does it */
	zval           *callback;
	struct _event_loop_object *evloop;
	struct _event_object   *prev;     /* Part of events doubly linked list on event_loop_object */
	struct _event_object   *next;     /* Part of events doubly linked list on event_loop_object */
} event_object;

typedef event_object stat_event_object;

typedef struct _event_loop_object {
	zend_object    std;
	struct ev_loop *loop;
	event_object   *events; /* Head of the doubly-linked list of associated events */
} event_loop_object;

/* The object containing ev_default_loop, managed by EventLoop::getDefaultLoop() */
zval *default_event_loop_object = NULL;


static int php_event_stop(event_object *obj)
{
	if(obj->watcher && EVENT_HAS_LOOP(obj) && (EVENT_IS_ACTIVE(obj) || EVENT_IS_PENDING(obj)))
	{
		EV_WATCHER_ACTION(obj, obj->evloop, stop, io)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, timer)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, periodic)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, signal)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, child)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, stat)
		else EV_WATCHER_ACTION(obj, obj->evloop, stop, idle)
		
		LOOP_REF_DEL(obj);
		
		return 1;
	}
	
	return 0;
}


#define CREATE_HANDLER(name) zend_object_value name##_create_handler(zend_class_entry *type TSRMLS_DC)\
{                                                                                                \
	IF_DEBUG(libev_printf("Allocating " #name "_object..."));                                    \
	                                                                                             \
	zval *tmp;                                                                                   \
	zend_object_value retval;                                                                    \
	                                                                                             \
	name##_object *obj = emalloc(sizeof(name##_object));                                         \
	memset(obj, 0, sizeof(name##_object));                                                       \
	obj->std.ce = type;                                                                          \
	                                                                                             \
	ALLOC_HASHTABLE(obj->std.properties);                                                        \
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);                              \
	zend_hash_copy(obj->std.properties, &type->default_properties,                               \
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));                       \
	                                                                                             \
	retval.handle = zend_objects_store_put(obj, NULL, name##_free_storage, NULL TSRMLS_CC);      \
	retval.handlers = &name##_object_handlers;                                                   \
	                                                                                             \
	IF_DEBUG(php_printf("done\n"));                                                              \
	                                                                                             \
	return retval;                                                                               \
}

#define FREE_STORAGE(name, code) void name##_free_storage(void *object TSRMLS_DC) \
{                                                                                 \
	IF_DEBUG(libev_printf("Freeing " #name "_object..."));                        \
	                                                                              \
	zval *events;                                                                 \
	name##_object *obj = (name##_object *) object;                                \
	                                                                              \
	zend_hash_destroy(obj->std.properties);                                       \
	FREE_HASHTABLE(obj->std.properties);                                          \
	                                                                              \
	code                                                                          \
	                                                                              \
	efree(obj);                                                                   \
	                                                                              \
	IF_DEBUG(php_printf("done\n"));                                               \
}

FREE_STORAGE(event,
	
	assert(obj->callback);
	assert(obj->watcher);
	
	if(obj->evloop)
	{
		IF_DEBUG(php_printf(" WARNING freeing active: %d, pending: %d, refcount: %d with evloop link ",
			EVENT_IS_ACTIVE(obj), EVENT_IS_PENDING(obj), Z_REFCOUNT_P(obj->this)));
		/* TODO: Stacktrace PHP, and see why obj->this is getting freed despite
		         being attached to an EventLoop (refcount != 0) */
		php_event_stop(obj);
	}
	
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	if(obj->watcher)
	{
		efree(obj->watcher);
	}
	
	/* No need to free obj->this, it is already done */
	IF_DEBUG(php_printf(" freed event 0x%lx ", (size_t) obj->this));
)

FREE_STORAGE(stat_event,
	
	assert(obj->callback);
	assert(obj->watcher);
	
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	if(obj->watcher)
	{
		/* ev_stat has a pointer to a PHP allocated string, free it */
		efree(((ev_stat *)obj->watcher)->path);
		efree(obj->watcher);
	}
	
	/* No need to free obj->this, it is already done */
	IF_DEBUG(libev_printf("Freed event 0x%lx\n", (size_t) obj->this));
)

FREE_STORAGE(event_loop,
	
	assert(obj->loop);
	
	if(obj->events)
	{
		/* Stop and free all in the linked list */
		event_object *ev = obj->events;
		event_object *tmp;
		
		while(ev)
		{
			IF_DEBUG(php_printf("Freeing event 0x%lx\n", (size_t) ev->this));
			assert(ev->this);
			assert(ev->evloop);
			IF_DEBUG(libev_printf("Decreasing refcount on Event 0x%lx to %d\n",
				(unsigned long) (size_t) ev->this,
				Z_REFCOUNT_P(ev->this) - 1));
			
			if(ev->evloop)
			{
				EV_WATCHER_ACTION(ev, ev->evloop, stop, io)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, timer)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, periodic)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, signal)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, child)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, stat)
				else EV_WATCHER_ACTION(ev, ev->evloop, stop, idle)
			}
			
			tmp = ev->next;
			
			/* Reset the struct */
			ev->next   = NULL;
			ev->prev   = NULL;
			ev->evloop = NULL;
			
			/* No need to efree ev, it has been freed by the ev->this destructor */
			ev = tmp;
		}
	}
	
	if(obj->loop)
	{
		/* If it is the default loop, we need to free its "singleton-zval" as we
		   already are in the shutdown phase (so no risk of freeing the default
		   loop too early) and PHP will not free our zval automatically (ie.
		   its refcount is 1, yet we still happen here, meaning PHP is cleaing
		   all objects) */
		if(ev_is_default_loop(obj->loop))
		{
			assert(default_event_loop_object);
			assert(Z_REFCOUNT_P(default_event_loop_object) == 1);
			
			IF_DEBUG(php_printf(" freeing default loop "));
			
			zval_ptr_dtor(&default_event_loop_object);
		}
		
		ev_loop_destroy(obj->loop);
	}
)

CREATE_HANDLER(event)
CREATE_HANDLER(stat_event)
CREATE_HANDLER(event_loop)

/**
 * Generic event callback which will call the associated PHP callback.
 */
static void event_callback(struct ev_loop *loop, ev_watcher *w, int revents)
{
	/* Note: loop might be null pointer because of Event::invoke() */
	IF_DEBUG(libev_printf("Calling PHP callback\n"));
	
	assert(w->data);
	
	zval retval;
	zval *args[1];
	event_object *event = (event_object *) w->data;
	
	/* Pass the Event object to the callback */
	args[0] = event->this;
	zval_add_ref(&args[0]);
	
	assert(event->callback);
	
	if(call_user_function(EG(function_table), NULL, event->callback, &retval, 1, args TSRMLS_CC) == SUCCESS)
	{
		zval_dtor(&retval);
	}
	
	zval_ptr_dtor(&(args[0]));
	
	if(loop && event->evloop && ! ev_is_active(w) && ! ev_is_pending(w) )
	{
		LOOP_REF_DEL(event);
	}
}

#include "Events.c"
#include "EventLoop.c"

static const function_entry event_methods[] = {
	/* Abstract __construct makes the class abstract */
	ZEND_ME(Event, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(Event, isActive, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, isPending, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, setCallback, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, invoke, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, stop, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry io_event_methods[] = {
	ZEND_ME(IOEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	{NULL, NULL, NULL}
};

static const function_entry timer_event_methods[] = {
	ZEND_ME(TimerEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(TimerEvent, getRepeat, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, setRepeat, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, getAfter, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, again, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, getRemaining, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry periodic_event_methods[] = {
	ZEND_ME(PeriodicEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(PeriodicEvent, getTime, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(PeriodicEvent, getOffset, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(PeriodicEvent, getInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(PeriodicEvent, setInterval, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry signal_event_methods[] = {
	ZEND_ME(SignalEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	{NULL, NULL, NULL}
};

static const function_entry child_event_methods[] = {
	ZEND_ME(ChildEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(ChildEvent, getPid, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(ChildEvent, getRPid, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(ChildEvent, getRStatus, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry stat_event_methods[] = {
	ZEND_ME(StatEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(StatEvent, getPath, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(StatEvent, getInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(StatEvent, getAttr, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(StatEvent, getPrev, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry idle_event_methods[] = {
	ZEND_ME(IdleEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	{NULL, NULL, NULL}
};

static const function_entry event_loop_methods[] = {
	ZEND_ME(EventLoop, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(EventLoop, getDefaultLoop, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC | ZEND_ACC_FINAL)
	ZEND_ME(EventLoop, notifyFork, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, isDefaultLoop, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getIteration, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getDepth, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getBackend, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, now, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, updateNow, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, suspend, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, resume, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, run, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, breakLoop, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, ref, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, unref, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, setIOCollectInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, setTimeoutCollectInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getPendingCount, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, add, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, remove, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, clearPending, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, feedEvent, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getEvents, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static void *libevrealloc(void *ptr, size_t size)
{
	/* php_printf("realloc(0x%lx, %ld)\n", (size_t) ptr, size); */
	if(size) { return erealloc(ptr, size); } else if(ptr) { efree(ptr); } return 0;
}

PHP_MINIT_FUNCTION(libev)
{
	/* Change the allocator for libev */
	ev_set_allocator(libevrealloc);
	
	zend_class_entry ce;
	/* Init generic object handlers for Event objects, prevent clone */
	memcpy(&event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	event_object_handlers.clone_obj = NULL;
	/* Same for StatEvent */
	memcpy(&stat_event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	stat_event_object_handlers.clone_obj = NULL;
	
	
	/* libev\Event abstract */
	INIT_CLASS_ENTRY(ce, "libev\\Event", event_methods);
	event_ce = zend_register_internal_class(&ce TSRMLS_CC);
	event_ce->create_object = event_create_handler;
	
	
	/* libev\IOEvent */
	INIT_CLASS_ENTRY(ce, "libev\\IOEvent", io_event_methods);
	io_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	/* Override default object creation */
	io_event_ce->create_object = event_create_handler;
	/* Constants */
	zend_declare_class_constant_long(io_event_ce, "READ", sizeof("READ") - 1, EV_READ TSRMLS_CC);
	zend_declare_class_constant_long(io_event_ce, "WRITE", sizeof("WRITE") - 1, EV_WRITE TSRMLS_CC);
	
	
	/* libev\TimerEvent */
	INIT_CLASS_ENTRY(ce, "libev\\TimerEvent", timer_event_methods);
	timer_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	timer_event_ce->create_object = event_create_handler;
	
	
	/* libev\PeriodicEvent */
	INIT_CLASS_ENTRY(ce, "libev\\PeriodicEvent", periodic_event_methods);
	periodic_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	periodic_event_ce->create_object = event_create_handler;
	
	
	/* libev\SignalEvent */
	INIT_CLASS_ENTRY(ce, "libev\\SignalEvent", signal_event_methods);
	signal_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	signal_event_ce->create_object = event_create_handler;
	/* Constants */
#   define signal_constant(name)  zend_declare_class_constant_long(signal_event_ce, #name, sizeof(#name) - 1, (long) name TSRMLS_CC)
	signal_constant(SIGHUP);
	signal_constant(SIGINT);
	signal_constant(SIGQUIT);
	signal_constant(SIGILL);
	signal_constant(SIGTRAP);
	signal_constant(SIGABRT);
#   ifdef SIGIOT
		signal_constant(SIGIOT);
#   endif
	signal_constant(SIGBUS);
	signal_constant(SIGFPE);
	signal_constant(SIGKILL);
	signal_constant(SIGUSR1);
	signal_constant(SIGSEGV);
	signal_constant(SIGUSR2);
	signal_constant(SIGPIPE);
	signal_constant(SIGALRM);
	signal_constant(SIGTERM);
#   ifdef SIGSTKFLT
		signal_constant(SIGSTKFLT);
#   endif 
#   ifdef SIGCLD
		signal_constant(SIGCLD);
#   endif
#   ifdef SIGCHLD
		signal_constant(SIGCHLD);
#   endif
	signal_constant(SIGCONT);
	signal_constant(SIGSTOP);
	signal_constant(SIGTSTP);
	signal_constant(SIGTTIN);
	signal_constant(SIGTTOU);
	signal_constant(SIGURG);
	signal_constant(SIGXCPU);
	signal_constant(SIGXFSZ);
	signal_constant(SIGVTALRM);
	signal_constant(SIGPROF);
	signal_constant(SIGWINCH);
#   ifdef SIGPOLL
		signal_constant(SIGPOLL);
#   endif
	signal_constant(SIGIO);
#   ifdef SIGPWR
		signal_constant(SIGPWR);
#   endif
#   ifdef SIGSYS
		signal_constant(SIGSYS);
#   endif
#   undef signal_constant
	
	/* libev\ChildEvent */
	INIT_CLASS_ENTRY(ce, "libev\\ChildEvent", child_event_methods);
	child_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	child_event_ce->create_object = event_create_handler;
	
	/* libev\StatEvent */
	INIT_CLASS_ENTRY(ce, "libev\\StatEvent", stat_event_methods);
	stat_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	stat_event_ce->create_object = stat_event_create_handler;
	
	/* libev\IdleEvent */
	INIT_CLASS_ENTRY(ce, "libev\\IdleEvent", idle_event_methods);
	idle_event_ce = zend_register_internal_class_ex(&ce, event_ce, NULL TSRMLS_CC);
	idle_event_ce->create_object = event_create_handler;
	
	
	/* libev\EventLoop */
	INIT_CLASS_ENTRY(ce, "libev\\EventLoop", event_loop_methods);
	event_loop_ce = zend_register_internal_class(&ce TSRMLS_CC);
	
	/* Override default object handlers so we can use custom struct */
	event_loop_ce->create_object = event_loop_create_handler;
	memcpy(&event_loop_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	event_loop_object_handlers.clone_obj = NULL;
	
	/* EventLoop class constants */
	zend_declare_class_constant_long(event_loop_ce, "RUN_NOWAIT", sizeof("RUN_NOWAIT") - 1, (long) EVRUN_NOWAIT TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "RUN_ONCE", sizeof("RUN_ONCE") - 1, (long) EVRUN_ONCE TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "BREAK_ONE", sizeof("BREAK_ONE") - 1, (long) EVBREAK_ONE TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "BREAK_ALL", sizeof("BREAK_ALL") - 1, (long) EVBREAK_ALL TSRMLS_CC);
#   define backend_constant(name) \
	zend_declare_class_constant_long(event_loop_ce, "BACKEND_" #name, sizeof("BACKEND_" #name) - 1, (long) EVBACKEND_##name TSRMLS_CC)
	backend_constant(SELECT);
	backend_constant(POLL);
	backend_constant(EPOLL);
	backend_constant(KQUEUE);
	backend_constant(DEVPOLL);
	backend_constant(PORT);
	backend_constant(ALL);
#   undef backend_constant
	
	return SUCCESS;
}

static PHP_MINFO_FUNCTION(libev)
{
	char version[64];
	
	php_info_print_table_start();
	php_info_print_table_row(2, "Extension version", PHP_LIBEV_EXTVER);
	
	snprintf(version, sizeof(version) -1, "%d.%d", ev_version_major(), ev_version_minor());
	php_info_print_table_row(2, "libev version", version);
	
	php_info_print_table_end();
}

static const zend_module_dep libev_deps[] = {
	ZEND_MOD_OPTIONAL("sockets")
	{NULL, NULL, NULL}
};

zend_module_entry libev_module_entry = {
	STANDARD_MODULE_HEADER_EX,
	NULL,
	libev_deps,
	PHP_LIBEV_EXTNAME,
	NULL,                  /* Functions */
	PHP_MINIT(libev),
	NULL,                  /* MSHUTDOWN */
	NULL,                  /* RINIT */
	NULL,                  /* RSHUTDOWN */
	PHP_MINFO(libev),      /* MINFO */
	PHP_LIBEV_EXTVER,
	STANDARD_MODULE_PROPERTIES
};


