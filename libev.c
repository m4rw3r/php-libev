

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

#ifdef COMPILE_DL_LIBEV
ZEND_GET_MODULE(libev)
#endif


// Defining class API
zend_class_entry *event_ce;
zend_class_entry *io_event_ce;
zend_class_entry *timer_event_ce;
zend_class_entry *event_loop_ce;


zend_object_handlers event_object_handlers;
zend_object_handlers event_loop_object_handlers;


typedef struct event_object {
	zend_object std;
	zval *callback;
	ev_watcher *watcher;
} event_object;

typedef struct event_loop_object {
	zend_object std;
	struct ev_loop *loop;
} event_loop_object;


void event_free_storage(void *object TSRMLS_DC)
{
	event_object *obj = (event_object *) object;
	
	zend_hash_destroy(obj->std.properties);
	FREE_HASHTABLE(obj->std.properties);
	
	// TODO: Is this correct?
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	if(obj->watcher)
	{
		efree(obj->watcher);
	}
	
	efree(obj);
}

zend_object_value event_create_handler(zend_class_entry *type TSRMLS_DC)
{
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
	
	return retval;
}

void event_loop_free_storage(void *object TSRMLS_DC)
{
	event_loop_object *obj = (event_loop_object *) object;
	
	ev_loop_destroy(obj->loop);
	
	zend_hash_destroy(obj->std.properties);
	FREE_HASHTABLE(obj->std.properties);
	
	efree(obj);
}

zend_object_value event_loop_create_handler(zend_class_entry *type TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	
	event_loop_object *obj = emalloc(sizeof(event_loop_object));
	memset(obj, 0, sizeof(event_loop_object));
	obj->std.ce = type;
	
	ALLOC_HASHTABLE(obj->std.properties);
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(obj->std.properties, &type->default_properties,
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
	
	// TODO: Do we need to be able to change the parameter to ev_loop_new() here?
	obj->loop = ev_loop_new(EVFLAG_AUTO);
	
	// TODO: Add macros around these
	ev_verify(obj->loop);
	
	retval.handle = zend_objects_store_put(obj, NULL, event_loop_free_storage, NULL TSRMLS_CC);
	retval.handlers = &event_loop_object_handlers;
	
	return retval;
}

static void event_callback(struct ev_loop *loop, ev_timer *w, int revents)
{
	zval retval;
	
	if(call_user_function(EG(function_table), NULL, ((event_object *)w->data)->callback, &retval, 0, NULL TSRMLS_CC) == SUCCESS)
	{
		zval_dtor(&retval);
	}
}


PHP_METHOD(IOEvent, __construct)
{
	long events;
	php_socket_t file_desc;
	zval **fd, *zcallback = NULL;
	char *func_name;
	event_object *obj;
	php_stream *stream;
	php_socket *php_sock;
	zval *object = getThis();
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "lZz", &events, &fd, &zcallback) != SUCCESS) {
		return;
	}
	
	// Check if we have the correct flags
	if( ! (events & (EV_READ | EV_WRITE)))
	{
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "events parameter must be at least one of IOEvent::READ or IOEvent::WRITE");
		RETURN_FALSE;
	}
	
	// Attempt to get the file descriptor from the stream
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
	
	obj = (event_object *)zend_object_store_get_object(object TSRMLS_CC);
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
 * @param  double
 * @param  double    Default: 0 = no repeat
 * @param  callback
 */
PHP_METHOD(TimerEvent, __construct)
{
	double after;
	double repeat = 0.;
	zval *callback;
	char *func_name;
	event_object *obj;
	zval *object = getThis();
	
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
	
	obj = (event_object *)zend_object_store_get_object(object TSRMLS_CC);
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
	// TODO: Not sure if this is a good idea, ev_timer->at is marked as private in ev.h
	
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(obj->watcher)
	{
		RETURN_DOUBLE(((ev_timer *)obj->watcher)->at);
	}
	
	RETURN_BOOL(0);
}
	

// TODO: implement support for ev_timer_again(loop, ev_timer*) ?
// TODO: implement support for ev_timer_remaining(loop, ev_timer*) ?

/**
 * Notifies libev that a fork might have been done and forces it
 * to reinitialize kernel state where needed on the next loop iteration.
 * 
 * @return boolean  false if object has not been initialized
 */
PHP_METHOD(EventLoop, notifyFork)
{
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_loop_fork(loop);
		
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
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		RETURN_LONG(ev_iteration(loop));
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
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		RETURN_LONG(ev_depth(loop));
	}
	
	RETURN_BOOL(0);
}

/**
 * Returns the time the current loop iteration received events.
 * 
 * @return double
 * @return false  if object is not initialized
 */
