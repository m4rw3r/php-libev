
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
		IF_DEBUG(php_printf(" WARNING freeing active: %d, pending: %d with evloop link ",
			EVENT_IS_ACTIVE(obj), EVENT_IS_PENDING(obj)));
		/* TODO: Stacktrace PHP, and see why obj->this got a refcount of 0
		   despite being attached to an EventLoop */
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
			php_printf("Freeing event 0x%lx\n", (size_t) ev->this);
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
 * @return null  If object has not been initialized
 */
PHP_METHOD(Event, isActive)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_BOOL(EVENT_IS_ACTIVE(obj));
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
 * @return null  If object has not been initialized
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
	
	CHECK_CALLABLE(zcallback, func_name);
	
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

/* TODO: Add Event::getCallback() ? */
/* TODO: Add Event::getPriority() and Event::setPriority() */

/**
 * Invokes the callback on this event, Event does not need to be attached
 * to any EventLoop for this to work (disregarding requirments of the
 * associated callback itself).
 * 
 * @return boolean  false if the object is uninitialized
 */
PHP_METHOD(Event, invoke)
{
	/* Empty dummy-loop pointer */
	struct ev_loop *loop = NULL;
	int revents = 0;
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		/* NOTE: loop IS NULL-POINTER, MAKE SURE CALLBACK DOES NOT READ IT! */
		ev_invoke(loop, obj->watcher, revents);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * If the event is associated with any EventLoop (add()ed or feed_event()ed), it
 * will be stopped and reset.
 * 
 * @return boolean
 */
PHP_METHOD(Event, stop)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	RETURN_BOOL(php_event_stop(obj));
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
		/* TODO: libev-specific exception class here */
		zend_throw_exception(NULL, "libev\\IOEvent: events parameter must be at least one of IOEvent::READ or IOEvent::WRITE", 1 TSRMLS_DC);
		
		return;
	}
	
	/* Attempt to get the file descriptor from the stream */
	if(ZEND_FETCH_RESOURCE_NO_RETURN(stream, php_stream*, fd, -1, NULL, php_file_le_stream()))
	{
		if(php_stream_cast(stream, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void*)&file_desc, 1) != SUCCESS || file_desc < 0)
		{
			/* TODO: libev-specific exception class here */
			zend_throw_exception(NULL, "libev\\IOEvent: invalid stream", 1 TSRMLS_DC);
		
			return;
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
			/* TODO: libev-specific exception class here */
			zend_throw_exception(NULL, "libev\\IOEvent: fd argument must be either valid PHP stream or valid PHP socket resource", 1 TSRMLS_DC);
		
			return;
		}
	}
	
	CHECK_CALLABLE(zcallback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, zcallback);
	
	EVENT_CREATE_WATCHER(obj, io, (int) file_desc, (int) events);
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
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER(obj, timer, after, repeat);
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
 * Sets the new repeat value, will be used every time the watcher times out,
 * or TimerEvent::again() is called.
 * 
 * @param  double    timeout, in seconds, 0 = no-repeat
 * @return booelean  false if the object is not initialized
 */
PHP_METHOD(TimerEvent, setRepeat)
{
	double repeat = 0.;
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "d", &repeat) != SUCCESS) {
		return;
	}
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		((ev_timer *)obj->watcher)->repeat = repeat;
		
		RETURN_BOOL(1);
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

/**
 * This will act as if the timer timed out and restarts it again if it is repeating.
 * 
 * The exact semantics are:
 *  * If the timer is pending, its pending status is cleared.
 *  * If the timer is started but non-repeating, stop it (as if it timed out).
 *  * If the timer is repeating, either start it if necessary (with the repeat value),
 *    or reset the running timer to the repeat value.
 * 
 * See <http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#Be_smart_about_timeouts>
 * for more information.
 * 
 * @return boolean  false if it is not associated with an EventLoop object
 *                  or event is uninitialized
 */
