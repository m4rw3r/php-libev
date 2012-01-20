===================
PHP libev Extension
===================

PHP extension providing an object-oriented binding to the libev event-loop library.

Still under development, most events are not yet supported.

Author: Martin Wernståhl <m4rw3r@gmail.com>

Installing
==========

::
  
  phpize
  ./configure --with-libev
  make
  make install

Examples
========

All of these examples should be working.

Most of these should you be able to combine into the same loop, for example the
timer and the uppercase echo.

::

  $loop = new libev\EventLoop();
  
  $repeater = new libev\TimerEvent(function()
  {
      echo "I repeat every second!\n";
  }, 1, 1);
  
  $single = new libev\TimerEvent(function()
  {
     echo "I will fire after 5 seconds, without repeat\n";
  }, 5);
  
  $loop->add($repeater);
  $loop->add($single);
  
  $loop->run();

::

  $loop = new libev\EventLoop();

  $in = fopen('php://stdin', 'r');
  $echo = new libev\IOEvent(libev\IOEvent::READ, $in, function() use($in)
  {
      // Read all (at most 200) and uppercase 
      echo "ECHO: ".strtoupper(fread($in, 200));
  });

  $loop->add($echo);
  $loop->run();

::

  $loop = new libev\EventLoop();
  
  // This will trigger very close to exactly 10 seconds after
  // this object has been created
  $time = new libev\PeriodicEvent(function()
  {
      echo "I was triggered!";
  }, time() + 10);
  
  $loop->add($time);
  $loop->run();

Periodically switching off events::

  $loop = new libev\EventLoop();

  $period1 = new libev\TimerEvent(function()
  {
  	echo "Fast\n";
  }, .1, .5);

  $period2 = new libev\TimerEvent(function() use($period1, $loop)
  {
      if($period1->isActive())
      {
          echo "Fast off\n";
          $loop->remove($period1);
      }
      else
      {
          echo "Fast on\n";
          $loop->add($period1);
      }
  }, 3, 3);
  
  $loop->add($period1);
  $loop->add($period2);
  
  $loop->run();

Combining ``libev\SignalEvent`` and ``libev\StatEvent``::

  $loop = new libev\EventLoop();
  
  // Watch ./test for changes
  $stat = new libev\StatEvent('./test', function() use(&$stat)
  {
      printf("%s changed\n", './test');
      var_dump($stat->getAttr());
  });
  
  $loop->add($stat);
  
  // Graceful shutdown on ^C
  $loop->add(new libev\SignalEvent(libev\SignalEvent::SIGINT, function() use($loop)
  {
      echo "exiting\n";
      $loop->breakLoop();
  }));
  
  $loop->run();
  

Interface
=========


``libev\EventLoop``
-------------------

**EventLoop::__construct**

Creates a new EventLoop object with a new ``ev_loop`` as base.

**static EventLoop EventLoop::getDefaultLoop()**

Returns the default event loop object, this object is a global singleton
and it is not recommended to use it unless you require ChildEvent watchers
as they can only be attached to the default loop.

**boolean EventLoop::notifyFork()**

Notifies libev that a fork might have been done and forces it
to reinitialize kernel state where needed on the next loop iteration.

**boolean EventLoop::isDefaultLoop()**

Returns true if the EventLoop is the default libev loop.

**int EventLoop::getIteration()**

Returns the current loop iteration.

**int EventLoop::getDepth()**

Returns the current nesting depth of event-loops.

**double EventLoop::now()**

Returns the time the current loop iteration received events.
Seconds in libev time.

**bool EventLoop::suspend()**

Suspends the event loop, pausing all timers and delays processing of events.

**NOTE:** DO NOT CALL IF YOU HAVE CALLED EventLoop->suspend() ALREADY!

**bool EventLoop::resume()**

Resumes the event loop and all timers.

**NOTE:** DO NOT CALL UNLESS YOU HAVE CALLED EventLoop->suspend() first!

**boolean EventLoop::run(flag = 0)**

Runs the event loop, processing all events, will block until EventLoop->break()
is called or no more events are associated with this loop by default.

libev ``flag``:

* int(``0``), default
  
  run() handles events until there are no events to handle
  
* ``EventLoop::RUN_NOWAIT``
  
  run() looks for new events, handles them and
  then return after one iteration of the loop
  
* ``EventLoop::RUN_ONCE``
  
  run() looks for new events (wait if necessary)
  and will handle those and any outstanding ones. It will block until
  at least one event has arrived and will return after one iteration of
  the loop

**boolean EventLoop::breakLoop(flag = EventLoop::BREAK_ONE)**

Breaks the current event loop after it has processed all outstanding events.

libev break flag:

* ``EventLoop::BREAK_ONE``:    will break the innermost loop, default behaviour
* ``EventLoop::BREAK_ALL``:    will break all the currently running loops

**boolean EventLoop::setIOCollectInterval(double = 0)**

Sets the time libev spends sleeping for new IO events between loop iterations,
seconds.

**boolean EventLoop::setTimeoutCollectInterval(double = 0)**

Sets the time libev spends sleeping for new timeout events between loop iterations,
seconds.

**int EventLoop::getPendingCount()**

Returns the number of pending events.

**boolean EventLoop::add(libev\Event)**

Adds the event to the event loop.

This method will increase the refcount on the supplied Event, protecting it
from garbage collection. Refcount will be decreased on ``EventLoop::remove()`` or
when the EventLoop object is Garbage Collected.

**boolean EventLoop::remove(libev\Event)**

Removes the event from the event loop, will skip all pending events on it too.

``libev\Event``
---------------

Abstract base class for all event objects.

**boolean Event::isActive()**

Returns true if the event is active, ie. associated with an event loop.

**boolean Event::isPending()**

