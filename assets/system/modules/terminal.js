/**
 *  miniRT terminal CommonJS module
 *  an easy-to-integrate debug console for Sphere games
 *  (c) 2015-2016 Fat Cerberus
**/

'use strict';
module.exports =
{
	isOpen:     isOpen,
	close:      close,
	open:       open,
	print:      print,
	register:   register,
	unregister: unregister
};

const link    = require('link');
const prim    = require('prim');
const scenes  = require('scenes');
const threads = require('threads');

var font = Font.Default;
var nextLine = 0;
var numLines = 0;
var visible = { yes: false, fade: 0.0, line: 0.0 };
var wasKeyDown = false;

var numLines = Math.floor((screen.height - 32) / font.height);
var bufferSize = 1000;
var prompt = "$";

var buffer = [];
var commands = [];
var entry = "";
var cursorColor = new Color(255, 255, 128, 255);
new scenes.Scene()
	.doWhile(function() { return true; })
		.tween(cursorColor, 0.25, 'easeInSine', { alpha: 255 })
		.tween(cursorColor, 0.25, 'easeOutSine', { alpha: 64 })
	.end()
	.run();
threads.create({
	update:	  update,
	render:	  render,
	getInput: getInput,
}, Infinity);

print(engine.game.name + " Console");
print(engine.name + " " + engine.version + " / API v" + engine.apiVersion + " Lv." + engine.apiLevel);
print("");

function executeCommand(command)
{
	// NOTES:
	//    * Command format is `<entity_name> <instruction> <arg_1> ... <arg_n>`
	//      e.g.: `cow eat kitties 100`
	//    * Quoted text (single or double quotes) is treated as a single token.
	//    * Numeric arguments are converted to actual JS numbers before being passed to an
	//      instruction method.

	// tokenize the command string
	var tokens = command.match(/'.*?'|".*?"|\S+/g);
	if (tokens == null) return;
	for (var i = 0; i < tokens.length; ++i) {
		tokens[i] = tokens[i].replace(/'(.*)'/, "$1");
		tokens[i] = tokens[i].replace(/"(.*)"/, "$1");
	}
	var entity = tokens[0];
	var instruction = tokens[1];

	// check that the instruction is valid
	if (!link(commands)
		.pluck('entity')
		.contains(entity))
	{
		print("Entity name '" + entity + "' not recognized");
		return;
	}
	if (tokens.length < 2) {
		print("No instruction provided for '" + entity + "'");
		return;
	}
	if (!link(commands)
		.filterBy('entity', entity)
		.pluck('instruction')
		.contains(instruction))
	{
		print("Instruction '" + instruction + "' not valid for '" + entity + "'");
		return;
	}

	// parse arguments
	for (var i = 2; i < tokens.length; ++i) {
		var maybeNumber = parseFloat(tokens[i]);
		tokens[i] = !isNaN(maybeNumber) ? maybeNumber : tokens[i];
	}

	// execute the command
	link(commands)
		.filterBy('instruction', instruction)
		.filterBy('entity', entity)
		.each(function(desc)
	{
		threads.create({
			update: function() {
				desc.method.apply(desc.that, tokens.slice(2));
			}
		});
	});
}

function getInput()
{
	var consoleKey = GetPlayerKey(PLAYER_1, PLAYER_KEY_MENU);
	if (!wasKeyDown && IsKeyPressed(consoleKey)) {
		if (!isOpen())
			open();
		else
			close();
	}
	wasKeyDown = IsKeyPressed(consoleKey);
	if (isOpen()) {
		var wheelKey = GetNumMouseWheelEvents() > 0 ? GetMouseWheelEvent() : null;
		var speed = wheelKey != null ? 1.0 : 0.5;
		if (IsKeyPressed(KEY_PAGEUP) || wheelKey == MOUSE_WHEEL_UP) {
			visible.line = Math.min(visible.line + speed, buffer.length - numLines);
		} else if (IsKeyPressed(KEY_PAGEDOWN) || wheelKey == MOUSE_WHEEL_DOWN) {
			visible.line = Math.max(visible.line - speed, 0);
		}
		var keycode = AreKeysLeft() ? GetKey() : null;
		switch (keycode) {
			case KEY_ENTER:
				print("Command entered: '" + entry + "'");
				executeCommand(entry);
				entry = "";
				break;
			case KEY_BACKSPACE:
				entry = entry.slice(0, -1);
				break;
			case KEY_HOME:
				var newLine = buffer.length - numLines;
				new scenes.Scene()
					.tween(visible, 0.125, 'easeOut', { line: newLine })
					.run();
				break;
			case KEY_END:
				new scenes.Scene()
					.tween(visible, 0.125, 'easeOut', { line: 0.0 })
					.run();
				break;
			case KEY_TAB: break;
			case null: break;
			default:
				var ch = GetKeyString(keycode, IsKeyPressed(KEY_SHIFT));
				ch = GetToggleState(KEY_CAPSLOCK) ? ch.toUpperCase() : ch;
				entry += ch;
		}
	}
}

