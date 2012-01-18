<?php

$loop = new libev\EventLoop();

//$loop->now();

var_dump($loop->getIteration());
var_dump($loop->getDepth());
//var_dump($loop->now());
//var_dump($loop->run(libev\EventLoop::RUN_NOWAIT));

/*$event = new libev\IOEvent(libev\IOEvent::READ, fopen(__FILE__, 'r'), function()
{
	echo "WOW!";
});

var_dump($loop->add($event));


var_dump(class_implements($event));
*/

$event = new libev\TimerEvent(function(){ echo "WORKS!\n"; }, 2.0, 0.0);

var_dump($loop->add($event));
var_dump($loop->getIteration());
var_dump($loop->run());
var_dump($loop->getIteration());