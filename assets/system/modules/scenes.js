/**
 *  miniRT/scenes CommonJS module
 *  advanced scene manager using multiple timelines and cooperative threading
 *  (c) 2015-2016 Fat Cerberus
 *
 *  based on the Scenario cutscene engine originally written for Sphere 1.5
**/

const link    = require('link');
const prim    = require('prim');
const threads = require('threads');

var screenMask = new Color(0, 0, 0, 0);

var scenes =
module.exports = (function()
{
	renderScenes = function()
	{
		if (screenMask.alpha > 0) {
			prim.rect(screen, 0, 0, screen.width, screen.height, screenMask);
		}
	};

	var updateScenes = function()
	{
		return true;
	};

	var priority = 99;
	var threadID = threads.create({
		update: updateScenes,
		render: renderScenes,
	}, priority);

	// scenelet()
	// register a new scenelet.
	// arguments:
	//     name: the name of the scenelet.  this should be a valid JavaScript identifier (alphanumeric, no spaces).
	//     def:  an object defining the scenelet callbacks:
	//           .start(scene, ...): called when the command begins executing to initialize the state, or for
	//                               instantaneous commands, perform the necessary action.
	//           .update(scene):     optional.  a function to be called once per frame to update state data.  if not
	//                               provided, scenes immediately moves on to the next command after calling start().
	//                               this function should return true to keep the operation running, or false to
	//                               terminate it.
	//           .getInput(scene):   optional.  a function to be called once per frame to check for player input and
	//                               update state data accordingly.
	//           .render(scene):     optional.  a function to be called once per frame to perform any rendering
	//                               related to the command (e.g. text boxes).
	//           .finish(scene):     optional.  called after command execution ends, just before Scenes executes
	//                               the next instruction in the queue.
	// remarks:
	//    it is safe to call this prior to initialization.
	var scenelet = function(name, def)
	{
		if (name in Scene.prototype)
			throw new Error("scenelet ID `" + name + "` already in use");
		Scene.prototype[name] = function() {
			this.enqueue({
				arguments: arguments,
				start: def.start,
				getInput: def.getInput,
				update: def.update,
				render: def.render,
				finish: def.finish
			});
			return this;
		};
	};
	
	// Scene()
	// construct a scene definition.
	function Scene()
	{
		var activation = null;
		var forkedQueues = [];
		var jumpsToFix = [];
		var mainThread = 0;
		var openBlockTypes = [];
		var queueToFill = [];
		var tasks = [];

		var goTo = function(address)
		{
			activation.pc = address;
		};

		function runTimeline(ctx)
		{
			if ('opThread' in ctx) {
				if (threads.isRunning(ctx.opThread))
					return true;
				else {
					link(tasks)
						.where(function(thread) { return ctx.opThread == thread })
						.remove();
					delete ctx.opThread;
					activation = ctx;
					if (typeof ctx.op.finish === 'function')
						ctx.op.finish.call(ctx.opctx, this);
					activation = null;
				}
			}
			if (ctx.pc < ctx.instructions.length) {
				ctx.op = ctx.instructions[ctx.pc++];
				ctx.opctx = {};
				if (typeof ctx.op.start === 'function') {
					var arglist = [ this ];
					for (i = 0; i < ctx.op.arguments.length; ++i)
						arglist.push(ctx.op.arguments[i]);
					activation = ctx;
					ctx.op.start.apply(ctx.opctx, arglist);
					activation = null;
				}
				if (ctx.op.update != null) {
					ctx.opThread = threads.createEx(ctx.opctx, {
						update: ctx.op.update.bind(ctx.opctx, this),
						render: typeof ctx.op.render === 'function' ? ctx.op.render.bind(ctx.opctx, this) : undefined,
						getInput: typeof ctx.op.getInput  === 'function' ? ctx.op.getInput.bind(ctx.opctx, this) : undefined,
						priority: priority,
					});
					tasks.push(ctx.opThread);
				} else {
					ctx.opThread = 0;
				}
				return true;
			} else {
				if (link(ctx.forks)
					.where(function(thread) { return threads.isRunning(thread); })
					.length() == 0)
				{
					var self = threads.self();
					link(tasks)
						.where(function(thread) { return self == thread })
						.remove();
					return false;
				} else {
					return true;
				}
			}
		};

        // Scene:isRunning()
		// determines whether a scene is currently playing.
		// Returns:
		//     true if the scene is still executing commands; false otherwise.
		function isRunning()
		{
			return threads.isRunning(mainThread);
		};

		// Scene:doIf()
		// during scene execution, execute a block of commands only if a specified condition is met.
		// arguments:
		//     conditional: a function to be called during scene execution to determine whether to run the following
		//                  block.  the function should return true to execute the block, or false to skip it.  it
		//                  will be called with 'this' set to the invoking scene.
		function doIf(conditional)
		{
			var jump = { ifFalse: null };
			jumpsToFix.push(jump);
			var command = {
				arguments: [],
				start: function(scene) {
					if (!conditional.call(scene)) {
						goTo(jump.ifFalse);
					}
				}
			};
			enqueue(command);
			openBlockTypes.push('branch');
			return this;
		};

		// Scene:doWhile()
		// During scene execution, repeats a block of commands for as long as a specified condition is met.
		// Arguments:
		//     conditional: A function to be called at each iteration to determine whether to continue the
		//                  loop. The function should return true to continue the loop, or false to
		//                  stop it. It will be called with 'this' set to the invoking Scene object.
		function doWhile(conditional)
		{
			var jump = { loopStart: queueToFill.length, ifDone: null };
			jumpsToFix.push(jump);
			var command = {
				arguments: [],
				start: function(scene) {
					if (!conditional.call(scene)) {
						goTo(jump.ifDone);
					}
				}
			};
			enqueue(command);
			openBlockTypes.push('loop');
			return this;
		};

		// Scene:end()
		// marks the end of a block of commands.
		function end()
		{
			if (openBlockTypes.length == 0)
				throw new Error("Mismatched end() in scene definition");
			var blockType = openBlockTypes.pop();
			switch (blockType) {
				case 'fork':
					var command = {
						arguments: [ queueToFill ],
						start: function(scene, instructions) {
							var ctx = {
								instructions: instructions,
								pc: 0,
								forks: [],
							};
							var tid = threads.createEx(scene, {
								update: runTimeline.bind(scene, ctx)
							});
							tasks.push(tid);
							activation.forks.push(tid);
						}
					};
					queueToFill = forkedQueues.pop();
					enqueue(command);
					break;
				case 'branch':
					var jump = jumpsToFix.pop();
					jump.ifFalse = queueToFill.length;
					break;
				case 'loop':
					var command = {
						arguments: [],
						start: function(scene) {
							goTo(jump.loopStart);
						}
					};
					enqueue(command);
					var jump = jumpsToFix.pop();
					jump.ifDone = queueToFill.length;
					break;
				default:
					throw new Error("Scenario internal error (unknown block type)");
					break;
			}
			return this;
		};

		// Scene:enqueue()
		// enqueues a custom scenelet.  not recommended for outside use.
		function enqueue(command)
		{
			if (isRunning())
				throw new Error("cannot modify scene definition during playback");
			queueToFill.push(command);
		};

		// Scene:fork()
		// during scene execution, fork the timeline, allowing a block to run simultaneously with
		// the instructions after it.
		function fork()
		{
			forkedQueues.push(queueToFill);
			queueToFill = [];
			openBlockTypes.push('fork');
			return this;
		};

		// Scene:restart()
		// restart the scene from the beginning.  this has the same effect as calling
		// .stop() and .play() back-to-back.
		function restart()
		{
			stop();
			run();
		};

		// Scene:resync()
		// during a scene, suspend the current timeline until all of its forks have run to
		// completion.
		// remarks:
		//     there is an implicit resync at the end of a timeline.
		function resync()
		{
			var command = {
				arguments: [],
				start: function(scene) {
					forks = activation.forks;
				},
				update: function(scene) {
					return link(forks)
						.where(function(tid) { return threads.isRunning(tid); })
						.length() > 0;
				}
			};
			enqueue(command);
			return this;
		}

		// Scene:run()
		// play back the scene.
		// arguments:
		//     waitUntilDone: if true, block until playback has finished.
		function run(waitUntilDone)
		{
			if (openBlockTypes.length > 0)
				throw new Error("unclosed block in scene definition");
			if (isRunning()) return;
			var ctx = {
				instructions: queueToFill,
				pc: 0,
				forks: [],
			};
			mainThread = threads.createEx(this, {
				update: runTimeline.bind(this, ctx)
			});
			tasks.push(mainThread);
			if (waitUntilDone)
				threads.join(mainThread);
			return this;
		};

		// Scene:stop()
		// immediately halt scene playback.  no effect if the scene isn't playing.
		// remarks:
		//     after calling this method, calling .play() afterwards will start from the
		//     beginning.
		function stop()
		{
			link(tasks)
				.each(function(tid)
			{
				threads.kill(tid);
			});
		};

		var retobj = {
			isRunning: isRunning,
			doIf:      doIf,
			doWhile:   doWhile,
			end:       end,
			enqueue:   enqueue,
			fork:      fork,
			restart:   restart,
			resync:    resync,
			run:       run,
			stop:      stop,
		};
		Object.setPrototypeOf(retobj, Scene.prototype);
		return retobj;
	}

	return {
		scenelet: scenelet,
		Scene:    Scene,
	};
})();

