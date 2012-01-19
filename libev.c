
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

#define LIBEV_DEBUG 0

#if LIBEV_DEBUG
#  define libev_printf(...) php_printf("phplibev: " __VA_ARGS__)
#  define IF_DEBUG(x) x
#else
#  define IF_DEBUG(x)
#  define libev_printf(...)
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
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
#if LIBEV_DEBUG
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
zend_class_entry *event_loop_ce;


zend_object_handlers event_object_handlers;
zend_object_handlers event_loop_object_handlers;


typedef struct event_object {
	zend_object std;
	ev_watcher *watcher;
	zval *callback;
} event_object;

typedef struct event_loop_object {
	zend_object std;
	struct ev_loop *loop;
	zval *events;
} event_loop_object;

/* The object containing ev_default_loop, managed by EventLoop::getDefaultLoop() */
zval *default_event_loop_object;


void event_free_storage(void *object TSRMLS_DC)
{
	IF_DEBUG(libev_printf("Freeing event_object..."));
	
	event_object *obj = (event_object *) object;
	
	zend_hash_destroy(obj->std.properties);
	FREE_HASHTABLE(obj->std.properties);
	
	assert(obj->callback);
	
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		efree(obj->watcher);
	}
	
	efree(obj);
	
	IF_DEBUG(php_printf("done\n"));
}

zend_object_value event_create_handler(zend_class_entry *type TSRMLS_DC)
{
	IF_DEBUG(libev_printf("Allocating event_object..."));
	
	zval *tmp;
	zend_object_value retval;
	
	event_object *obj = emalloc(sizeof(event_object));
	memset(obj, 0, sizeof(event_object));
	obj->std.ce = type;
	
	ALLOC_HASHTABLE(obj->std.properties);
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(obj->std.properties, &type->default_properties,
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
	
	retval.handle = zend_objects_store_put(obj, NULL, event_free_storage, NULL TSRMLS_CC);
	retval.handlers = &event_object_handlers;
	
	IF_DEBUG(php_printf("done\n"));
	
	return retval;
}

void event_loop_free_storage(void *object TSRMLS_DC)
{
	IF_DEBUG(libev_printf("Freeing event_loop_object..."));
	
	event_loop_object *obj = (event_loop_object *) object;
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_loop_destroy(obj->loop);
	}
	
	assert(obj->events);
	
	zval_ptr_dtor(&obj->events);
	
	zend_hash_destroy(obj->std.properties);
	FREE_HASHTABLE(obj->std.properties);
	
	efree(obj);
	
	IF_DEBUG(php_printf("done\n"));
}

zend_object_value event_loop_create_handler(zend_class_entry *type TSRMLS_DC)
{
	IF_DEBUG(libev_printf("Allocating event_loop_object..."));
	
	zval *tmp;
	zend_object_value retval;
	
	event_loop_object *obj = emalloc(sizeof(event_loop_object));
	memset(obj, 0, sizeof(event_loop_object));
	obj->std.ce = type;
	
	ALLOC_HASHTABLE(obj->std.properties);
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(obj->std.properties, &type->default_properties,
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
	
	/* Allocate internal hash for the associated Event objects */
	MAKE_STD_ZVAL(obj->events);
	array_init(obj->events);
	
	retval.handle = zend_objects_store_put(obj, NULL, event_loop_free_storage, NULL TSRMLS_CC);
	retval.handlers = &event_loop_object_handlers;
	
	IF_DEBUG(php_printf("done\n"));
	
	return retval;
}

/**
 * Generic event callback which will call the associated PHP callback.
 */
static void event_callback(struct ev_loop *loop, ev_timer *w, int revents)
{
	IF_DEBUG(libev_printf("Calling PHP callback\n"));
	
	zval retval;
	
	assert(w->data);
	assert(((event_object *)w->data)->callback);
	
	if(((event_object *)w->data)->callback)
	{
		if(call_user_function(EG(function_table), NULL, ((event_object *)w->data)->callback, &retval, 0, NULL TSRMLS_CC) == SUCCESS)
		{
			zval_dtor(&retval);
		}
	}
}


/**
 * Empty abstract constructor for Event.
 */
PHP_METHOD(Event, __construct)
{
	/* Intentionally left empty */
}

/**
 * Returns true if the event is active, ie. associated with an event loop.
 * 
 * @return boolean
 * @return null  If the watcher is uninitialized
 */
PHP_METHOD(Event, isActive)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_BOOL(ev_is_active(obj->watcher));
	}
	
	RETURN_NULL();
}

