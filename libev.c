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

zend_object_handlers io_event_object_handlers;
zend_object_handlers timer_event_object_handlers;
zend_object_handlers event_loop_object_handlers;


typedef struct io_event_object {
	zend_object std;
	ev_io watcher;
	zval *callback;
} io_event_object;

typedef struct timer_event_object {
	zend_object std;
	ev_timer watcher;
	zval *callback;
} timer_event_object;

typedef struct event_loop_object {
	zend_object std;
	struct ev_loop *loop;
} event_loop_object;


void io_event_free_storage(void *object TSRMLS_DC)
{
	io_event_object *obj = (io_event_object *) object;
	
	zend_hash_destroy(obj->std.properties);
	FREE_HASHTABLE(obj->std.properties);
	
	// TODO: Is this correct?
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	efree(obj);
}

zend_object_value io_event_create_handler(zend_class_entry *type TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	
	io_event_object *obj = (io_event_object *)emalloc(sizeof(io_event_object));
	memset(obj, 0, sizeof(io_event_object));
	obj->std.ce = type;
	
	ALLOC_HASHTABLE(obj->std.properties);
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(obj->std.properties, &type->default_properties,
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
	
	retval.handle = zend_objects_store_put(obj, NULL, io_event_free_storage, NULL TSRMLS_CC);
	retval.handlers = &io_event_object_handlers;
	
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
	
	event_loop_object *obj = (event_loop_object *)emalloc(sizeof(event_loop_object));
	memset(obj, 0, sizeof(event_loop_object));
	obj->std.ce = type;
	
	ALLOC_HASHTABLE(obj->std.properties);
	zend_hash_init(obj->std.properties, 0, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_copy(obj->std.properties, &type->default_properties,
	        (copy_ctor_func_t)zval_add_ref, (void *)&tmp, sizeof(zval *));
	
	retval.handle = zend_objects_store_put(obj, NULL, event_loop_free_storage, NULL TSRMLS_CC);
	retval.handlers = &event_loop_object_handlers;
	
	return retval;
}

static void io_event_callback(struct ev_loop *loop, ev_io *w, int revents)
{
	
}

PHP_METHOD(IOEvent, __construct)
{
	long events;
	php_socket_t file_desc;
	zval **fd, *zcallback = NULL;
	char *func_name;
	io_event_object *obj;
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
	
	obj = (io_event_object *)zend_object_store_get_object(object TSRMLS_CC);
	zval_add_ref(&zcallback);
	obj->callback = zcallback;
	
	ev_io_init(&obj->watcher, io_event_callback, (int)file_desc, (int)events);
}

PHP_METHOD(EventLoop, __construct)
{
	struct ev_loop *loop;
	zval *object = getThis();
	event_loop_object *obj;
	
	loop = ev_loop_new(EVFLAG_AUTO);
	
	obj = (event_loop_object *)zend_object_store_get_object(object TSRMLS_CC);
	obj->loop = loop;
}

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
		ev_run(loop, how);
		
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
	struct ev_loop *loop;
	zval* event_object;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &event_object, event_ce) != SUCCESS) {
		return;
	}
	
	
}


static const function_entry event_methods[] = {
	{NULL, NULL, NULL}
};

static const function_entry io_event_methods[] = {
	ZEND_ME(IOEvent, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	{NULL, NULL, NULL}
};

static const function_entry event_loop_methods[] = {
	ZEND_ME(EventLoop, __construct, NULL, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
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
	// libev\Event interface
	zend_class_entry ce;
	INIT_CLASS_ENTRY(ce, "libev\\Event", event_methods);
	event_ce = zend_register_internal_interface(&ce TSRMLS_CC);
	
	
	// libev\IOEvent
	zend_class_entry ce3;
	INIT_CLASS_ENTRY(ce3, "libev\\IOEvent", io_event_methods);
	io_event_ce = zend_register_internal_class(&ce3 TSRMLS_CC);
	libev_register_implements(io_event_ce, event_ce TSRMLS_CC);
	
	// Override default object handlers so we can use custom struct
	io_event_ce->create_object = io_event_create_handler;
	memcpy(&io_event_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	io_event_object_handlers.clone_obj = NULL;
	
	// IOEvent constants
	zend_declare_class_constant_long(io_event_ce, "READ", sizeof("READ") - 1, EV_READ TSRMLS_CC);
	zend_declare_class_constant_long(io_event_ce, "WRITE", sizeof("WRITE") - 1, EV_WRITE TSRMLS_CC);
	
	
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
	php_info_print_table_header(2, "libev support", "enabled");
	php_info_print_table_row(2, "extension version", PHP_LIBEV_EXTVER);
	
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