// .call() scenelet
// Calls a function during scene execution.
// Arguments:
//     method: The function to be called.
// Remarks:
//     Any additional arguments provided beyond the 'method' argument will be passed
//     to the specified function when it is called.
scenes.scenelet('call',
{
	start: function(scene, method /*...*/) {
		method.apply(null, [].slice.call(arguments, 2));
	}
});

// .fadeTo() scenelet
// Fades the screen mask to a specified color.
// Arguments:
//     color:    The new screen mask color.
//     duration: The length of the fading operation, in seconds.
scenes.scenelet('fadeTo',
{
	start: function(scene, color, duration) {
		duration = duration !== undefined ? duration : 0.25;
		
		this.fader = new scenes.Scene()
			.tween(screenMask, duration, 'linear', color)
			.run();
	},
	update: function(scene) {
		return this.fader.isRunning();
	}
});

// .marquee() scenelet
// Shows a scrolling marquee with the specified text. Useful for announcing boss battles.
// Arguments:
//     text:            The text to display.
//     backgroundColor: The background color of the marquee.
//     color:           The text color.
scenes.scenelet('marquee',
{
	start: function(scene, text, backgroundColor, color) {
		if (backgroundColor === undefined) { backgroundColor = new Color(0, 0, 0, 255); }
		if (color === undefined) { color = new Color(255, 255, 255, 255); }
		
		this.text = text;
		this.color = color;
		this.background = backgroundColor;
		this.font = Font.Default;
		this.windowSize = screen.width + this.font.getStringWidth(this.text);
		this.height = this.font.getHeight() + 10;
		this.textHeight = this.font.getHeight();
		this.fadeness = 0.0;
		this.scroll = 0.0;
		this.animation = new scenes.Scene()
			.tween(this, 0.25, 'linear', { fadeness: 1.0 })
			.tween(this, 1.0, 'easeOutExpo', { scroll: 0.5 })
			.tween(this, 1.0, 'easeInExpo', { scroll: 1.0 })
			.tween(this, 0.25, 'linear', { fadeness: 0.0 })
			.run();
	},
	render: function(scene) {
		var boxHeight = this.height * this.fadeness;
		var boxY = screen.height / 2 - boxHeight / 2;
		var textX = screen.width - this.scroll * this.windowSize;
		var textY = boxY + boxHeight / 2 - this.textHeight / 2;
		prim.rect(screen, 0, boxY, screen.width, boxHeight, this.background);
		this.font.drawText(screen, textX + 1, textY + 1, this.text, Color.Black.fade(this.color.alpha));
		this.font.drawText(screen, textX, textY, this.text, this.color);
	},
	update: function(scene) {
		return this.animation.isRunning();
	}
});