/**
 * Returns true if the event watcher is pending (ie. it has outstanding events but
 * the callback has not been called yet).
 * 
 * TODO: Investigate what happens if we free obj->watcher while is_pending is true,
 *       the lbev manual says it is a bad idea
 * 
 * @return boolean
 * @return null  If the watcher is uninitialized
 */
PHP_METHOD(Event, isPending)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_BOOL(ev_is_pending(obj->watcher));
	}
	
	RETURN_NULL();
}

/**
 * Replaces the PHP callback on an event.
 * 
 * @return void
 */
PHP_METHOD(Event, setCallback)
{
	event_object *obj;
	zval *zcallback = NULL;
	char *func_name;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zcallback) != SUCCESS) {
		return;
	}
	
	if( ! zend_is_callable(zcallback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->callback);
	
	/* Destroy existing callback reference */
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	zval_add_ref(&zcallback);
	obj->callback = zcallback;
}


/**
 * Creates an IO event which will trigger when there is data to read and/or data to write
 * on the supplied stream.
 * 
 * @param  int  either IOEvent::READ and/or IOEvent::WRITE depending on type of event
 * @param  resource  the PHP stream to watch
 * @param  callback  the PHP callback to call
 */
PHP_METHOD(IOEvent, __construct)
{
	long events;
	php_socket_t file_desc;
	zval **fd, *zcallback = NULL;
	char *func_name;
	event_object *obj;
	php_stream *stream;
	php_socket *php_sock;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lZz", &events, &fd, &zcallback) != SUCCESS) {
		return;
	}
	
	/* Check if we have the correct flags */
	if( ! (events & (EV_READ | EV_WRITE)))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "events parameter must be at least one of IOEvent::READ or IOEvent::WRITE");
		RETURN_FALSE;
	}
	
	/* Attempt to get the file descriptor from the stream */
	if(ZEND_FETCH_RESOURCE_NO_RETURN(stream, php_stream*, fd, -1, NULL, php_file_le_stream()))
	{
		if(php_stream_cast(stream, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void*)&file_desc, 1) != SUCCESS || file_desc < 0)
		{
			RETURN_FALSE;
		}
	}
	else
	{
		if(ZEND_FETCH_RESOURCE_NO_RETURN(php_sock, php_socket *, fd, -1, NULL, php_sockets_le_socket()))
		{
			file_desc = php_sock->bsd_socket;
		}
		else
		{
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "fd argument must be either valid PHP stream or valid PHP socket resource");
			
			RETURN_FALSE;
		}
	}
	
	if( ! zend_is_callable(zcallback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	zval_add_ref(&zcallback);
	obj->callback = zcallback;
	
	obj->watcher = emalloc(sizeof(ev_io));
	obj->watcher->data = obj;
	ev_io_init((ev_io *)obj->watcher, event_callback, (int)file_desc, (int)events);
}


/**
 * Creates a timer event which will occur approximately after $after seconds
 * and after that will repeat with an approximate interval of $repeat.
 * 
 * @param  callback
 * @param  double    Time before first triggering, seconds
 * @param  double    Time between repeats, seconds, Default: 0 = no repeat
 */
PHP_METHOD(TimerEvent, __construct)
{
	double after;
	double repeat = 0.;
	zval *callback;
	char *func_name;
	event_object *obj;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zd|d", &callback, &after, &repeat) != SUCCESS) {
		return;
	}
	
	if( ! zend_is_callable(callback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	zval_add_ref(&callback);
	obj->callback = callback;
	
	obj->watcher = emalloc(sizeof(ev_timer));
	obj->watcher->data = obj;
	ev_timer_init((ev_timer *)obj->watcher, event_callback, after, repeat);
}

/**
 * Returns the seconds between event triggering.
 * 
 * @return double
 * @return false   If the event has not been initialized
 */
PHP_METHOD(TimerEvent, getRepeat)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_timer *)obj->watcher)->repeat);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the time from the loop start until the first triggering of this TimerEvent.
 * 
 * @return double
 * @return false   If the event has not been initialized
 */
