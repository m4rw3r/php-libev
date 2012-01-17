<?php

$loop = new libev\EventLoop();

var_dump($loop->getIteration());
var_dump($loop->getDepth());
var_dump($loop->now());
var_dump($loop->run(libev\EventLoop::RUN_NOWAIT));