function render()
{
	if (visible.fade <= 0.0)
		return;

	// draw the command prompt...
	var boxY = -22 * (1.0 - visible.fade);
	prim.rect(screen, 0, boxY, screen.width, 22, Color.Black.fade(visible.fade * 224));
	var promptWidth = font.getStringWidth(prompt + " ");
	font.drawText(screen, 6, 6 + boxY, prompt, Color.Black.fade(visible.fade * 192));
	font.drawText(screen, 5, 5 + boxY, prompt, Color.Gray.fade(visible.fade * 192));
	font.drawText(screen, 6 + promptWidth, 6 + boxY, entry, Color.Black.fade(visible.fade * 192));
	font.drawText(screen, 5 + promptWidth, 5 + boxY, entry, new Color(255, 255, 128, visible.fade * 192));
	font.drawText(screen, 5 + promptWidth + font.getStringWidth(entry), 5 + boxY, "_", cursorColor);

	// ...then the console output
	var boxHeight = numLines * font.height + 10;
	var boxY = screen.height - boxHeight * visible.fade;
	prim.rect(screen, 0, boxY, screen.width, boxHeight, new Color(0, 0, 0, visible.fade * 192));
	screen.clipTo(5, boxY + 5, screen.width - 10, boxHeight - 10);
	for (var i = -1; i < numLines + 1; ++i) {
		var lineToDraw = (nextLine - numLines) + i - Math.floor(visible.line);
		var lineInBuffer = lineToDraw % bufferSize;
		if (lineToDraw >= 0 && buffer[lineInBuffer] != null) {
			var y = boxY + 5 + i * font.height;
			y += (visible.line - Math.floor(visible.line)) * font.height;
			font.drawText(screen, 6, y + 1, buffer[lineInBuffer], Color.Black.fade(visible.fade * 192));
			font.drawText(screen, 5, y, buffer[lineInBuffer], Color.White.fade(visible.fade * 192));
		}
	}
	screen.clipTo(0, 0, screen.width, screen.height);
}

function update()
{
	if (visible.fade <= 0.0) {
		visible.line = 0.0;
	}
	return true;
}

// terminal.isOpen()
// determine whether the console is currently displayed or not.
// returns:
//     true if the console is open, false otherwise.
function isOpen()
{
	return visible.yes;
}

// terminal.close()
// close the console, hiding it from view.
function close()
{
	new scenes.Scene()
		.tween(visible, 0.25, 'easeInQuad', { fade: 0.0 })
		.call(function() { visible.yes = false; entry = ""; })
		.run();
}

// terminal.print()
// print a line of text to the terminal.
function print(/*...*/)
{
	var lineInBuffer = nextLine % bufferSize;
	buffer[lineInBuffer] = ">" + arguments[0];
	for (var i = 1; i < arguments.length; ++i) {
		buffer[lineInBuffer] += " >>" + arguments[1];
	}
	++nextLine;
	visible.line = 0.0;
	console.log(buffer[lineInBuffer]);
}

// terminal.open()
// open the terminal, making it visible to the player.
function open()
{
	new scenes.Scene()
		.tween(visible, 0.25, 'easeOutQuad', { fade: 1.0 })
		.call(function() { visible.yes = true; })
		.run();
}

// terminal.register()
// register a named entity with the terminal.
// arguments:
//     name:    the name of the entity.	 this should not contain spaces.
//     that:    the value which will be bound to `this` when one of the entity's methods is executed.
//     methods: an associative array of functions, keyed by name, defining the valid operations
//              for this entity.  one-word names are recommended and as with the entity name,
//              should not contain spaces.
function register(name, that, methods)
{
	for (var instruction in methods) {
		commands.push({
			entity: name,
			instruction: instruction,
			that: that,
			method: methods[instruction]
		});
	}
}

// terminal.unregister()
// unregister all commands for a previously-registered entity.
// arguments:
//     name: the name of the entity as passed to terminal.register().
function unregister(name)
{
	commands = link(commands)
		.where(function(command) { return command.entity != name; })
		.toArray();
}