PHP_METHOD(TimerEvent, again)
{
	event_object *event_obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(event_obj->watcher);
	
	if(event_obj->watcher && EVENT_HAS_LOOP(event_obj))
	{
		ev_timer_again(event_obj->evloop->loop, (ev_timer *)event_obj->watcher);
		
		if( ! EVENT_IS_ACTIVE(event_obj) && ! EVENT_IS_PENDING(event_obj))
		{
			/* No longer referenced by libev, so remove GC protection */
			IF_DEBUG(libev_printf("ev_timer_again() stopped non-repeating timer\n"));
			LOOP_REF_DEL(event_obj);
		}
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the remaining time until a timer fires, relative to the event loop
 * time.
 * 
 * @return double
 * @return false   if the TimerEvent is not associated with any EventLoop
 */
PHP_METHOD(TimerEvent, getRemaining)
{
	event_object *event_obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(event_obj->watcher);
	
	if(event_obj->watcher && EVENT_HAS_LOOP(event_obj))
	{
		RETURN_DOUBLE(ev_timer_remaining(event_obj->evloop->loop, (ev_timer *)event_obj->watcher));
	}
	
	RETURN_BOOL(0);
}


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
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER(obj, periodic, after, repeat, 0);
}

/**
 * Returns the time for the next trigger of the event, seconds.
 * 
 * @return double
 * @return false  if object has not been initialized
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
 * @return double
 * @return false  if object has not been initialized
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
 * @return double
 * @return false  if object has not been initialized
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
 * @return boolean  if object has not been initialized
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
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER(obj, signal, (int) signo);
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
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER(obj, child, (int) pid, (int) trace);
}

/**
 * Returns the PID this event was registered for.
 * 
 * @return int
 * @return false  if object has not been initialized
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
 * @return false  if object has not been initialized
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
 * @return false  if object has not been initialized
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
 * Watches a file system path for attribute changes, triggers when at least
 * one attribute has been changed.
 * 
 * The path does not need to exist, and the event will be triggered when the
 * path starts to exist.
 * 
 * The portable implementation of ev_stat is using the system stat() call
 * to regularily poll the path for changes which is inefficient. But even
 * with OS supported change notifications it can be resource-intensive if
 * many StatEvent watchers are used.
 * 
 * If inotify is supported and is compiled into libev that will be used instead
 * of stat() where possible.
 * 
 * NOTE: When libev is doing the stat() call the loop will be blocked, so it
 *       is not recommended to use it on network resources as there might be
 *       a long delay (accoring to libev manual, it usually takes several
 *       milliseconds on a network resource, in best cases)
 * 
 * stat() system calls also only supports full-second resolution portably,
 * meaning that if the time is the only thing which changes on the file
 * several updates of it close in time might be missed because stat() still
 * returns the same full second, unless the file changes in other ways too.
 * 
 * One solution to this problem is to start a timer which triggers after
 * roughly a one-second delay (recommended to be a bit grater than 1.0 seconds
 * because Linux gettimeofday() might return a different time from time(),
 * the libev manual recommends 1.02)
 * 
 * @param  string  Path to file to watch, does not need to exist at time of call
 *                 NOTE: absolute paths are to be preferred, as libev's behaviour
 *                       is undefined if the current working directory changes
 * @param  callback
 * @param  double    the minimum interval libev will check for file-changes,
 *                   will automatically be set to the minimum value by libev if
 *                   the supplied value is smaller than the allowed minimum.
 */
PHP_METHOD(StatEvent, __construct)
{
	char *filename;
	int filename_len;
	char *stat_path;
	double interval = 0.;
	zval *callback;
	char *func_name;
	event_object *obj;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sz|d", &filename, &filename_len, &callback, &interval) != SUCCESS) {
		return;
	}
	
	assert(strlen(filename) == filename_len);
	
	/* TODO: Do we need to respect safe_mode and open_basedir here? */
	
	CHECK_CALLABLE(callback, func_name);
	
	/* This string needs to be freed on object destruction */
	stat_path = emalloc(filename_len + 1);
	memcpy(stat_path, filename, filename_len + 1);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER(obj, stat, stat_path, interval);
}

/**
 * Returns the path this event is watching.
 * 
 * @return string
 * @return false  if object has not been initialized
 */
PHP_METHOD(StatEvent, getPath)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_STRING(((ev_stat *)obj->watcher)->path, 1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the interval which libev will check file status with stat().
 * 
 * @return double  seconds
 * @return false  if object has not been initialized
 */
PHP_METHOD(StatEvent, getInterval)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_stat *)obj->watcher)->interval);
	}
	
	RETURN_BOOL(0);
}

/* The two macros below are more or less copies from php_if_fstat */
#define MAKE_LONG_ZVAL(name, val)\
	MAKE_STD_ZVAL(name); \
	ZVAL_LONG(name, val);