PHP_METHOD(TimerEvent, getAfter)
{
	/* TODO: Not sure if this is a good idea, ev_timer->at is marked as private in ev.h */
	
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_timer *)obj->watcher)->at);
	}
	
	RETURN_BOOL(0);
}
	

/* TODO: implement support for ev_timer_again(loop, ev_timer*) ? */
/* TODO: implement support for ev_timer_remaining(loop, ev_timer*) ? */

/**
 * Schedules an event (or a repeating series of events) at a specific point
 * in time.
 * 
 * There are two variants of PeriodicEvents:
 * * Absolute timer (offset = absolute time, interval = 0)
 *   In this configuration the watcher triggers an event after the wall clock
 *   time offset has passed. It will not repeat and will not adjust when a time
 *   jump occurs, that is, if it is to be run at January 1st 2011 then it will be
 *   stopped and invoked when the system clock reaches or surpasses this point in time.
 * 
 * * Repeating interval timer (offset = offset within interval, interval > 0)
 *   In this mode the watcher will always be scheduled to time out at the next
 *   offset + N * interval time (for some integer N, which can also be negative)
 *   and then repeat, regardless of any time jumps. The offset argument is merely
 *   an offset into the interval periods.
 * 
 * @param  callback
 * @param  double  The offset value
 * @param  double  the interval value, Default = 0, no repeat
 */
PHP_METHOD(PeriodicEvent, __construct)
{
	double after;
	double repeat = 0.;
	zval *callback;
	char *func_name;
	event_object *obj;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zd|d", &callback, &after, &repeat) != SUCCESS) {
		return;
	}

	if( ! zend_is_callable(callback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);

	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	zval_add_ref(&callback);
	obj->callback = callback;

	obj->watcher = emalloc(sizeof(ev_periodic));
	obj->watcher->data = obj;
	ev_periodic_init((ev_periodic *)obj->watcher, event_callback, after, repeat, 0);
}

/**
 * Returns the time for the next trigger of the event, seconds.
 * 
 * @return float
 */
PHP_METHOD(PeriodicEvent, getTime)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(ev_periodic_at((ev_periodic *)obj->watcher));
	}
	
	RETURN_BOOL(0);
}

/**
 * When repeating, returns the offset, otherwise it returns the absolute time for
 * the event trigger.
 * 
 * @return float
 */
PHP_METHOD(PeriodicEvent, getOffset)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_periodic *)obj->watcher)->offset);
	}
	
	RETURN_BOOL(0);
}

/**
 * When repeating, returns the current interval value.
 * 
 * @return float
 */
PHP_METHOD(PeriodicEvent, getInterval)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_periodic *)obj->watcher)->interval);
	}
	
	RETURN_BOOL(0);
}

/**
 * Sets the interval value, changes only take effect when the event has fired.
 * 
 * @return boolean
 */
PHP_METHOD(PeriodicEvent, setInterval)
{
	double interval;
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "d", &interval) != SUCCESS) {
		return;
	}
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		((ev_periodic *)obj->watcher)->interval = interval;
		
		RETURN_BOOL(1)
	}
	
	RETURN_BOOL(0);
}

/* TODO: Implement ev_periodic_again(loop, ev_periodic *) ? */