// .pause() scenelet
// Delays execution of the current timeline for a specified amount of time.
// Arguments:
//     duration: The length of the delay, in seconds.
scenes.scenelet('pause',
{
	start: function(scene, duration) {
		this.duration = duration;
		this.elapsed = 0;
	},
	update: function(scene) {
		this.elapsed += 1.0 / screen.frameRate;
		return this.elapsed < this.duration;
	}
});

// .playSound() scenelet
// Plays a sound from a file.
//     fileName: The name of the file to play.
scenes.scenelet('playSound',
{
	start: function(scene, fileName) {
		this.sound = new Sound(fileName);
		this.sound.play(Mixer.Default);
		return true;
	},
	update: function(scene) {
		return this.sound.isPlaying();
	}
});

// .tween() scenelet
// Smoothly adjusts numeric properties of an object over a period of time.
// Arguments:
//    object:     The object containing the properties to be tweened.
//    duration:   The length of the tweening operation, in seconds.
//    easingType: The name of the easing function to use, e.g. 'linear' or 'easeOutQuad'.
//    endValues:  An object specifying the properties to tween and their final values.
scenes.scenelet('tween',
{
	start: function(scene, object, duration, easingType, endValues) {
		this.easers = {
			linear: function(t, b, c, d) {
				return c * t / d + b;
			},
			easeInQuad: function(t, b, c, d) {
				return c*(t/=d)*t + b;
			},
			easeOutQuad: function(t, b, c, d) {
				return -c *(t/=d)*(t-2) + b;
			},
			easeInOutQuad: function(t, b, c, d) {
				if ((t/=d/2) < 1) return c/2*t*t + b;
				return -c/2 * ((--t)*(t-2) - 1) + b;
			},
			easeInCubic: function(t, b, c, d) {
				return c*(t/=d)*t*t + b;
			},
			easeOutCubic: function(t, b, c, d) {
				return c*((t=t/d-1)*t*t + 1) + b;
			},
			easeInOutCubic: function(t, b, c, d) {
				if ((t/=d/2) < 1) return c/2*t*t*t + b;
				return c/2*((t-=2)*t*t + 2) + b;
			},
			easeInQuart: function(t, b, c, d) {
				return c*(t/=d)*t*t*t + b;
			},
			easeOutQuart: function(t, b, c, d) {
				return -c * ((t=t/d-1)*t*t*t - 1) + b;
			},
			easeInOutQuart: function(t, b, c, d) {
				if ((t/=d/2) < 1) return c/2*t*t*t*t + b;
				return -c/2 * ((t-=2)*t*t*t - 2) + b;
			},
			easeInQuint: function(t, b, c, d) {
				return c*(t/=d)*t*t*t*t + b;
			},
			easeOutQuint: function(t, b, c, d) {
				return c*((t=t/d-1)*t*t*t*t + 1) + b;
			},
			easeInOutQuint: function(t, b, c, d) {
				if ((t/=d/2) < 1) return c/2*t*t*t*t*t + b;
				return c/2*((t-=2)*t*t*t*t + 2) + b;
			},
			easeInSine: function(t, b, c, d) {
				return -c * Math.cos(t/d * (Math.PI/2)) + c + b;
			},
			easeOutSine: function(t, b, c, d) {
				return c * Math.sin(t/d * (Math.PI/2)) + b;
			},
			easeInOutSine: function(t, b, c, d) {
				return -c/2 * (Math.cos(Math.PI*t/d) - 1) + b;
			},
			easeInExpo: function(t, b, c, d) {
				return (t==0) ? b : c * Math.pow(2, 10 * (t/d - 1)) + b;
			},
			easeOutExpo: function(t, b, c, d) {
				return (t==d) ? b+c : c * (-Math.pow(2, -10 * t/d) + 1) + b;
			},
			easeInOutExpo: function(t, b, c, d) {
				if (t==0) return b;
				if (t==d) return b+c;
				if ((t/=d/2) < 1) return c/2 * Math.pow(2, 10 * (t - 1)) + b;
				return c/2 * (-Math.pow(2, -10 * --t) + 2) + b;
			},
			easeInCirc: function(t, b, c, d) {
				return -c * (Math.sqrt(1 - (t/=d)*t) - 1) + b;
			},
			easeOutCirc: function(t, b, c, d) {
				return c * Math.sqrt(1 - (t=t/d-1)*t) + b;
			},
			easeInOutCirc: function(t, b, c, d) {
				if ((t/=d/2) < 1) return -c/2 * (Math.sqrt(1 - t*t) - 1) + b;
				return c/2 * (Math.sqrt(1 - (t-=2)*t) + 1) + b;
			},
			easeInElastic: function(t, b, c, d) {
				var s=1.70158;var p=0;var a=c;
				if (t==0) return b;  if ((t/=d)==1) return b+c;  if (!p) p=d*.3;
				if (a < Math.abs(c)) { a=c; var s=p/4; }
				else var s = p/(2*Math.PI) * Math.asin (c/a);
				return -(a*Math.pow(2,10*(t-=1)) * Math.sin( (t*d-s)*(2*Math.PI)/p )) + b;
			},
			easeOutElastic: function(t, b, c, d) {
				var s=1.70158;var p=0;var a=c;
				if (t==0) return b;  if ((t/=d)==1) return b+c;  if (!p) p=d*.3;
				if (a < Math.abs(c)) { a=c; var s=p/4; }
				else var s = p/(2*Math.PI) * Math.asin (c/a);
				return a*Math.pow(2,-10*t) * Math.sin( (t*d-s)*(2*Math.PI)/p ) + c + b;
			},
			easeInOutElastic: function(t, b, c, d) {
				var s=1.70158;var p=0;var a=c;
				if (t==0) return b;  if ((t/=d/2)==2) return b+c;  if (!p) p=d*(.3*1.5);
				if (a < Math.abs(c)) { a=c; var s=p/4; }
				else var s = p/(2*Math.PI) * Math.asin (c/a);
				if (t < 1) return -.5*(a*Math.pow(2,10*(t-=1)) * Math.sin( (t*d-s)*(2*Math.PI)/p )) + b;
				return a*Math.pow(2,-10*(t-=1)) * Math.sin( (t*d-s)*(2*Math.PI)/p )*.5 + c + b;
			},
			easeInBack: function(t, b, c, d, s) {
				if (s == undefined) s = 1.70158;
				return c*(t/=d)*t*((s+1)*t - s) + b;
			},
			easeOutBack: function(t, b, c, d, s) {
				if (s == undefined) s = 1.70158;
				return c*((t=t/d-1)*t*((s+1)*t + s) + 1) + b;
			},
			easeInOutBack: function(t, b, c, d, s) {
				if (s == undefined) s = 1.70158; 
				if ((t/=d/2) < 1) return c/2*(t*t*(((s*=(1.525))+1)*t - s)) + b;
				return c/2*((t-=2)*t*(((s*=(1.525))+1)*t + s) + 2) + b;
			},
			easeInBounce: function(t, b, c, d) {
				return c - this.easeOutBounce(d-t, 0, c, d) + b;
			},
			easeOutBounce: function(t, b, c, d) {
				if ((t/=d) < (1/2.75)) {
					return c*(7.5625*t*t) + b;
				} else if (t < (2/2.75)) {
					return c*(7.5625*(t-=(1.5/2.75))*t + .75) + b;
				} else if (t < (2.5/2.75)) {
					return c*(7.5625*(t-=(2.25/2.75))*t + .9375) + b;
				} else {
					return c*(7.5625*(t-=(2.625/2.75))*t + .984375) + b;
				}
			},
			easeInOutBounce: function(t, b, c, d) {
				if (t < d/2) return this.easeInBounce(t*2, 0, c, d) * .5 + b;
				return this.easeOutBounce(t*2-d, 0, c, d) * .5 + c*.5 + b;
			}
		};
		this.change = {};
		this.duration = duration;
		this.elapsed = 0.0;
		this.object = object;
		this.startValues = {};
		this.type = easingType in this.easers ? easingType : 'linear';
		var isChanged = false;
		for (var p in endValues) {
			this.change[p] = endValues[p] - object[p];
			this.startValues[p] = object[p];
			isChanged = isChanged || this.change[p] != 0;
		}
		if (!isChanged) {
			this.elapsed = this.duration;
		}
	},
	update: function(scene) {
		this.elapsed += 1.0 / screen.frameRate;
		if (this.elapsed < this.duration) {
			for (var p in this.change) {
				this.object[p] = this.easers[this.type](this.elapsed, this.startValues[p], this.change[p], this.duration);
			}
			return true;
		} else {
			return false;
		}
	},
	finish: function(scene) {
		for (var p in this.change) {
			this.object[p] = this.startValues[p] + this.change[p];
		}
	}
});
