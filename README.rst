===================
PHP libev Extension
===================

PHP extension providing an object-oriented binding to the libev event-loop library.

Still under development, most events are not yet supported.

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