Returns true if the event watcher is pending (ie. it has outstanding events but
the callback has not been called yet).

**void Event::setCallback(callback)**

Replaces the PHP callback on an event.

``libev\IOEvent`` extends ``libev\Event``
-----------------------------------------

**IOEvent::__construct(flag, resource, callback)**

Creates an IO event which will trigger when there is data to read and/or data
to write on the supplied stream.

``flag`` is and integer field with either ``IOEvent::READ`` and/or
``IOEvent::WRITE`` depending on the types of events you want to listen to.

``resource`` is a valid PHP stream resource.

``libev\TimerEvent`` extends ``libev\Event``
--------------------------------------------

**TimerEvent::__construct(callback, double after, double repeat = 0)**

Creates a timer event which will occur approximately after ``after`` seconds
and after that will repeat with an approximate interval of ``repeat``.

``after`` is the time before first triggering, seconds.

``interval`` is the time between repeats, seconds. Default is 0, which equals
no repeating event.

**double TimerEvent::getRepeat()**

Returns the seconds between event triggering.

**double TimerEvent::getAfter()**

Returns the time from the loop start until the first triggering of this TimerEvent.

``libev\PeriodicEvent`` extends ``libev\Event``
-----------------------------------------------

Schedules an event (or a repeating series of events) at a specific point in time.

**PeriodicEvent::__construct(callback, double offset, double interval = 0)**

* Absolute timer (``offset`` = absolute time, ``interval`` = 0)
  In this configuration the watcher triggers an event after the wall clock
  time offset has passed. It will not repeat and will not adjust when a time
  jump occurs, that is, if it is to be run at January 1st 2011 then it will be
  stopped and invoked when the system clock reaches or surpasses this point in time.
  
* Repeating interval timer (``offset`` = offset within interval, ``interval`` > 0)
  In this mode the watcher will always be scheduled to time out at the next
  ``offset`` + N * ``interval`` time (for some integer N, which can also be negative)
  and then repeat, regardless of any time jumps. The ``offset`` argument is merely
  an offset into the interval periods.

**double PeriodicEvent::getTime()**

Returns the time for the next trigger of the event, seconds.

**double PeriodicEvent::getOffset()**

When repeating, returns the offset, otherwise it returns the absolute time for
the event trigger.

**double PeriodicEvent::getInterval()**

When repeating, returns the current interval value.

**boolean PeriodicEvent::setInterval(double)**

Sets the interval value, changes only take effect when the event has fired.

``libev\SignalEvent`` extends ``libev\Event``
---------------------------------------------

**SignalEvent::__construct(signal, callback)**

``signal`` is a ``SignalEvent`` constant, the presense or absense of some of
the constants match the presense or absense of them in the system's ``signal.h``
header.

For now, you can use this code to see which constans are defined::

  $class = new ReflectionClass('libev\\SignalEvent');
  var_dump($class->getConstants());

``libev\ChildEvent`` extends ``libev\Event``
--------------------------------------------

This event will be triggered on child status changes.

**NOTE:** Must be attached to the default loop (ie. the instance from
``EventLoop::getDefaultLoop()``)


**ChildEvent::__construct(callback, int pid, boolean trace = false)**

``pid`` is the PID of the child process to watch, 0 if you want the event
to trigger for any child process.

If ``trace`` is true, then this event is also triggered on suspend/continue
and not only terminate.

**int ChildEvent::getPid()**

Returns the PID of the watched child process.

**int ChildEvent::getRPid()**

Returns the PID of the child which caused the last event trigger.

**int ChildEvent::getRStatus()**

Returns the exit/trace status (see ``waitpid`` and ``sys/wait.h``) caused by the child
ChildEvent::getRPid().

``libev\StatEvent`` extends ``libev\Event``
-------------------------------------------

Watches a file system path for attribute changes, triggers when at least
one attribute has been changed.

The path does not need to exist, and the event will be triggered when the
path starts to exist.

The portable implementation of ev_stat is using the system stat() call
to regularily poll the path for changes which is inefficient. But even
with OS supported change notifications it can be resource-intensive if
many StatEvent watchers are used.

If inotify is supported and is compiled into libev that will be used instead
of stat() where possible.

**NOTE:**
          When libev is doing the stat() call the loop will be blocked, so it
          is not recommended to use it on network resources as there might be
          a long delay (accoring to libev manual, it usually takes several
          milliseconds on a network resource, in best cases)

stat() system calls also only supports full-second resolution portably,
meaning that if the time is the only thing which changes on the file
several updates of it close in time might be missed because stat() still
returns the same full second, unless the file changes in other ways too.

One solution to this problem is to start a timer which triggers after
roughly a one-second delay (recommended to be a bit grater than 1.0 seconds
because Linux gettimeofday() might return a different time from time(),
the libev manual recommends 1.02)

**StatEvent::__construct(string file, callback, double interval = libev_default_stat_interval)**

``interval`` is the minimum interval libev will check for file-changes,
will automatically be set to the default value by libev if the supplied
value is smaller than the default.

**string StatEvent::getPath()**

**double StatEvent::getInterval()**

**array StatEvent::getAttr()**

Returns a key => value list of the file attributes, all keys will be 0 if the
event has not yet been triggered.

The following attributes are supported:

* dev
* ino
* mode
* nlink
* uid
* gid
* rdev
* size
* atime
* mtime
* ctime

**NOTE:**
          If nlink is 0, the file does not exist and the rest of the values
          may be inaccurate as they might remain from the file which existed
          during previous events.

**array StatEvent::getPrev()**

Returns the previous file attributes, all keys will be 0 if the
event has not yet been triggered.

.. _`PCNTL PHP Extension`: http://www.php.net/manual/en/book.pcntl.php