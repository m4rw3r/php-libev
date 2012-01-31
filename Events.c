
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
	
	RETURN_BOOL(event_is_active(obj));
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
	
	RETURN_BOOL(event_is_pending(obj));
}

/**
 * Replaces the PHP callback on an event.
 * 
 * @return void
 */
PHP_METHOD(Event, setCallback)
{
	event_object *obj;
	dCALLBACK;
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &callback) != SUCCESS) {
		return;
	}
	
	CHECK_CALLBACK;
	
	obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert(obj->callback);
	
	/* Destroy existing callback reference */
	if(obj->callback)
	{
		zval_ptr_dtor(&obj->callback);
	}
	
	zval_add_ref(&callback);
	obj->callback = callback;
}

/**
 * If the Event is pending, this function clears its pending status and
 * returns its revents bitset (as if its callback was invoked). If the watcher
 * isn't pending it returns 0.
 * 
 * @param  Event
 * @return int  revents bitset if pending, 0 if not (or not associated with EventLoop)
 */
PHP_METHOD(Event, clearPending)
{
	int revents = 0;
	event_object *event = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(event->loop_obj)
	{
		revents = event_clear_pending(event->loop_obj, event);
		
		/* Inactive event, meaning it is no longer part of the event loop
		   and must be dtor:ed (most probably a fed event which has never
		   become active because ev_TYPE_start has not been called) */
		if( ! event_is_active(event))
		{
			EVENT_LOOP_REF_DEL(event);
		}
		
		RETURN_LONG(revents);
	}
	
	RETURN_LONG(0);
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
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &revents) != SUCCESS) {
		return;
	}
	
	/* NOTE: loop IS NULL-POINTER, MAKE SURE CALLBACK DOES NOT READ IT! */
	/* Can't use event_invoke here as it requires an event_loop_object */
	ev_invoke(loop, obj->watcher, revents);
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
	
	EVENT_STOP(obj);
	
	EVENT_LOOP_REF_DEL(obj);
}


/**
 * Creates an IO event which will trigger when there is data to read and/or data to write
 * on the supplied stream.
 * 
 * @param  callback  the PHP callback to call
 * @param  resource  the PHP stream to watch
 * @param  int  either IOEvent::READ and/or IOEvent::WRITE depending on type of event
 */