PHP_METHOD(SignalEvent, __construct)
{
	long signo;
	zval *callback;
	char *func_name;
	event_object *obj;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lz", &signo, &callback) != SUCCESS) {
		return;
	}

	if( ! zend_is_callable(callback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	zval_add_ref(&callback);
	obj->callback = callback;

	obj->watcher = emalloc(sizeof(ev_signal));
	obj->watcher->data = obj;
	ev_signal_init((ev_signal *)obj->watcher, event_callback, (int) signo);
}


/**
 * This event will be triggered on child status changes.
 * 
 * NOTE: Must be attached to the default loop (ie. the instance from
 * EventLoop::getDefaultLoop())
 * 
 * @param  callback
 * @param  int   PID, 0 if all children
 * @param  boolean  If to also trigger on suspend/continue events and not just termination
 */
PHP_METHOD(ChildEvent, __construct)
{
	long pid;
	zend_bool trace = 0;
	zval *callback;
	char *func_name;
	event_object *obj;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "zl|b", &callback, &pid, &trace) != SUCCESS) {
		return;
	}

	if( ! zend_is_callable(callback, 0, &func_name TSRMLS_CC))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "'%s' is not a valid callback", func_name);
		efree(func_name);
		RETURN_FALSE;
	}
	efree(func_name);
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	zval_add_ref(&callback);
	obj->callback = callback;
	
	obj->watcher = emalloc(sizeof(ev_child));
	obj->watcher->data = obj;
	ev_child_init((ev_child *)obj->watcher, event_callback, (int)pid, (int)trace);
}

/**
 * Returns the PID this event was registered for.
 * 
 * @return int
 */
PHP_METHOD(ChildEvent, getPid)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_LONG(((ev_child *)obj->watcher)->pid);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the PID for the last child triggering this event.
 * 
 * @return int
 */
PHP_METHOD(ChildEvent, getRPid)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_LONG(((ev_child *)obj->watcher)->rpid);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the exit/trace status (see waitpid and sys/wait.h) caused by the child
 * ChildEvent::getRPid().
 * 
 * @return int
 */
PHP_METHOD(ChildEvent, getRStatus)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_LONG(((ev_child *)obj->watcher)->rstatus);
	}
	
	RETURN_BOOL(0);
}


/**
 * Normal constructor for EventLoop instance.
 */
PHP_METHOD(EventLoop, __construct)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	/* TODO: Do we need to be able to change the parameter to ev_loop_new() here? */
	obj->loop = ev_loop_new(EVFLAG_AUTO);
	
	IF_DEBUG(ev_verify(obj->loop));
}

/**
 * Returns the default event loop object, this object is a global singleton
 * and it is not recommended to use it unless you require ChildEvent watchers
 * as they can only be attached to the default loop.
 * 
 * @return EventLoop
 */
PHP_METHOD(EventLoop, getDefaultLoop)
{
	/* Singleton */
	if( ! default_event_loop_object)
	{
		/* TODO: How do we deal with memory management here?
		   Do we need to destroy the zval in some kind of extension shutdown? */
		ALLOC_INIT_ZVAL(default_event_loop_object);
		
		/* Create object without calling constructor, we now have an EventLoop missing the ev_loop */
		if(object_init_ex(default_event_loop_object, event_loop_ce) != SUCCESS) {
			/* TODO: Error handling */
			RETURN_BOOL(0);
		
			return;
		}
		
		event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(default_event_loop_object TSRMLS_CC);
		
		assert( ! obj->loop);
		
		obj->loop = ev_default_loop(EVFLAG_AUTO);
		
		IF_DEBUG(ev_verify(obj->loop));
		IF_DEBUG(libev_printf("Created default_event_loop_object\n"));
	}
	
	/* Return copy, no destruct on our local zval */
	RETURN_ZVAL(default_event_loop_object, 1, 0);
}

/**
 * Notifies libev that a fork might have been done and forces it
 * to reinitialize kernel state where needed on the next loop iteration.
 * 
 * @return boolean  false if object has not been initialized
 */
PHP_METHOD(EventLoop, notifyFork)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_loop_fork(obj->loop);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the current loop iteration.
 * 
 * @return int
 * @return false  if object is not initialized
 */
PHP_METHOD(EventLoop, getIteration)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_LONG(ev_iteration(obj->loop));
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the current nesting depth of event-loops.
 * 
 * @return int
 * @return false  if object is not initialized
 */
PHP_METHOD(EventLoop, getDepth)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_LONG(ev_depth(obj->loop));
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the time the current loop iteration received events.
 * Seconds in libev.
 * 
 * @return double
 * @return false  if object is not initialized
 */
PHP_METHOD(EventLoop, now)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_DOUBLE(ev_now(obj->loop));
	}
	
	RETURN_BOOL(0);
}

