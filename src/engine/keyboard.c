#include "minisphere.h"
#include "keyboard.h"

#include "api.h"
#include "debugger.h"
#include "vector.h"

struct key_queue
{
	int num_keys;
	int keys[255];
};

static void queue_key (int keycode);

static ALLEGRO_EVENT_QUEUE* s_events;
static struct key_queue     s_key_queue;
static bool                 s_key_state[ALLEGRO_KEY_MAX];
static int                  s_keymod_state;

void
initialize_keyboard(void)
{
	console_log(1, "initializing keyboard subsystem");
	
	al_install_keyboard();
	s_events = al_create_event_queue();
	al_register_event_source(s_events, al_get_keyboard_event_source());
}

void
shutdown_keyboard(void)
{
	console_log(1, "shutting down keyboard subsystem");

	al_destroy_event_queue(s_events);
	al_uninstall_keyboard();
}

bool
is_any_key_down(void)
{
	int i;

	update_keyboard();
	for (i = 0; i < ALLEGRO_KEY_MAX; ++i)
		if (s_key_state[i]) return true;
	return false;
}

bool
is_key_down(int keycode)
{
	update_keyboard();
	return s_key_state[keycode];
}

bool
is_key_toggled(int keycode)
{
	int flag;
	
	flag = keycode == ALLEGRO_KEY_CAPSLOCK ? ALLEGRO_KEYMOD_CAPSLOCK
		: keycode == ALLEGRO_KEY_NUMLOCK ? ALLEGRO_KEYMOD_NUMLOCK
		: keycode == ALLEGRO_KEY_SCROLLLOCK ? ALLEGRO_KEYMOD_SCROLLLOCK
		: 0x0;
	return (s_keymod_state & flag) != 0;
}

void
attach_input_display(void)
{
	al_register_event_source(s_events,
		al_get_display_event_source(screen_display(g_screen)));
}

void
clear_key_queue(void)
{
	s_key_queue.num_keys = 0;
}

int
get_num_keys(void)
{
	return s_key_queue.num_keys;
}

int
read_key(void)
{
	int keycode;
	
	while (s_key_queue.num_keys == 0)
		do_events();
	keycode = s_key_queue.keys[0];
	--s_key_queue.num_keys;
	memmove(s_key_queue.keys, &s_key_queue.keys[1], sizeof(int) * s_key_queue.num_keys);
	return keycode;
}

void
update_keyboard(void)
{
	ALLEGRO_EVENT          event;
	int                    keycode;

	// process Allegro keyboard events
	while (al_get_next_event(s_events, &event)) {
		switch (event.type) {
		case ALLEGRO_EVENT_DISPLAY_SWITCH_OUT:
			// Alt+Tabbing out can cause keys to get "stuck", this works around it
			// by clearing the states when switching away.
			memset(s_key_state, 0, ALLEGRO_KEY_MAX * sizeof(bool));
			break;
		case ALLEGRO_EVENT_KEY_DOWN:
			keycode = event.keyboard.keycode;
			s_key_state[keycode] = true;
			break;
		case ALLEGRO_EVENT_KEY_UP:
			s_key_state[event.keyboard.keycode] = false;
			break;
		case ALLEGRO_EVENT_KEY_CHAR:
			s_keymod_state = event.keyboard.modifiers;
			switch (event.keyboard.keycode) {
			case ALLEGRO_KEY_ENTER:
				if (event.keyboard.modifiers & ALLEGRO_KEYMOD_ALT
				 || event.keyboard.modifiers & ALLEGRO_KEYMOD_ALTGR)
				{
					screen_toggle_fullscreen(g_screen);
				}
				else {
					queue_key(event.keyboard.keycode);
				}
				break;
			case ALLEGRO_KEY_F10:
				screen_toggle_fullscreen(g_screen);
				break;
			case ALLEGRO_KEY_F11:
				screen_toggle_fps(g_screen);
				break;
			case ALLEGRO_KEY_F12:
				if (is_debugger_attached())
					duk_debugger_pause(g_duk);
				else
					screen_queue_screenshot(g_screen);
				break;
			default:
				queue_key(event.keyboard.keycode);
				break;
			}
		}
	}
}

static void
queue_key(int keycode)
{
	int key_index;

	if (s_key_queue.num_keys < 255) {
		key_index = s_key_queue.num_keys;
		++s_key_queue.num_keys;
		s_key_queue.keys[key_index] = keycode;
	}
}