PHP_METHOD(IOEvent, __construct)
{
	dFILE_DESC;
	dCALLBACK;
	long events;
	event_object *obj;
	
	PARSE_PARAMETERS(IOEvent, "zZl", &callback, &fd, &events);
	
	/* Check if we have the correct flags */
	if( ! (events & (EV_READ | EV_WRITE)))
	{
		/* TODO: libev-specific exception class here */
		zend_throw_exception(NULL, "libev\\IOEvent: events parameter must be "
			"at least one of IOEvent::READ or IOEvent::WRITE", 1 TSRMLS_DC);
		
		return;
	}
	
	EXTRACT_FILE_DESC(IOEvent, __construct);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_io_init(obj, (int) file_desc, (int) events);
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
	event_object *obj;
	dCALLBACK;
	
	PARSE_PARAMETERS(TimerEvent, "zd|d", &callback, &after, &repeat);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_timer_init(obj, after, repeat);
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
	
	RETURN_DOUBLE(((ev_timer *)obj->watcher)->repeat);
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
	
	((ev_timer *)obj->watcher)->repeat = repeat;
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
	
	RETURN_DOUBLE(((ev_timer *)obj->watcher)->at);
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
	/* TODO: Optional EventLoop parameter? */
	event_object *event_obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(event_has_loop(event_obj))
	{
		event_timer_again(event_obj);
		
		if( ! event_is_active(event_obj) && ! event_is_pending(event_obj))
		{
			/* No longer referenced by libev, so remove GC protection */
			IF_DEBUG(libev_printf("ev_timer_again() stopped non-repeating timer\n"));
			EVENT_LOOP_REF_DEL(event_obj);
		}
		
		RETURN_BOOL(1);
	}
	
	/* TODO: Throw exception */
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
	
	if(event_has_loop(event_obj))
	{
		RETURN_DOUBLE(event_timer_remaining(event_obj));
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
	event_object *obj;
	dCALLBACK;

	PARSE_PARAMETERS(PeriodicEvent, "zd|d", &callback, &after, &repeat);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_periodic_init(obj, after, repeat, 0);
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
	
	RETURN_DOUBLE(event_periodic_at(obj));
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
	
	RETURN_DOUBLE(((ev_periodic *)obj->watcher)->offset);
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
	
	RETURN_DOUBLE(((ev_periodic *)obj->watcher)->interval);
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
	
	((ev_periodic *)obj->watcher)->interval = interval;
}

PHP_METHOD(PeriodicEvent, again)
{
	/* TODO: Optional EventLoop parameter? */
	event_object *event_obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(event_has_loop(event_obj))
	{
		event_periodic_again(event_obj);
		
		if( ! event_is_active(event_obj) && ! event_is_pending(event_obj))
		{
			/* No longer referenced by libev, so remove GC protection */
			IF_DEBUG(libev_printf("ev_periodic_again() stopped non-repeating timer\n"));
			EVENT_LOOP_REF_DEL(event_obj);
		}
		
		RETURN_BOOL(1);
	}
	
	/* TODO: Throw exception */
	RETURN_BOOL(0);
}


PHP_METHOD(SignalEvent, __construct)
{
	long signo;
	event_object *obj;
	dCALLBACK;

	PARSE_PARAMETERS(SignalEvent, "zl", &callback, &signo);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_signal_init(obj, (int) signo);
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
	event_object *obj;
	dCALLBACK;
	
	PARSE_PARAMETERS(ChildEvent, "zl|b", &callback, &pid, &trace);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_child_init(obj, (int) pid, (int) trace);
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
	
	RETURN_LONG(((ev_child *)obj->watcher)->pid);
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
	
	RETURN_LONG(((ev_child *)obj->watcher)->rpid);
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
	
	RETURN_LONG(((ev_child *)obj->watcher)->rstatus);
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
 * @param  callback
 * @param  string  Path to file to watch, does not need to exist at time of call
 *                 NOTE: absolute paths are to be preferred, as libev's behaviour
 *                       is undefined if the current working directory changes
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
	event_object *obj;
	dCALLBACK;
	
	PARSE_PARAMETERS(StatEvent, "zs|d", &callback, &filename, &filename_len, &interval);
	
	assert(strlen(filename) == filename_len);
	
	/* TODO: Do we need to respect safe_mode and open_basedir here? */
	/* TODO: Check for empty string? */
	
	CHECK_CALLBACK;
	
	/* This string needs to be freed on object destruction */
	stat_path = emalloc(filename_len + 1);
	memcpy(stat_path, filename, filename_len + 1);
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_stat_init(obj, stat_path, interval);
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
	
	RETURN_STRING(((ev_stat *)obj->watcher)->path, 1);
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
	
	RETURN_DOUBLE(((ev_stat *)obj->watcher)->interval);
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
	
	ev_statdata_to_php_array(((ev_stat *)obj->watcher)->attr, return_value);
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
	
	ev_statdata_to_php_array(((ev_stat *)obj->watcher)->prev, return_value);
}

PHP_METHOD(IdleEvent, __construct)
{
	event_object *obj;
	dCALLBACK;

	PARSE_PARAMETERS(IdleEvent, "z", &callback);
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_idle_init(obj);
}


PHP_METHOD(AsyncEvent, __construct)
{
	event_object *obj;
	dCALLBACK;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &callback) != SUCCESS) {
		return;
	}
	
	CHECK_CALLBACK;
	
	EVENT_OBJECT_PREPARE(obj, callback);
	
	event_async_init(obj);
}

/**
 * Sends an event to the AsyncEvent object which will trigger it in the event
 * loop.
 * 
 * @return boolean  false if the object is not attached to an event loop
 */
PHP_METHOD(AsyncEvent, send)
{
	event_object *obj = (event_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if(event_has_loop(obj))
	{
		ev_async_send(obj->loop_obj->loop, (ev_async*)obj->watcher);
		
		RETURN_BOOL(1);
	}
	
	RETURN_BOOL(0);
}

/* TODO: Implement ev_async_pending? */