#define ev_statdata_to_php_array(statdata, php_zval)        \
	zval *s_dev, *s_ino, *s_mode, *s_nlink, *s_uid, *s_gid, \
		*s_rdev, *s_size, *s_atime, *s_mtime, *s_ctime;     \
	array_init(php_zval);                                   \
	MAKE_LONG_ZVAL(s_dev, statdata.st_dev);                 \
	MAKE_LONG_ZVAL(s_ino, statdata.st_ino);                 \
	MAKE_LONG_ZVAL(s_mode, statdata.st_mode);               \
	MAKE_LONG_ZVAL(s_nlink, statdata.st_nlink);             \
	MAKE_LONG_ZVAL(s_uid, statdata.st_uid);                 \
	MAKE_LONG_ZVAL(s_gid, statdata.st_gid);                 \
	MAKE_LONG_ZVAL(s_rdev, statdata.st_rdev);               \
	MAKE_LONG_ZVAL(s_size, statdata.st_size);               \
	MAKE_LONG_ZVAL(s_atime, statdata.st_atime);             \
	MAKE_LONG_ZVAL(s_mtime, statdata.st_mtime);             \
	MAKE_LONG_ZVAL(s_ctime, statdata.st_ctime);             \
	/* Store string indexes referencing the same zval*/     \
	zend_hash_update(HASH_OF(php_zval), "dev", strlen("dev")+1, (void *)&s_dev, sizeof(zval *), NULL);       \
	zend_hash_update(HASH_OF(php_zval), "ino", strlen("ino")+1, (void *)&s_ino, sizeof(zval *), NULL);       \
	zend_hash_update(HASH_OF(php_zval), "mode", strlen("mode")+1, (void *)&s_mode, sizeof(zval *), NULL);    \
	zend_hash_update(HASH_OF(php_zval), "nlink", strlen("nlink")+1, (void *)&s_nlink, sizeof(zval *), NULL); \
	zend_hash_update(HASH_OF(php_zval), "uid", strlen("uid")+1, (void *)&s_uid, sizeof(zval *), NULL);       \
	zend_hash_update(HASH_OF(php_zval), "gid", strlen("gid")+1, (void *)&s_gid, sizeof(zval *), NULL);       \
	zend_hash_update(HASH_OF(php_zval), "rdev", strlen("rdev")+1, (void *)&s_rdev, sizeof(zval *), NULL);    \
	zend_hash_update(HASH_OF(php_zval), "size", strlen("size")+1, (void *)&s_size, sizeof(zval *), NULL);    \
	zend_hash_update(HASH_OF(php_zval), "atime", strlen("atime")+1, (void *)&s_atime, sizeof(zval *), NULL); \
	zend_hash_update(HASH_OF(php_zval), "mtime", strlen("mtime")+1, (void *)&s_mtime, sizeof(zval *), NULL); \
	zend_hash_update(HASH_OF(php_zval), "ctime", strlen("ctime")+1, (void *)&s_ctime, sizeof(zval *), NULL); \
	
/**
 * Returns the last stat information received about the file,
 * all array elements will be zero if the event has not been added to an EventLoop.
 * 
 * NOTE: If the nlink key is 0, then the file does not exist.
 * 
 * @return array
 * @return false  if object has not been initialized
 */
PHP_METHOD(StatEvent, getAttr)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		ev_statdata_to_php_array(((ev_stat *)obj->watcher)->attr, return_value);
		
		return;
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the next to last stat information received about the file,
 * all array elements will be zero if the event has not been added to an EventLoop.
 * 
 * NOTE: If the nlink key is 0, then the file does not exist.
 * 
 * @return array
 * @return false  if object has not been initialized
 */
PHP_METHOD(StatEvent, getPrev)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher)
	{
		ev_statdata_to_php_array(((ev_stat *)obj->watcher)->prev, return_value);
		
		return;
	}
	
	RETURN_BOOL(0);
}


PHP_METHOD(IdleEvent, __construct)
{
	zval *callback;
	event_object *obj;
	char *func_name;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &callback) != SUCCESS) {
		return;
	}
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	/* EVENT_CREATE_WATCHER(obj, idle): */
	assert( ! obj->watcher);
	obj->watcher = emalloc(sizeof(ev_idle));
	memset(obj->watcher, 0, sizeof(ev_idle));
	obj->watcher->data = obj;
	ev_idle_init((ev_idle *)obj->watcher, event_callback);
}


/**
 * Normal constructor for EventLoop instance.
 */
PHP_METHOD(EventLoop, __construct)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert( ! obj->loop);
	
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
 * Returns true if the loop is the default libev loop.
 * 
 * @return boolean
 * @return null  if object is not initialized
 */
