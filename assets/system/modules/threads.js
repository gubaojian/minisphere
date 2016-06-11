/**
 *  miniRT threads CommonJS module
 *  a threading engine for Sphere with an API similar to Pthreads
 *  (c) 2015-2016 Fat Cerberus
**/

'use strict';
module.exports =
{
	create:    create,
	isRunning: isRunning,
	join:      join,
	kill:      kill,
	self:      self,
};

const link = require('link');

var currentSelf = 0;
var hasUpdated = false;
var nextThreadID = 1;
var threads = [];

// threads.create()
// create an object thread.  this is the recommended thread creation method.
// arguments:
//     entity:   the object for which to create the thread.  this object's .update() method
//               will be called once per frame, along with .render() and .getInput() if they
//               exist, until .update() returns false.
//     priority: optional.  the render priority for the new thread.  higher-priority threads are rendered
//               later in a frame than lower-priority ones.  ignored if no renderer is provided. (default: 0)
function create(entity, priority)
{
	assert(entity instanceof Object || entity === null, "create() argument must be a valid object");

	priority = priority !== undefined ? priority : 0;

	var update = entity.update;
	var render = (typeof entity.render === 'function') ? entity.render : undefined;
	var getInput = (typeof entity.getInput === 'function') ? entity.getInput : null;
	return _makeThread(entity, {
		priority: priority,
		update: entity.update,
		render: entity.render,
		getInput: entity.getInput,
	});
}

// threads.isRunning()
// determine whether a thread is still running.
// arguments:
//     threadID: the ID of the thread to check.
function isRunning(threadID)
{
	if (threadID == 0) return false;
	for (var i = 0; i < threads.length; ++i) {
		if (threads[i].id == threadID) {
			return true;
		}
	}
	return false;
}

// threads.join()
// Blocks the calling thread until one or more other threads have terminated.
// arguments:
//     threadID: either a single thread ID or an array of them.  any invalid thread
//               ID will cause an error to be thrown.
function join(threadIDs)
{
	threadIDs = threadIDs instanceof Array ? threadIDs : [ threadIDs ];
	while (link(threads)
		.filterBy('id', threadIDs)
		.length() > 0)
	{
		_renderAll();
		screen.flip();
		_updateAll();
	}
}

// threads.kill()
// forcibly terminate a thread.
// arguments:
//     threadID: the ID of the thread to terminate.
function kill(threadID)
{
	link(threads)
		.where(function(thread) { return thread.id == threadID })
		.execute(function(thread) { thread.isValid = false; })
		.remove();
}

// threads.self()
// get the currently executing thread's thread ID.
// Remarks:
//     if this function is used outside of a thread update, render or input
//     handling call, it will return 0 (the ID of the main thread).
function self()
{
	return currentSelf;
}

function _compare(a, b) {
	return a.priority != b.priority ?
		a.priority - b.priority :
		a.id - b.id;
};

function _makeThread(that, threadDesc)
{
	var update = threadDesc.update.bind(that);
	var render = typeof threadDesc.render === 'function'
		? threadDesc.render.bind(that) : undefined;
	var getInput = typeof threadDesc.getInput === 'function'
		? threadDesc.getInput.bind(that) : undefined;
	var priority = 'priority' in threadDesc ? threadDesc.priority : 0;
	var newThread = {
		isValid: true,
		id: nextThreadID++,
		that: that,
		inputHandler: getInput,
		isBusy: false,
		priority: priority,
		renderer: render,
		updater: update,
	};
	threads.push(newThread);
	return newThread.id;
}

function _renderAll()
{
	link(link(threads).sort(_compare))
		.where(function(thread) { return thread.isValid; })
		.where(function(thread) { return thread.renderer !== undefined; })
		.each(function(thread)
	{
		thread.renderer();
	});
}

function _updateAll()
{
	var threadsEnding = [];
	link(link(threads).toArray())
		.where(function(thread) { return thread.isValid; })
		.where(function(thread) { return !thread.isBusy; })
		.each(function(thread)
	{
		var lastSelf = currentSelf;
		thread.isBusy = true;
		currentSelf = thread.id;
		var isRunning = thread.updater(thread.id);
		if (thread.inputHandler !== undefined && isRunning)
			thread.inputHandler();
		currentSelf = lastSelf;
		thread.isBusy = false;
		if (!isRunning)
			threadsEnding.push(thread.id);
	});
	link(threadsEnding)
		.each(function(threadID)
	{
		kill(threadID);
	});
	hasUpdated = true;
}
