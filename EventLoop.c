
/**
 * Normal constructor for EventLoop instance.
 */
PHP_METHOD(EventLoop, __construct)
{
	int backend = EVFLAG_AUTO;
	event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	assert( ! obj->loop);
	
	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|l", &backend) != SUCCESS) {
		return;
	}
	
	/* Check parameter */
	if(EVFLAG_AUTO       != backend &&
	   EVBACKEND_SELECT  != backend &&
	   EVBACKEND_POLL    != backend &&
	   EVBACKEND_EPOLL   != backend &&
	   EVBACKEND_KQUEUE  != backend &&
	   EVBACKEND_DEVPOLL != backend &&
	   EVBACKEND_PORT    != backend &&
	   EVBACKEND_ALL     != backend) {
		/* TODO: libev-specific exception class here */
		zend_throw_exception(NULL, "libev\\EventLoop: backend parameter must be "
			"one of the EventLoop::BACKEND_* constants.", 1 TSRMLS_DC);
		
		return;
	}
	
	obj->loop = ev_loop_new(backend);
	
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
		ALLOC_INIT_ZVAL(default_event_loop_object);
		
		/* Create object without calling constructor, we now have an EventLoop missing the ev_loop */
		if(object_init_ex(default_event_loop_object, event_loop_ce) != SUCCESS) {
			/* TODO: Error handling */
			RETURN_BOOL(0);
		
			return;
		}
		
		event_loop_object *obj = (event_loop_object *)zend_object_store_get_object(default_event_loop_object TSRMLS_CC);
		
		assert( ! obj->loop);
		
		/* TODO: allow other EVFLAGs */
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
	
	/* TODO: Check so it is >= 0 */
	
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
	
	/* TODO: Check so it is >= 0 */
	
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
	zval *zevent;
	event_object *event;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &zevent, event_ce) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(zevent TSRMLS_CC);
	
	assert(loop_obj->loop);
	
	/* Check so the event is not associated with any EventLoop, also needs to check
	   for active, no need to perform logic if it already is started */
	if(loop_obj->loop && ! event_is_active(event))
	{
		if(event_has_loop(event))
		{
			if( ! event_in_loop(loop_obj, event))
			{
				/* Attempting to add a fed event to this EventLoop which
				   has been fed to another loop */
				IF_DEBUG(libev_printf("Attempting to add() an event already associated with another EventLoop\n"));
				RETURN_BOOL(0);
			}
		}
		
		EVENT_WATCHER_ACTION(event, loop_obj, start, io)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, timer)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, periodic)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, signal)
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
		else EVENT_WATCHER_ACTION(event, loop_obj, start, stat)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, idle)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, async)
		else EVENT_WATCHER_ACTION(event, loop_obj, start, cleanup)
		
		if( ! event_has_loop(event))
		{
			/* GC protection */
			EVENT_LOOP_REF_ADD(event, loop_obj);
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
	
	if(loop_obj->loop && event_is_active(event))
	{
		assert(event->loop_obj);
		
		/* Check that the event is associated with us */
		if( ! event_in_loop(loop_obj, event))
		{
			IF_DEBUG(libev_printf("Event is not in this EventLoop\n"));
			
			RETURN_BOOL(0);
		}
		
		EVENT_STOP(event);
		
		/* Remove GC protection, no longer active or pending */
		EVENT_LOOP_REF_DEL(event);
		
		RETURN_BOOL(1);
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
	int revents = 0;
	zval *event_obj;
	event_object *event;
	event_loop_object *loop_obj = (event_loop_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O|l", &event_obj, event_ce, &revents) != SUCCESS) {
		return;
	}
	
	event = (event_object *)zend_object_store_get_object(event_obj TSRMLS_CC);
	
	assert(loop_obj->loop);
	
	/* Only allow Events which are associated with this EventLoop
	   or those which are not associated with any EventLoop yet */
	if(loop_obj->loop &&
		( ! event_has_loop(event) || event_in_loop(loop_obj, event)))
	{
		IF_DEBUG(libev_printf("Feeding event with pending %d and active %d...",
			event_is_pending(event), event_is_active(event)));
		
		/* The event might already have a loop, no need to increase refcount */
		if( ! event_has_loop(event))
		{
			EVENT_LOOP_REF_ADD(event, loop_obj);
		}
		
		event_feed_event(loop_obj, event, revents);
		
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