/**
 * Suspends the event loop, pausing all timers and delays processing of events.
 * 
 * NOTE: DO NOT CALL IF YOU HAVE CALLED EventLoop->suspend() ALREADY!
 * 
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, suspend)
{
	/* TODO: Implement a check for if we already have suspended the eventloop? */
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_suspend(obj->loop);
		
		RETURN_BOOL(true);
	}
	
	RETURN_BOOL(0);
}

/**
 * Resumes the event loop and all timers.
 * 
 * NOTE: DO NOT CALL UNLESS YOU HAVE CALLED EventLoop->suspend() first!
 * 
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, resume)
{
	/* TODO: Implement a check for it suspend has been called? */
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_resume(obj->loop);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Runs the event loop, processing all events, will block until EventLoop->break()
 * is called or no more events are associated with this loop by default.
 * 
 * @param  int  libev run flag
 *              * int(0)                 run() handles events until there are no events to handle
 *              * EventLoop::RUN_NOWAIT  run() looks for new events, handles them and
 *                then return after one iteration of the loop
 *              * EventLoop::RUN_ONCE    run() looks for new events (wait if necessary)
 *                and will handle those and any outstanding ones. It will block until
 *                at least one event has arrived and will return after one iteration of
 *                the loop
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, run)
{
	/* TODO: Implement support for ev_unref() which will make the EvenLoop ignore
	   the Event if it is the only active event */
	long how = 0;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &how) != SUCCESS) {
		return;
	}
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_run(obj->loop, (int)how);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Breaks the current event loop after it has processed all outstanding events.
 * 
 * @param  int  libev break flag
 *              * EventLoop::BREAK_ONE    will break the innermost loop, default behaviour
 *              * EventLoop::BREAK_ALL    will break all the currently running loops
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, breakLoop)
{
	long how = EVBREAK_ONE;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &how) != SUCCESS) {
		return;
	}
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_break(obj->loop, how);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Sets the time libev spends waiting for new IO events between loop iterations,
 * default is 0.
 * 
 * @param  double  time in seconds
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, setIOCollectInterval)
{
	double interval = 0;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "d", &interval) != SUCCESS) {
		return;
	}
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_set_io_collect_interval(obj->loop, interval);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Sets the time libev spends waiting for new timer events between loop iterations,
 * default 0.
 * 
 * @param  double  time in seconds
 * @return boolean
 */
PHP_METHOD(EventLoop, setTimeoutCollectInterval)
{
	double interval = 0;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "d", &interval) != SUCCESS) {
		return;
	}
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_set_timeout_collect_interval(obj->loop, interval);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the number of pending events.
 * 
 * @return int
 */
PHP_METHOD(EventLoop, getPendingCount)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_LONG(ev_pending_count(obj->loop));
	}
	
	RETURN_BOOL(0);
}