PHP_METHOD(EventLoop, isDefaultLoop)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_BOOL(ev_is_default_loop(obj->loop));
	}
	
	RETURN_NULL();
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
 * Returns one of the EventLoop::BACKEND_* flags indicating the event backend in use.
 * 
 * @return int
 * @return false  if object is not initialized
 */
PHP_METHOD(EventLoop, getBackend)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		RETURN_LONG(ev_backend(obj->loop));
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
 * Establishes the current time by querying the kernel, updating the time
 * returned by EventLoop::now() in the progress.
 * 
 * This is a costly operation and is usually done automatically within ev_run ().
 * This function is rarely useful, but when some event callback runs for a very
 * long time without entering the event loop, updating libev's idea of the current
 * time is a good idea.
 * 
 * @return boolean  false if object is not initialized
 */
PHP_METHOD(EventLoop, updateNow)
{
	/* TODO: Is this method name confusing? */
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_now_update(obj->loop);
		
		RETURN_BOOL(1);
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
		
		RETURN_BOOL(1);
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
 *              * int(0)                 run() handles events until there are no events
 *                attached, default
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
 * Increases the watcher-reference count on the EventLoop, this is the reverse
 * of EventLoop::unref()
 * 
 * @see EventLoop::unref()
 * @return boolean
 */
PHP_METHOD(EventLoop, ref)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_ref(obj->loop);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Ref/unref can be used to add or remove a reference count on the event loop:
 * Every watcher keeps one reference, and as long as the reference count is nonzero,
 * ev_run will not return on its own.
 * 
 * This is useful when you have a watcher that you never intend to unregister,
 * but that nevertheless should not keep ev_run from returning. In such a case,
 * call EventLoop::unref after starting, and EventLoop::ref before stopping it.
 * 
 * As an example, libev itself uses this for its internal signal pipe:
 * It is not visible to the libev user and should not keep ev_run from exiting if
 * no event watchers registered by it are active. It is also an excellent way to do
 * this for generic recurring timers or from within third-party libraries. Just remember
 * to unref after start and ref before stop (but only if the watcher wasn't active before,
 * or was active before, respectively. Note also that libev might stop watchers itself
 * (e.g. non-repeating timers) in which case you have to EventLoop::ref() in the callback).
 * 
 * Example: Create a signal watcher, but keep it from keeping ev_run running when nothing
 * else is active:
 * <code>
 * $sig = new libev\SignalEvent(libev\SignalEvent::SIGINT, function()
 * {
 *     // Do something
 * });
 * 
 * $loop->add($sig);
 * $loop->unref();
 * 
 * // For some weird reason we want to unregister the above handler
 * $loop->ref();
 * $sig->stop();  // or $loop->remove($sig);
 * </code>
 * 
 * @return boolean
 */
PHP_METHOD(EventLoop, unref)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->loop);
	
	if(obj->loop)
	{
		ev_unref(obj->loop);
		
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
 * @return boolean  false if object has not been initialized
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
 * @return boolean  false if object has not been initialized
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
	/* TODO: Implement support for ev_unref() which will make the EvenLoop ignore
	   the Event if it is the only active event */
	zval *zevent;
	event_object *event;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &zevent, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(zevent TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	/* Check so the event is not associated with any EventLoop, also needs to check
	   for active, no need to perform logic if it already is started */
	if(loop_obj->loop && event->watcher && ! EVENT_IS_ACTIVE(event))
	{
		if(EVENT_HAS_LOOP(event))
		{
			if( ! EVENT_IS_IN_LOOP(event, loop_obj))
			{
				/* Attempting to add a fed event to this EventLoop which
				   has been fed to another loop */
				IF_DEBUG(libev_printf("Attempting to add() an event already associated with another EventLoop\n"));
				RETURN_BOOL(0);
			}
		}
		
		EV_WATCHER_ACTION(event, loop_obj, start, io)
		else EV_WATCHER_ACTION(event, loop_obj, start, timer)
		else EV_WATCHER_ACTION(event, loop_obj, start, periodic)
		else EV_WATCHER_ACTION(event, loop_obj, start, signal)
		else if(instance_of_class(event->std.ce, child_event_ce))
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
		else EV_WATCHER_ACTION(event, loop_obj, start, stat)
		else EV_WATCHER_ACTION(event, loop_obj, start, idle)
		
		if( ! EVENT_HAS_LOOP(event))
		{
			/* GC protection */
			LOOP_REF_ADD(event, loop_obj);
		}
		
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
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	if(loop_obj->loop && event->watcher && ev_is_active(event->watcher))
	{
		assert(event->evloop);
		
		/* Check that the event is associated with us */
		if( ! EVENT_IS_IN_LOOP(event, loop_obj))
		{
			IF_DEBUG(libev_printf("Event is not in this EventLoop\n"));
			
			RETURN_BOOL(0);
		}
		
		EV_WATCHER_ACTION(event, loop_obj, stop, io)
		else EV_WATCHER_ACTION(event, loop_obj, stop, timer)
		else EV_WATCHER_ACTION(event, loop_obj, stop, periodic)
		else EV_WATCHER_ACTION(event, loop_obj, stop, signal)
		else EV_WATCHER_ACTION(event, loop_obj, stop, child)
		else EV_WATCHER_ACTION(event, loop_obj, stop, stat)
		else EV_WATCHER_ACTION(event, loop_obj, stop, idle)
		
		/* Remove GC protection, no longer active or pending */
		LOOP_REF_DEL(event);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * If the watcher is pending, this function clears its pending status and
 * returns its revents bitset (as if its callback was invoked). If the watcher
 * isn't pending it returns 0, or if it is not associated with this EventLoop
 * it returns false.
 * 
 * @param  Event
 * @return int  revents bitset if pending, 0 if not
 * @return false  If not associated with this EventLoop, or if the EventLoop or
 *                Event is not initialized
 */
PHP_METHOD(EventLoop, clearPending)
{
	zval *event_obj;
	event_object *event;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	int revents = 0;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	if(loop_obj->loop && event->watcher)
	{
		/* Check that the event is associated with us */
		if( ! EVENT_IS_IN_LOOP(event, loop_obj))
		{
			IF_DEBUG(libev_printf("Event is not in this EventLoop\n"));
			
			RETURN_BOOL(0);
		}
		
		revents = ev_clear_pending(loop_obj->loop, event->watcher);
		
		/* Inactive event, meaning it is no longer part of the event loop
		   and must be dtor:ed (most probably a fed event which has never
		   become active because ev_TYPE_start has not been called) */
		if( ! ev_is_active(event->watcher))
		{
			LOOP_REF_DEL(event);
		}
		
		RETURN_LONG(revents);
	}
	
	RETURN_BOOL(0);
}

/**
 * Feeds the given event set into the event loop, as if the specified event
 * had happened for the specified watcher.
 * 
 * The watcher will be GC protected until it has fired or clearPending is called
 * on it (unless you feed it again in the callback or add() it to an event loop
 * it won't accidentally be freed).
 * 
 * NOTE: As of libev 4.0.4; If you feed an event in the callback of a fed event,
 *       the newly fed event will be invoked before any other events (except other
 *       fed events). So do NOT create loops by re-feeding an event into the EventLoop
 *       as that loop will block just as much as a normal loop.
 * 
 * TODO: Add note about AsyncEvent when AsyncEvent is implemented
 * 
 * @param  Event
 * @return boolean  false if either the EventLoop or Event has not been initialized
 */
PHP_METHOD(EventLoop, feedEvent)
{
	zval *event_obj;
	event_object *event;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	assert(event->watcher);
	
	/* Only allow Events which are associated with this EventLoop
	   or those which are not associated with any EventLoop yet */
	if(loop_obj->loop && event->watcher &&
		( ! EVENT_HAS_LOOP(event) || EVENT_IS_IN_LOOP(event, loop_obj)))
	{
		IF_DEBUG(libev_printf("Feeding event with pending %d and active %d...",
			EVENT_IS_PENDING(event), EVENT_IS_ACTIVE(event)));
		
		/* The event might already have a loop, no need to increase refcount */
		if( ! EVENT_HAS_LOOP(event))
		{
			LOOP_REF_ADD(event, loop_obj);
		}
		
		ev_feed_event(loop_obj->loop, event->watcher, 0);
		
		IF_DEBUG(php_printf(" done\n"));
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns a list of all the events associated with the EventLoop.
 * 
 * NOTE: In the case of the default event loop, only events which have
 *       been added using php-libev will be returned as the others are
 *       managed by others.
 * 
 * @return array
 */
PHP_METHOD(EventLoop, getEvents)
{
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	array_init(return_value);
	
	event_object *ev = obj->events;
		
	while(ev)
	{
		assert(ev->this);
		
		zval_add_ref(&ev->this);
		zend_hash_next_index_insert(HASH_OF(return_value), (void *)&ev->this, sizeof(zval *), NULL);
		
		ev = ev->next;
	}
	
	return;
}


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