PHP_METHOD(EventLoop, now)
{
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		RETURN_DOUBLE(ev_now(loop));
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
	// TODO: Implement a check for if we already have suspended the eventloop?
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_suspend(loop);
		
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
	// TODO: Implement a check for it suspend has been called?
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_resume(loop);
		
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
	struct ev_loop *loop;
	long how = 0;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &how) != SUCCESS) {
		return;
	}
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_run(loop, (int)how);
		
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
	struct ev_loop *loop;
	long how = EVBREAK_ONE;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &how) != SUCCESS) {
		return;
	}
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_break(loop, how);
		
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
PHP_METHOD(EventLoop, setIoCollectInterval)
{
	struct ev_loop *loop;
	double interval = 0;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &interval) != SUCCESS) {
		return;
	}
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		ev_set_io_collect_interval(loop, interval);
		
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
	struct ev_loop *loop;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	loop = obj->loop;
	
	if(loop != NULL)
	{
		RETURN_LONG(ev_pending_count(loop));
	}
	
	RETURN_BOOL(0);
}

PHP_METHOD(EventLoop, add)
{
	zval *event_obj;
	event_object *event;
	zend_class_entry *object_ce;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_obj, event_ce) != SUCCESS) {
		return;
	}
	
	object_ce = zend_get_class_entry(event_obj);
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	// TODO: Validate that the Event object has not already been associated with an EventLoop
	
	if(object_ce == io_event_ce)
	{
		ev_io_start(loop_obj->loop, (ev_io *)event->watcher);
		
		RETURN_STRING("IOEvent", 1);
	}
	else if(object_ce == timer_event_ce)
	{
		ev_timer_start(loop_obj->loop, (ev_timer *)event->watcher);
		
		RETURN_STRING("TimerEvent", 1);
	}
	
	RETURN_STRING("UNKNOWN", 1);
}


static const function_entry event_methods[] = {
	{NULL, NULL, NULL}
};

static const function_entry io_event_methods[] = {
	ZEND_ME(IOEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	{NULL, NULL, NULL}
};

static const function_entry timer_event_methods[] = {
	ZEND_ME(TimerEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(TimerEvent, getRepeat, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(TimerEvent, getAfter, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};

static const function_entry event_loop_methods[] = {
	ZEND_ME(EventLoop, notifyFork, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getIteration, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getDepth, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, now, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, suspend, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, resume, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, run, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, breakLoop, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, setIoCollectInterval, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, getPendingCount, NULL, ZEND_ACC_PUBLIC)
	ZEND_ME(EventLoop, add, NULL, ZEND_ACC_PUBLIC)
	{NULL, NULL, NULL}
};


static void libev_register_implements(zend_class_entry *class_entry, zend_class_entry *interface_entry TSRMLS_DC)
{
	zend_uint num_interfaces = ++class_entry->num_interfaces;

	class_entry->interfaces = (zend_class_entry **) realloc(class_entry->interfaces, sizeof(zend_class_entry *) * num_interfaces);
	class_entry->interfaces[num_interfaces - 1] = interface_entry;
}


PHP_MINIT_FUNCTION(libev)
{
	// Init generic object handlers for Event objects, prevent clone
	memcpy(&event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	event_object_handlers.clone_obj = NULL;
	
	
	// libev\Event interface
	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "libev\\Event", event_methods);
	event_ce = zend_register_internal_interface(&ce TSRMLS_CC);
	
	
	// libev\IOEvent
	zend_class_entry ce3;
	INIT_CLASS_ENTRY(ce3, "libev\\IOEvent", io_event_methods);
	io_event_ce = zend_register_internal_class(&ce3 TSRMLS_CC);
	libev_register_implements(io_event_ce, event_ce TSRMLS_CC);
	
	// Override default object creation
	io_event_ce->create_object = event_create_handler;
	
	// IOEvent constants
	zend_declare_class_constant_long(io_event_ce, "READ", sizeof("READ") - 1, EV_READ TSRMLS_CC);
	zend_declare_class_constant_long(io_event_ce, "WRITE", sizeof("WRITE") - 1, EV_WRITE TSRMLS_CC);
	
	
	// libev\TimerEvent
	zend_class_entry ce4;
	INIT_CLASS_ENTRY(ce4, "libev\\TimerEvent", timer_event_methods);
	timer_event_ce = zend_register_internal_class(&ce4 TSRMLS_CC);
	libev_register_implements(timer_event_ce, event_ce TSRMLS_CC);
	timer_event_ce->create_object = event_create_handler;
	
	
	// libev\EventLoop
	zend_class_entry ce2;
	INIT_CLASS_ENTRY(ce2, "libev\\EventLoop", event_loop_methods);
	event_loop_ce = zend_register_internal_class(&ce2 TSRMLS_CC);
	
	// Override default object handlers so we can use custom struct
	event_loop_ce->create_object = event_loop_create_handler;
	memcpy(&event_loop_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	event_loop_object_handlers.clone_obj = NULL;
	
	// EventLoop class constants
	zend_declare_class_constant_long(event_loop_ce, "RUN_NOWAIT", sizeof("RUN_NOWAIT") - 1, EVRUN_NOWAIT TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "RUN_ONCE", sizeof("RUN_ONCE") - 1, EVRUN_ONCE TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "BREAK_ONE", sizeof("BREAK_ONE") - 1, EVBREAK_ONE TSRMLS_CC);
	zend_declare_class_constant_long(event_loop_ce, "BREAK_ALL", sizeof("BREAK_ALL") - 1, EVBREAK_ALL TSRMLS_CC);
	
	
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