#define ev_watcher_action(action, type)     if(object_ce == type##_event_ce)    \
	{                                                                           \
		IF_DEBUG(libev_printf("Calling ev_" #type "_" #action "\n"));           \
		ev_##type##_##action(loop_obj->loop, (ev_##type *)event->watcher);      \
	}

/**
 * Adds the event to the event loop.
 * 
 * This method will increase the refcount on the supplied Event, protecting it
 * from garbage collection. Refcount will be decreased on remove or if the
 * EventLoop object is GCd.
 * 
 * @param  Event
 * @return boolean
 */
PHP_METHOD(EventLoop, add)
{
	zval *event_obj;
	event_object *event;
	zend_class_entry *object_ce;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	if(loop_obj->loop && event->watcher && ! ev_is_active(event->watcher))
	{
		object_ce = zend_get_class_entry(event_obj);
		
		ev_watcher_action(start, io)
		else ev_watcher_action(start, timer)
		else ev_watcher_action(start, periodic)
		else ev_watcher_action(start, signal)
		else if(object_ce == child_event_ce)
		{
			/* Special logic, ev_child can only be attached to the default loop */
			if( ! ev_is_default_loop(loop_obj->loop))
			{
				/* TODO: libev-specific exception class here */
				zend_throw_exception(NULL, "libev\\ChildEvent can only be added to the default event-loop", 1 TSRMLS_DC);
				
				return;
			}
			
			ev_child_start(loop_obj->loop, (ev_child *)event->watcher);
			IF_DEBUG(libev_printf("Calling ev_child_start\n"));
		}
		
		IF_DEBUG(libev_printf("preAdd refcount for Event %ld: %d\n", (size_t) event->watcher, Z_REFCOUNT_P(event_obj)));
		
		/* Apply GC protection for the Event */
		if(add_index_zval(loop_obj->events, (size_t) event->watcher, event_obj) == FAILURE)
		{
			IF_DEBUG(libev_printf("Could not add Event to internal hash\n"));
			assert(0);
			
			RETURN_BOOL(0);
		}
		/* Increase refcount because add_index_zval() does not */
		zval_add_ref(&event_obj);
		
		IF_DEBUG(libev_printf("postAdd refcount for Event %ld: %d\n", (size_t) event->watcher, Z_REFCOUNT_P(event_obj)));
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Removes the event from the event loop, will skip all pending events on it too.
 * 
 * @param  Event
 * @return boolean  False if the Event is not associated with this EventLoop,
 *                  can also be false if there is an error
 */
PHP_METHOD(EventLoop, remove)
{
	zval *event_obj;
	event_object *event;
	zend_class_entry *object_ce;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	if(loop_obj->loop && event->watcher && ev_is_active(event->watcher))
	{
		IF_DEBUG(libev_printf("preRemove refcount for Event %ld: %d\n", (size_t) event->watcher, Z_REFCOUNT_P(event_obj)));
		
		/* Check that the event is associated with us */
		if(zend_hash_index_exists(Z_ARRVAL_P(loop_obj->events), (size_t) event->watcher) == FAILURE)
		{
			IF_DEBUG(libev_printf("Event is not in this EventLoop's internal hash\n"));
			
			RETURN_BOOL(0);
		}
		
		object_ce = zend_get_class_entry(event_obj);
		
		ev_watcher_action(stop, io)
		else ev_watcher_action(stop, timer)
		else ev_watcher_action(stop, periodic)
		else ev_watcher_action(stop, signal)
		else ev_watcher_action(stop, child)
		
		/* Remove GC protection */
		/* For some reason does zend_hash_index_del() decrease the zval refcount
		   so no need to call zval_dtor */
		if(zend_hash_index_del(Z_ARRVAL_P(loop_obj->events), (size_t) event->watcher) == FAILURE)
		{
			IF_DEBUG(libev_printf("Failed to remove Event from EventLoop internal hash\n"));
			assert(0);
			
			RETURN_BOOL(0);
		}
		
		IF_DEBUG(libev_printf("postRemove refcount for Event %ld: %d\n", (size_t) event->watcher, Z_REFCOUNT_P(event_obj)));
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

#undef ev_watcher_action

/* TODO: Implement EventLoop::getEvents() or something like that? */


static const function_entry event_methods[] = {
	/* Abstract __construct makes the class abstract */
	ZEND_ME(Event, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_ABSTRACT)
	ZEND_ME(Event, isActive, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, isPending, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(Event, setCallback, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry io_event_methods[] = {
	ZEND_ME(IOEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	{NULL, NULL, NULL}
};

static const function_entry timer_event_methods[] = {
	ZEND_ME(TimerEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(TimerEvent, getRepeat, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, getAfter, NULL, ZEND_ACC_PUBLIC)
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

static const function_entry event_loop_methods[] = {
	ZEND_ME(EventLoop, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR | ZEND_ACC_FINAL)
	ZEND_ME(EventLoop, getDefaultLoop, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC | ZEND_ACC_FINAL)
	ZEND_ME(EventLoop, notifyFork, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getIteration, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getDepth, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, now, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, suspend, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, resume, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, run, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, breakLoop, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, setIOCollectInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, setTimeoutCollectInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getPendingCount, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, add, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, remove, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};


PHP_MINIT_FUNCTION(libev)
{
	zend_class_entry ce;
	/* Init generic object handlers for Event objects, prevent clone */
	memcpy(&event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	event_object_handlers.clone_obj = NULL;
	
	
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


