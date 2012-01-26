
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
	
	RETURN_BOOL(php_event_stop(obj, 0));
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
	
	PARSE_PARAMETERS(IOEvent, "lZz", &events, &fd, &zcallback);
	
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
	
	PARSE_PARAMETERS(TimerEvent, "zd|d", &callback, &after, &repeat);
	
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

	PARSE_PARAMETERS(PeriodicEvent, "zd|d", &callback, &after, &repeat);
	
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

	PARSE_PARAMETERS(SignalEvent, "lz", &signo, &callback);
	
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
	
	PARSE_PARAMETERS(ChildEvent, "zl|b", &callback, &pid, &trace);
	
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
	
	PARSE_PARAMETERS(StatEvent, "sz|d", &filename, &filename_len, &callback, &interval);
	
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

	PARSE_PARAMETERS(IdleEvent, "z", &callback);
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER2(obj, idle);
}


PHP_METHOD(AsyncEvent, __construct)
{
	zval *callback;
	event_object *obj;
	char *func_name;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &callback) != SUCCESS) {
		return;
	}
	
	CHECK_CALLABLE(callback, func_name);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	EVENT_CREATE_WATCHER2(obj, async);
}

PHP_METHOD(AsyncEvent, send)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->watcher);
	
	if(obj->watcher && EVENT_HAS_LOOP(obj))
	{
		ev_async_send(obj->evloop->loop, (ev_async*)obj->watcher);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/* TODO: Implement ev_async_pending? */