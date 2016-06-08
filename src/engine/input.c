#include "minisphere.h"
#include "input.h"

#include "api.h"
#include "debugger.h"
#include "vector.h"

#define MAX_JOYSTICKS   4
#define MAX_JOY_BUTTONS 32

struct key_queue
{
	int num_keys;
	int keys[255];
};

enum mouse_button
{
	MOUSE_BUTTON_LEFT,
	MOUSE_BUTTON_RIGHT,
	MOUSE_BUTTON_MIDDLE
};

enum mouse_wheel_event
{
	MOUSE_WHEEL_UP,
	MOUSE_WHEEL_DOWN
};

static duk_ret_t js_AreKeysLeft             (duk_context* ctx);
static duk_ret_t js_IsAnyKeyPressed         (duk_context* ctx);
static duk_ret_t js_IsJoystickButtonPressed (duk_context* ctx);
static duk_ret_t js_IsKeyPressed            (duk_context* ctx);
static duk_ret_t js_IsMouseButtonPressed    (duk_context* ctx);
static duk_ret_t js_GetJoystickAxis         (duk_context* ctx);
static duk_ret_t js_GetKey                  (duk_context* ctx);
static duk_ret_t js_GetKeyString            (duk_context* ctx);
static duk_ret_t js_GetNumMouseWheelEvents  (duk_context* ctx);
static duk_ret_t js_GetMouseWheelEvent      (duk_context* ctx);
static duk_ret_t js_GetMouseX               (duk_context* ctx);
static duk_ret_t js_GetMouseY               (duk_context* ctx);
static duk_ret_t js_GetNumJoysticks         (duk_context* ctx);
static duk_ret_t js_GetNumJoystickAxes      (duk_context* ctx);
static duk_ret_t js_GetNumJoystickButtons   (duk_context* ctx);
static duk_ret_t js_GetNumMouseWheelEvents  (duk_context* ctx);
static duk_ret_t js_GetPlayerKey            (duk_context* ctx);
static duk_ret_t js_GetToggleState          (duk_context* ctx);
static duk_ret_t js_SetMousePosition        (duk_context* ctx);
static duk_ret_t js_SetPlayerKey            (duk_context* ctx);
static duk_ret_t js_BindKey                 (duk_context* ctx);
static duk_ret_t js_BindJoystickButton      (duk_context* ctx);
static duk_ret_t js_ClearKeyQueue           (duk_context* ctx);
static duk_ret_t js_UnbindKey               (duk_context* ctx);
static duk_ret_t js_UnbindJoystickButton    (duk_context* ctx);

static void bind_button       (vector_t* bindings, int joy_index, int button, script_t* on_down_script, script_t* on_up_script);
static void bind_key          (vector_t* bindings, int keycode, script_t* on_down_script, script_t* on_up_script);
static void queue_key         (int keycode);
static void queue_wheel_event (int event);

static vector_t*            s_bound_buttons;
static vector_t*            s_bound_keys;
static vector_t*            s_bound_map_keys;
static ALLEGRO_EVENT_QUEUE* s_events;
static bool                 s_have_joystick;
static bool                 s_have_mouse;
static ALLEGRO_JOYSTICK*    s_joy_handles[MAX_JOYSTICKS];
static int                  s_key_map[4][PLAYER_KEY_MAX];
static struct key_queue     s_key_queue;
static bool                 s_key_state[ALLEGRO_KEY_MAX];
static int                  s_keymod_state;
static int                  s_last_wheel_pos = 0;
static int                  s_num_joysticks = 0;
static int                  s_num_wheel_events = 0;
static bool                 s_has_keymap_changed = false;
static int                  s_wheel_queue[255];

struct bound_button
{
	int       joystick_id;
	int       button;
	bool      is_pressed;
	script_t* on_down_script;
	script_t* on_up_script;
};

struct bound_key
{
	int       keycode;
	bool      is_pressed;
	script_t* on_down_script;
	script_t* on_up_script;
};

void
initialize_input(void)
{
	int i;

	console_log(1, "initializing input");
	
	al_install_keyboard();
	if (!(s_have_mouse = al_install_mouse()))
		console_log(1, "  mouse initialization failed");
	if (!(s_have_joystick = al_install_joystick()))
		console_log(1, "  joystick initialization failed");

	s_events = al_create_event_queue();
	al_register_event_source(s_events, al_get_keyboard_event_source());
	if (s_have_mouse)
		al_register_event_source(s_events, al_get_mouse_event_source());
	if (s_have_joystick)
		al_register_event_source(s_events, al_get_joystick_event_source());

	// look for active joysticks
	if (s_have_joystick) {
		s_num_joysticks = fmin(MAX_JOYSTICKS, al_get_num_joysticks());
		for (i = 0; i < MAX_JOYSTICKS; ++i)
			s_joy_handles[i] = i < s_num_joysticks ? al_get_joystick(i) : NULL;
	}

	// create bound key vectors
	s_bound_buttons = vector_new(sizeof(struct bound_button));
	s_bound_keys = vector_new(sizeof(struct bound_key));
	s_bound_map_keys = vector_new(sizeof(struct bound_key));

	// fill in default player key map
	memset(s_key_map, 0, sizeof(s_key_map));
	s_key_map[0][PLAYER_KEY_UP] = ALLEGRO_KEY_UP;
	s_key_map[0][PLAYER_KEY_DOWN] = ALLEGRO_KEY_DOWN;
	s_key_map[0][PLAYER_KEY_LEFT] = ALLEGRO_KEY_LEFT;
	s_key_map[0][PLAYER_KEY_RIGHT] = ALLEGRO_KEY_RIGHT;
	s_key_map[0][PLAYER_KEY_A] = ALLEGRO_KEY_Z;
	s_key_map[0][PLAYER_KEY_B] = ALLEGRO_KEY_X;
	s_key_map[0][PLAYER_KEY_X] = ALLEGRO_KEY_C;
	s_key_map[0][PLAYER_KEY_Y] = ALLEGRO_KEY_V;
	s_key_map[0][PLAYER_KEY_MENU] = ALLEGRO_KEY_TAB;
	s_key_map[1][PLAYER_KEY_UP] = ALLEGRO_KEY_W;
	s_key_map[1][PLAYER_KEY_DOWN] = ALLEGRO_KEY_S;
	s_key_map[1][PLAYER_KEY_LEFT] = ALLEGRO_KEY_A;
	s_key_map[1][PLAYER_KEY_RIGHT] = ALLEGRO_KEY_D;
	s_key_map[1][PLAYER_KEY_A] = ALLEGRO_KEY_1;
	s_key_map[1][PLAYER_KEY_B] = ALLEGRO_KEY_2;
	s_key_map[1][PLAYER_KEY_X] = ALLEGRO_KEY_3;
	s_key_map[1][PLAYER_KEY_Y] = ALLEGRO_KEY_4;
	s_key_map[1][PLAYER_KEY_MENU] = ALLEGRO_KEY_TAB;
	s_key_map[2][PLAYER_KEY_UP] = ALLEGRO_KEY_PAD_8;
	s_key_map[2][PLAYER_KEY_DOWN] = ALLEGRO_KEY_PAD_2;
	s_key_map[2][PLAYER_KEY_LEFT] = ALLEGRO_KEY_PAD_4;
	s_key_map[2][PLAYER_KEY_RIGHT] = ALLEGRO_KEY_PAD_6;
	s_key_map[2][PLAYER_KEY_A] = ALLEGRO_KEY_PAD_PLUS;
	s_key_map[2][PLAYER_KEY_B] = ALLEGRO_KEY_PAD_MINUS;
	s_key_map[2][PLAYER_KEY_X] = ALLEGRO_KEY_PAD_0;
	s_key_map[2][PLAYER_KEY_Y] = ALLEGRO_KEY_PAD_DELETE;
	s_key_map[2][PLAYER_KEY_MENU] = ALLEGRO_KEY_TAB;
	s_key_map[3][PLAYER_KEY_UP] = ALLEGRO_KEY_I;
	s_key_map[3][PLAYER_KEY_DOWN] = ALLEGRO_KEY_K;
	s_key_map[3][PLAYER_KEY_LEFT] = ALLEGRO_KEY_J;
	s_key_map[3][PLAYER_KEY_RIGHT] = ALLEGRO_KEY_L;
	s_key_map[3][PLAYER_KEY_A] = ALLEGRO_KEY_7;
	s_key_map[3][PLAYER_KEY_B] = ALLEGRO_KEY_8;
	s_key_map[3][PLAYER_KEY_X] = ALLEGRO_KEY_9;
	s_key_map[3][PLAYER_KEY_Y] = ALLEGRO_KEY_0;
	s_key_map[3][PLAYER_KEY_MENU] = ALLEGRO_KEY_TAB;
}

void
shutdown_input(void)
{
	struct bound_button* pbutton;
	struct bound_key*    pkey;
	
	iter_t iter;

	// save player key mappings
	console_log(1, "shutting down input");

	// free bound key scripts
	iter = vector_enum(s_bound_buttons);
	while (pbutton = vector_next(&iter)) {
		free_script(pbutton->on_down_script);
		free_script(pbutton->on_up_script);
	}
	iter = vector_enum(s_bound_keys);
	while (pkey = vector_next(&iter)) {
		free_script(pkey->on_down_script);
		free_script(pkey->on_up_script);
	}
	iter = vector_enum(s_bound_map_keys);
	while (pkey = vector_next(&iter)) {
		free_script(pkey->on_down_script);
		free_script(pkey->on_up_script);
	}
	vector_free(s_bound_buttons);
	vector_free(s_bound_keys);
	vector_free(s_bound_map_keys);

	// shut down Allegro input
	al_destroy_event_queue(s_events);
	al_uninstall_joystick();
	al_uninstall_mouse();
	al_uninstall_keyboard();
}

bool
is_any_key_down(void)
{
	int i_key;

	update_input();
	for (i_key = 0; i_key < ALLEGRO_KEY_MAX; ++i_key)
		if (s_key_state[i_key]) return true;
	return false;
}

bool
is_joy_button_down(int joy_index, int button)
{
	ALLEGRO_JOYSTICK_STATE joy_state;
	ALLEGRO_JOYSTICK*      joystick;

	if (!s_have_joystick)
		return false;
	if (!(joystick = s_joy_handles[joy_index]))
		return 0.0;
	al_get_joystick_state(joystick, &joy_state);
	return joy_state.button[button] > 0;
}

bool
is_key_down(int keycode)
{
	bool is_pressed;
	
	update_input();
	switch (keycode) {
	case ALLEGRO_KEY_LSHIFT:
		is_pressed = s_key_state[ALLEGRO_KEY_LSHIFT]
			|| s_key_state[ALLEGRO_KEY_RSHIFT];
		break;
	case ALLEGRO_KEY_LCTRL:
		is_pressed = s_key_state[ALLEGRO_KEY_LCTRL]
			|| s_key_state[ALLEGRO_KEY_RCTRL];
		break;
	case ALLEGRO_KEY_ALT:
		is_pressed = s_key_state[ALLEGRO_KEY_ALT]
			|| s_key_state[ALLEGRO_KEY_ALTGR];
		break;
	default:
		is_pressed = s_key_state[keycode];
	}
	return is_pressed;
}

float
get_joy_axis(int joy_index, int axis_index)
{
	ALLEGRO_JOYSTICK_STATE joy_state;
	ALLEGRO_JOYSTICK*      joystick;
	int                    n_stick_axes;
	int                    n_sticks;

	int i;

	if (!s_have_joystick || !(joystick = s_joy_handles[joy_index]))
		return 0.0;
	al_get_joystick_state(joystick, &joy_state);
	n_sticks = al_get_joystick_num_sticks(joystick);
	for (i = 0; i < n_sticks; ++i) {
		n_stick_axes = al_get_joystick_num_axes(joystick, i);
		if (axis_index < n_stick_axes)
			return joy_state.stick[i].axis[axis_index];
		axis_index -= n_stick_axes;
	}
	return 0.0;
}

int
get_joy_axis_count(int joy_index)
{
	ALLEGRO_JOYSTICK* joystick;
	int               n_axes;
	int               n_sticks;

	int i;
	
	if (!s_have_joystick || !(joystick = s_joy_handles[joy_index]))
		return 0;
	n_sticks = al_get_joystick_num_sticks(joystick);
	n_axes = 0;
	for (i = 0; i < n_sticks; ++i)
		n_axes += al_get_joystick_num_axes(joystick, i);
	return n_axes;
}

int
get_joy_button_count(int joy_index)
{
	ALLEGRO_JOYSTICK* joystick;

	if (!s_have_joystick || !(joystick = s_joy_handles[joy_index]))
		return 0;
	return al_get_joystick_num_buttons(joystick);
}

int
get_player_key(int player, player_key_t vkey)
{
	return s_key_map[player][vkey];
}

void
attach_input_display(void)
{
	al_register_event_source(s_events,
		al_get_display_event_source(screen_display(g_screen)));
}

void
set_player_key(int player, player_key_t vkey, int keycode)
{
	s_key_map[player][vkey] = keycode;
	s_has_keymap_changed = g_game_path != NULL;
}

void
clear_key_queue(void)
{
	s_key_queue.num_keys = 0;
}

void
update_bound_keys(bool use_map_keys)
{
	struct bound_button* button;
	struct bound_key*    key;
	bool                 is_down;
	
	iter_t iter;

	// check bound keyboard keys
	if (use_map_keys) {
		iter = vector_enum(s_bound_map_keys);
		while (key = vector_next(&iter)) {
			is_down = s_key_state[key->keycode];
			if (is_down && !key->is_pressed)
				run_script(key->on_down_script, false);
			if (!is_down && key->is_pressed)
				run_script(key->on_up_script, false);
			key->is_pressed = is_down;
		}
	}
	iter = vector_enum(s_bound_keys);
	while (key = vector_next(&iter)) {
		is_down = s_key_state[key->keycode];
		if (is_down && !key->is_pressed)
			run_script(key->on_down_script, false);
		if (!is_down && key->is_pressed)
			run_script(key->on_up_script, false);
		key->is_pressed = is_down;
	}

	// check bound joystick buttons
	iter = vector_enum(s_bound_buttons);
	while (button = vector_next(&iter)) {
		is_down = is_joy_button_down(button->joystick_id, button->button);
		if (is_down && !button->is_pressed)
			run_script(button->on_down_script, false);
		if (!is_down && button->is_pressed)
			run_script(button->on_up_script, false);
		button->is_pressed = is_down;
	}
}

void
update_input(void)
{
	ALLEGRO_EVENT          event;
	int                    keycode;
	ALLEGRO_MOUSE_STATE    mouse_state;
	
	// process Allegro input events
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
			
			// queue Ctrl/Alt/Shift keys (Sphere compatibility hack)
			if (keycode == ALLEGRO_KEY_LCTRL || keycode == ALLEGRO_KEY_RCTRL
				|| keycode == ALLEGRO_KEY_ALT || keycode == ALLEGRO_KEY_ALTGR
				|| keycode == ALLEGRO_KEY_LSHIFT || keycode == ALLEGRO_KEY_RSHIFT)
			{
				if (keycode == ALLEGRO_KEY_LCTRL || keycode == ALLEGRO_KEY_RCTRL)
					queue_key(ALLEGRO_KEY_LCTRL);
				if (keycode == ALLEGRO_KEY_ALT || keycode == ALLEGRO_KEY_ALTGR)
					queue_key(ALLEGRO_KEY_ALT);
				if (keycode == ALLEGRO_KEY_LSHIFT || keycode == ALLEGRO_KEY_RSHIFT)
					queue_key(ALLEGRO_KEY_LSHIFT);
			}
			
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
	
	// check whether mouse wheel moved since last update
	if (s_have_mouse) {
		al_get_mouse_state(&mouse_state);
		if (mouse_state.z > s_last_wheel_pos)
			queue_wheel_event(MOUSE_WHEEL_UP);
		if (mouse_state.z < s_last_wheel_pos)
			queue_wheel_event(MOUSE_WHEEL_DOWN);
		s_last_wheel_pos = mouse_state.z;
	}
}

static void
bind_button(vector_t* bindings, int joy_index, int button, script_t* on_down_script, script_t* on_up_script)
{
	bool                 is_new_entry = true;
	struct bound_button* bound;
	struct bound_button  new_binding;
	script_t*            old_down_script;
	script_t*            old_up_script;

	iter_t iter;

	new_binding.joystick_id = joy_index;
	new_binding.button = button;
	new_binding.is_pressed = false;
	new_binding.on_down_script = on_down_script;
	new_binding.on_up_script = on_up_script;
	iter = vector_enum(bindings);
	while (bound = vector_next(&iter)) {
		if (bound->joystick_id == joy_index && bound->button == button) {
			bound->is_pressed = false;
			old_down_script = bound->on_down_script;
			old_up_script = bound->on_up_script;
			memcpy(bound, &new_binding, sizeof(struct bound_button));
			if (old_down_script != bound->on_down_script)
				free_script(old_down_script);
			if (old_up_script != bound->on_up_script)
				free_script(old_up_script);
			is_new_entry = false;
		}
	}
	if (is_new_entry)
		vector_push(bindings, &new_binding);
}

static void
bind_key(vector_t* bindings, int keycode, script_t* on_down_script, script_t* on_up_script)
{
	bool              is_new_key = true;
	struct bound_key* key;
	struct bound_key  new_binding;
	script_t*         old_down_script;
	script_t*         old_up_script;

	iter_t iter;

	new_binding.keycode = keycode;
	new_binding.is_pressed = false;
	new_binding.on_down_script = on_down_script;
	new_binding.on_up_script = on_up_script;
	iter = vector_enum(bindings);
	while (key = vector_next(&iter)) {
		if (key->keycode == keycode) {
			key->is_pressed = false;
			old_down_script = key->on_down_script;
			old_up_script = key->on_up_script;
			memcpy(key, &new_binding, sizeof(struct bound_key));
			if (old_down_script != key->on_down_script)
				free_script(old_down_script);
			if (old_up_script != key->on_up_script)
				free_script(old_up_script);
			is_new_key = false;
		}
	}
	if (is_new_key)
		vector_push(bindings, &new_binding);
}

void
init_input_api(void)
{
	api_register_const(g_duk, NULL, "PLAYER_1", 0);
	api_register_const(g_duk, NULL, "PLAYER_2", 1);
	api_register_const(g_duk, NULL, "PLAYER_3", 2);
	api_register_const(g_duk, NULL, "PLAYER_4", 3);
	api_register_const(g_duk, NULL, "PLAYER_KEY_MENU", PLAYER_KEY_MENU);
	api_register_const(g_duk, NULL, "PLAYER_KEY_UP", PLAYER_KEY_UP);
	api_register_const(g_duk, NULL, "PLAYER_KEY_DOWN", PLAYER_KEY_DOWN);
	api_register_const(g_duk, NULL, "PLAYER_KEY_LEFT", PLAYER_KEY_LEFT);
	api_register_const(g_duk, NULL, "PLAYER_KEY_RIGHT", PLAYER_KEY_RIGHT);
	api_register_const(g_duk, NULL, "PLAYER_KEY_A", PLAYER_KEY_A);
	api_register_const(g_duk, NULL, "PLAYER_KEY_B", PLAYER_KEY_B);
	api_register_const(g_duk, NULL, "PLAYER_KEY_X", PLAYER_KEY_X);
	api_register_const(g_duk, NULL, "PLAYER_KEY_Y", PLAYER_KEY_Y);
	api_register_const(g_duk, NULL, "KEY_NONE", 0);
	api_register_const(g_duk, NULL, "KEY_SHIFT", ALLEGRO_KEY_LSHIFT);
	api_register_const(g_duk, NULL, "KEY_CTRL", ALLEGRO_KEY_LCTRL);
	api_register_const(g_duk, NULL, "KEY_ALT", ALLEGRO_KEY_ALT);
	api_register_const(g_duk, NULL, "KEY_UP", ALLEGRO_KEY_UP);
	api_register_const(g_duk, NULL, "KEY_DOWN", ALLEGRO_KEY_DOWN);
	api_register_const(g_duk, NULL, "KEY_LEFT", ALLEGRO_KEY_LEFT);
	api_register_const(g_duk, NULL, "KEY_RIGHT", ALLEGRO_KEY_RIGHT);
	api_register_const(g_duk, NULL, "KEY_APOSTROPHE", ALLEGRO_KEY_QUOTE);
	api_register_const(g_duk, NULL, "KEY_BACKSLASH", ALLEGRO_KEY_BACKSLASH);
	api_register_const(g_duk, NULL, "KEY_BACKSPACE", ALLEGRO_KEY_BACKSPACE);
	api_register_const(g_duk, NULL, "KEY_CLOSEBRACE", ALLEGRO_KEY_CLOSEBRACE);
	api_register_const(g_duk, NULL, "KEY_CAPSLOCK", ALLEGRO_KEY_CAPSLOCK);
	api_register_const(g_duk, NULL, "KEY_COMMA", ALLEGRO_KEY_COMMA);
	api_register_const(g_duk, NULL, "KEY_DELETE", ALLEGRO_KEY_DELETE);
	api_register_const(g_duk, NULL, "KEY_END", ALLEGRO_KEY_END);
	api_register_const(g_duk, NULL, "KEY_ENTER", ALLEGRO_KEY_ENTER);
	api_register_const(g_duk, NULL, "KEY_EQUALS", ALLEGRO_KEY_EQUALS);
	api_register_const(g_duk, NULL, "KEY_ESCAPE", ALLEGRO_KEY_ESCAPE);
	api_register_const(g_duk, NULL, "KEY_HOME", ALLEGRO_KEY_HOME);
	api_register_const(g_duk, NULL, "KEY_INSERT", ALLEGRO_KEY_INSERT);
	api_register_const(g_duk, NULL, "KEY_MINUS", ALLEGRO_KEY_MINUS);
	api_register_const(g_duk, NULL, "KEY_NUMLOCK", ALLEGRO_KEY_NUMLOCK);
	api_register_const(g_duk, NULL, "KEY_OPENBRACE", ALLEGRO_KEY_OPENBRACE);
	api_register_const(g_duk, NULL, "KEY_PAGEDOWN", ALLEGRO_KEY_PGDN);
	api_register_const(g_duk, NULL, "KEY_PAGEUP", ALLEGRO_KEY_PGUP);
	api_register_const(g_duk, NULL, "KEY_PERIOD", ALLEGRO_KEY_FULLSTOP);
	api_register_const(g_duk, NULL, "KEY_SCROLLOCK", ALLEGRO_KEY_SCROLLLOCK);
	api_register_const(g_duk, NULL, "KEY_SCROLLLOCK", ALLEGRO_KEY_SCROLLLOCK);
	api_register_const(g_duk, NULL, "KEY_SEMICOLON", ALLEGRO_KEY_SEMICOLON);
	api_register_const(g_duk, NULL, "KEY_SPACE", ALLEGRO_KEY_SPACE);
	api_register_const(g_duk, NULL, "KEY_SLASH", ALLEGRO_KEY_SLASH);
	api_register_const(g_duk, NULL, "KEY_TAB", ALLEGRO_KEY_TAB);
	api_register_const(g_duk, NULL, "KEY_TILDE", ALLEGRO_KEY_TILDE);
	api_register_const(g_duk, NULL, "KEY_F1", ALLEGRO_KEY_F1);
	api_register_const(g_duk, NULL, "KEY_F2", ALLEGRO_KEY_F2);
	api_register_const(g_duk, NULL, "KEY_F3", ALLEGRO_KEY_F3);
	api_register_const(g_duk, NULL, "KEY_F4", ALLEGRO_KEY_F4);
	api_register_const(g_duk, NULL, "KEY_F5", ALLEGRO_KEY_F5);
	api_register_const(g_duk, NULL, "KEY_F6", ALLEGRO_KEY_F6);
	api_register_const(g_duk, NULL, "KEY_F7", ALLEGRO_KEY_F7);
	api_register_const(g_duk, NULL, "KEY_F8", ALLEGRO_KEY_F8);
	api_register_const(g_duk, NULL, "KEY_F9", ALLEGRO_KEY_F9);
	api_register_const(g_duk, NULL, "KEY_F10", ALLEGRO_KEY_F10);
	api_register_const(g_duk, NULL, "KEY_F11", ALLEGRO_KEY_F11);
	api_register_const(g_duk, NULL, "KEY_F12", ALLEGRO_KEY_F12);
	api_register_const(g_duk, NULL, "KEY_A", ALLEGRO_KEY_A);
	api_register_const(g_duk, NULL, "KEY_B", ALLEGRO_KEY_B);
	api_register_const(g_duk, NULL, "KEY_C", ALLEGRO_KEY_C);
	api_register_const(g_duk, NULL, "KEY_D", ALLEGRO_KEY_D);
	api_register_const(g_duk, NULL, "KEY_E", ALLEGRO_KEY_E);
	api_register_const(g_duk, NULL, "KEY_F", ALLEGRO_KEY_F);
	api_register_const(g_duk, NULL, "KEY_G", ALLEGRO_KEY_G);
	api_register_const(g_duk, NULL, "KEY_H", ALLEGRO_KEY_H);
	api_register_const(g_duk, NULL, "KEY_I", ALLEGRO_KEY_I);
	api_register_const(g_duk, NULL, "KEY_J", ALLEGRO_KEY_J);
	api_register_const(g_duk, NULL, "KEY_K", ALLEGRO_KEY_K);
	api_register_const(g_duk, NULL, "KEY_L", ALLEGRO_KEY_L);
	api_register_const(g_duk, NULL, "KEY_M", ALLEGRO_KEY_M);
	api_register_const(g_duk, NULL, "KEY_N", ALLEGRO_KEY_N);
	api_register_const(g_duk, NULL, "KEY_O", ALLEGRO_KEY_O);
	api_register_const(g_duk, NULL, "KEY_P", ALLEGRO_KEY_P);
	api_register_const(g_duk, NULL, "KEY_Q", ALLEGRO_KEY_Q);
	api_register_const(g_duk, NULL, "KEY_R", ALLEGRO_KEY_R);
	api_register_const(g_duk, NULL, "KEY_S", ALLEGRO_KEY_S);
	api_register_const(g_duk, NULL, "KEY_T", ALLEGRO_KEY_T);
	api_register_const(g_duk, NULL, "KEY_U", ALLEGRO_KEY_U);
	api_register_const(g_duk, NULL, "KEY_V", ALLEGRO_KEY_V);
	api_register_const(g_duk, NULL, "KEY_W", ALLEGRO_KEY_W);
	api_register_const(g_duk, NULL, "KEY_X", ALLEGRO_KEY_X);
	api_register_const(g_duk, NULL, "KEY_Y", ALLEGRO_KEY_Y);
	api_register_const(g_duk, NULL, "KEY_Z", ALLEGRO_KEY_Z);
	api_register_const(g_duk, NULL, "KEY_1", ALLEGRO_KEY_1);
	api_register_const(g_duk, NULL, "KEY_2", ALLEGRO_KEY_2);
	api_register_const(g_duk, NULL, "KEY_3", ALLEGRO_KEY_3);
	api_register_const(g_duk, NULL, "KEY_4", ALLEGRO_KEY_4);
	api_register_const(g_duk, NULL, "KEY_5", ALLEGRO_KEY_5);
	api_register_const(g_duk, NULL, "KEY_6", ALLEGRO_KEY_6);
	api_register_const(g_duk, NULL, "KEY_7", ALLEGRO_KEY_7);
	api_register_const(g_duk, NULL, "KEY_8", ALLEGRO_KEY_8);
	api_register_const(g_duk, NULL, "KEY_9", ALLEGRO_KEY_9);
	api_register_const(g_duk, NULL, "KEY_0", ALLEGRO_KEY_0);
	api_register_const(g_duk, NULL, "KEY_NUM_1", ALLEGRO_KEY_PAD_1);
	api_register_const(g_duk, NULL, "KEY_NUM_2", ALLEGRO_KEY_PAD_2);
	api_register_const(g_duk, NULL, "KEY_NUM_3", ALLEGRO_KEY_PAD_3);
	api_register_const(g_duk, NULL, "KEY_NUM_4", ALLEGRO_KEY_PAD_4);
	api_register_const(g_duk, NULL, "KEY_NUM_5", ALLEGRO_KEY_PAD_5);
	api_register_const(g_duk, NULL, "KEY_NUM_6", ALLEGRO_KEY_PAD_6);
	api_register_const(g_duk, NULL, "KEY_NUM_7", ALLEGRO_KEY_PAD_7);
	api_register_const(g_duk, NULL, "KEY_NUM_8", ALLEGRO_KEY_PAD_8);
	api_register_const(g_duk, NULL, "KEY_NUM_9", ALLEGRO_KEY_PAD_9);
	api_register_const(g_duk, NULL, "KEY_NUM_0", ALLEGRO_KEY_PAD_0);

	api_register_const(g_duk, NULL, "MOUSE_LEFT", MOUSE_BUTTON_LEFT);
	api_register_const(g_duk, NULL, "MOUSE_MIDDLE", MOUSE_BUTTON_MIDDLE);
	api_register_const(g_duk, NULL, "MOUSE_RIGHT", MOUSE_BUTTON_RIGHT);
	api_register_const(g_duk, NULL, "MOUSE_WHEEL_UP", MOUSE_WHEEL_UP);
	api_register_const(g_duk, NULL, "MOUSE_WHEEL_DOWN", MOUSE_WHEEL_DOWN);

	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_X", 0);
	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_Y", 1);
	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_Z", 2);
	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_R", 3);
	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_U", 4);
	api_register_const(g_duk, NULL, "JOYSTICK_AXIS_V", 5);

	api_register_method(g_duk, NULL, "AreKeysLeft", js_AreKeysLeft);
	api_register_method(g_duk, NULL, "IsAnyKeyPressed", js_IsAnyKeyPressed);
	api_register_method(g_duk, NULL, "IsJoystickButtonPressed", js_IsJoystickButtonPressed);
	api_register_method(g_duk, NULL, "IsKeyPressed", js_IsKeyPressed);
	api_register_method(g_duk, NULL, "IsMouseButtonPressed", js_IsMouseButtonPressed);
	api_register_method(g_duk, NULL, "GetJoystickAxis", js_GetJoystickAxis);
	api_register_method(g_duk, NULL, "GetKey", js_GetKey);
	api_register_method(g_duk, NULL, "GetKeyString", js_GetKeyString);
	api_register_method(g_duk, NULL, "GetMouseWheelEvent", js_GetMouseWheelEvent);
	api_register_method(g_duk, NULL, "GetMouseX", js_GetMouseX);
	api_register_method(g_duk, NULL, "GetMouseY", js_GetMouseY);
	api_register_method(g_duk, NULL, "GetNumJoysticks", js_GetNumJoysticks);
	api_register_method(g_duk, NULL, "GetNumJoystickAxes", js_GetNumJoystickAxes);
	api_register_method(g_duk, NULL, "GetNumJoystickButtons", js_GetNumJoystickButtons);
	api_register_method(g_duk, NULL, "GetNumMouseWheelEvents", js_GetNumMouseWheelEvents);
	api_register_method(g_duk, NULL, "GetPlayerKey", js_GetPlayerKey);
	api_register_method(g_duk, NULL, "GetToggleState", js_GetToggleState);
	api_register_method(g_duk, NULL, "SetMousePosition", js_SetMousePosition);
	api_register_method(g_duk, NULL, "SetPlayerKey", js_SetPlayerKey);
	api_register_method(g_duk, NULL, "BindJoystickButton", js_BindJoystickButton);
	api_register_method(g_duk, NULL, "BindKey", js_BindKey);
	api_register_method(g_duk, NULL, "ClearKeyQueue", js_ClearKeyQueue);
	api_register_method(g_duk, NULL, "UnbindJoystickButton", js_UnbindJoystickButton);
	api_register_method(g_duk, NULL, "UnbindKey", js_UnbindKey);
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

static void
queue_wheel_event(int event)
{
	if (s_num_wheel_events < 255) {
		s_wheel_queue[s_num_wheel_events] = event;
		++s_num_wheel_events;
	}
}

static duk_ret_t
js_AreKeysLeft(duk_context* ctx)
{
	update_input();
	duk_push_boolean(ctx, s_key_queue.num_keys > 0);
	return 1;
}

static duk_ret_t
js_IsAnyKeyPressed(duk_context* ctx)
{
	duk_push_boolean(ctx, is_any_key_down());
	return 1;
}

static duk_ret_t
js_IsJoystickButtonPressed(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);
	
	duk_push_boolean(ctx, is_joy_button_down(joy_index, button));
	return 1;
}

static duk_ret_t
js_IsKeyPressed(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);

	duk_push_boolean(ctx, is_key_down(keycode));
	return 1;
}

static duk_ret_t
js_IsMouseButtonPressed(duk_context* ctx)
{
	int                 button;
	int                 button_id;
	ALLEGRO_DISPLAY*    display;
	ALLEGRO_MOUSE_STATE mouse_state;

	button = duk_require_int(ctx, 0);
	button_id = button == MOUSE_BUTTON_RIGHT ? 2
		: button == MOUSE_BUTTON_MIDDLE ? 3
		: 1;
	al_get_mouse_state(&mouse_state);
	display = screen_display(g_screen);
	duk_push_boolean(ctx, mouse_state.display == display && al_mouse_button_down(&mouse_state, button_id));
	return 1;
}

static duk_ret_t
js_GetJoystickAxis(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int axis_index = duk_require_int(ctx, 1);

	duk_push_number(ctx, get_joy_axis(joy_index, axis_index));
	return 1;
}

static duk_ret_t
js_GetKey(duk_context* ctx)
{
	int keycode;

	while (s_key_queue.num_keys == 0) {
		do_events();
	}
	keycode = s_key_queue.keys[0];
	--s_key_queue.num_keys;
	memmove(s_key_queue.keys, &s_key_queue.keys[1], sizeof(int) * s_key_queue.num_keys);
	duk_push_int(ctx, keycode);
	return 1;
}

static duk_ret_t
js_GetKeyString(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	int keycode = duk_require_int(ctx, 0);
	bool shift = n_args >= 2 ? duk_require_boolean(ctx, 1) : false;

	switch (keycode) {
	case ALLEGRO_KEY_A: duk_push_string(ctx, shift ? "A" : "a"); break;
	case ALLEGRO_KEY_B: duk_push_string(ctx, shift ? "B" : "b"); break;
	case ALLEGRO_KEY_C: duk_push_string(ctx, shift ? "C" : "c"); break;
	case ALLEGRO_KEY_D: duk_push_string(ctx, shift ? "D" : "d"); break;
	case ALLEGRO_KEY_E: duk_push_string(ctx, shift ? "E" : "e"); break;
	case ALLEGRO_KEY_F: duk_push_string(ctx, shift ? "F" : "f"); break;
	case ALLEGRO_KEY_G: duk_push_string(ctx, shift ? "G" : "g"); break;
	case ALLEGRO_KEY_H: duk_push_string(ctx, shift ? "H" : "h"); break;
	case ALLEGRO_KEY_I: duk_push_string(ctx, shift ? "I" : "i"); break;
	case ALLEGRO_KEY_J: duk_push_string(ctx, shift ? "J" : "j"); break;
	case ALLEGRO_KEY_K: duk_push_string(ctx, shift ? "K" : "k"); break;
	case ALLEGRO_KEY_L: duk_push_string(ctx, shift ? "L" : "l"); break;
	case ALLEGRO_KEY_M: duk_push_string(ctx, shift ? "M" : "m"); break;
	case ALLEGRO_KEY_N: duk_push_string(ctx, shift ? "N" : "n"); break;
	case ALLEGRO_KEY_O: duk_push_string(ctx, shift ? "O" : "o"); break;
	case ALLEGRO_KEY_P: duk_push_string(ctx, shift ? "P" : "p"); break;
	case ALLEGRO_KEY_Q: duk_push_string(ctx, shift ? "Q" : "q"); break;
	case ALLEGRO_KEY_R: duk_push_string(ctx, shift ? "R" : "r"); break;
	case ALLEGRO_KEY_S: duk_push_string(ctx, shift ? "S" : "s"); break;
	case ALLEGRO_KEY_T: duk_push_string(ctx, shift ? "T" : "t"); break;
	case ALLEGRO_KEY_U: duk_push_string(ctx, shift ? "U" : "u"); break;
	case ALLEGRO_KEY_V: duk_push_string(ctx, shift ? "V" : "v"); break;
	case ALLEGRO_KEY_W: duk_push_string(ctx, shift ? "W" : "w"); break;
	case ALLEGRO_KEY_X: duk_push_string(ctx, shift ? "X" : "x"); break;
	case ALLEGRO_KEY_Y: duk_push_string(ctx, shift ? "Y" : "y"); break;
	case ALLEGRO_KEY_Z: duk_push_string(ctx, shift ? "Z" : "z"); break;
	case ALLEGRO_KEY_1: duk_push_string(ctx, shift ? "!" : "1"); break;
	case ALLEGRO_KEY_2: duk_push_string(ctx, shift ? "@" : "2"); break;
	case ALLEGRO_KEY_3: duk_push_string(ctx, shift ? "#" : "3"); break;
	case ALLEGRO_KEY_4: duk_push_string(ctx, shift ? "$" : "4"); break;
	case ALLEGRO_KEY_5: duk_push_string(ctx, shift ? "%" : "5"); break;
	case ALLEGRO_KEY_6: duk_push_string(ctx, shift ? "^" : "6"); break;
	case ALLEGRO_KEY_7: duk_push_string(ctx, shift ? "&" : "7"); break;
	case ALLEGRO_KEY_8: duk_push_string(ctx, shift ? "*" : "8"); break;
	case ALLEGRO_KEY_9: duk_push_string(ctx, shift ? "(" : "9"); break;
	case ALLEGRO_KEY_0: duk_push_string(ctx, shift ? ")" : "0"); break;
	case ALLEGRO_KEY_BACKSLASH: duk_push_string(ctx, shift ? "|" : "\\"); break;
	case ALLEGRO_KEY_FULLSTOP: duk_push_string(ctx, shift ? ">" : "."); break;
	case ALLEGRO_KEY_CLOSEBRACE: duk_push_string(ctx, shift ? "}" : "]"); break;
	case ALLEGRO_KEY_COMMA: duk_push_string(ctx, shift ? "<" : ","); break;
	case ALLEGRO_KEY_EQUALS: duk_push_string(ctx, shift ? "+" : "="); break;
	case ALLEGRO_KEY_MINUS: duk_push_string(ctx, shift ? "_" : "-"); break;
	case ALLEGRO_KEY_QUOTE: duk_push_string(ctx, shift ? "\"" : "'"); break;
	case ALLEGRO_KEY_OPENBRACE: duk_push_string(ctx, shift ? "{" : "["); break;
	case ALLEGRO_KEY_SEMICOLON: duk_push_string(ctx, shift ? ":" : ";"); break;
	case ALLEGRO_KEY_SLASH: duk_push_string(ctx, shift ? "?" : "/"); break;
	case ALLEGRO_KEY_SPACE: duk_push_string(ctx, " "); break;
	case ALLEGRO_KEY_TAB: duk_push_string(ctx, "\t"); break;
	case ALLEGRO_KEY_TILDE: duk_push_string(ctx, shift ? "~" : "`"); break;
	default:
		duk_push_string(ctx, "");
	}
	return 1;
}

static duk_ret_t
js_GetMouseWheelEvent(duk_context* ctx)
{
	int event;

	int i;
	
	while (s_num_wheel_events == 0) {
		do_events();
	}
	event = s_wheel_queue[0];
	--s_num_wheel_events;
	for (i = 0; i < s_num_wheel_events; ++i) s_wheel_queue[i] = s_wheel_queue[i + 1];
	duk_push_int(ctx, event);
	return 1;
}

static duk_ret_t
js_GetMouseX(duk_context* ctx)
{
	int x;
	int y;
	
	screen_get_mouse_xy(g_screen, &x, &y);
	duk_push_int(ctx, x);
	return 1;
}

static duk_ret_t
js_GetMouseY(duk_context* ctx)
{
	int x;
	int y;

	screen_get_mouse_xy(g_screen, &x, &y);
	duk_push_int(ctx, y);
	return 1;
}

static duk_ret_t
js_GetNumJoysticks(duk_context* ctx)
{
	duk_push_int(ctx, s_num_joysticks);
	return 1;
}

static duk_ret_t
js_GetNumJoystickAxes(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	
	duk_push_int(ctx, get_joy_axis_count(joy_index));
	return 1;
}

static duk_ret_t
js_GetNumJoystickButtons(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);

	duk_push_int(ctx, get_joy_button_count(joy_index));
	return 1;
}

static duk_ret_t
js_GetNumMouseWheelEvents(duk_context* ctx)
{
	duk_push_int(ctx, s_num_wheel_events);
	return 1;
}

static duk_ret_t
js_GetPlayerKey(duk_context* ctx)
{
	int player = duk_require_int(ctx, 0);
	int key_type = duk_require_int(ctx, 1);

	if (player < 0 || player >= 4)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "GetPlayerKey(): player index out of range");
	if (key_type < 0 || key_type >= PLAYER_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "GetPlayerKey(): invalid key type constant");
	duk_push_int(ctx, get_player_key(player, key_type));
	return 1;
}

static duk_ret_t
js_GetToggleState(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);

	int flag;
	
	if (keycode != ALLEGRO_KEY_CAPSLOCK
		&& keycode != ALLEGRO_KEY_NUMLOCK
		&& keycode != ALLEGRO_KEY_SCROLLLOCK)
	{
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "GetToggleState(): invalid toggle key constant");
	}
	flag = keycode == ALLEGRO_KEY_CAPSLOCK ? ALLEGRO_KEYMOD_CAPSLOCK
		: keycode == ALLEGRO_KEY_NUMLOCK ? ALLEGRO_KEYMOD_NUMLOCK
		: keycode == ALLEGRO_KEY_SCROLLLOCK ? ALLEGRO_KEYMOD_SCROLLLOCK
		: 0x0;
	duk_push_boolean(ctx, (s_keymod_state & flag) != 0);
	return 1;
}

static duk_ret_t
js_SetMousePosition(duk_context* ctx)
{
	int x;
	int y;
	
	x = duk_require_int(ctx, 0);
	y = duk_require_int(ctx, 1);
	screen_set_mouse_xy(g_screen, x, y);
	return 0;
}

static duk_ret_t
js_SetPlayerKey(duk_context* ctx)
{
	int player = duk_require_int(ctx, 0);
	int key_type = duk_require_int(ctx, 1);
	int keycode = duk_require_int(ctx, 2);

	if (player < 0 || player >= 4)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "SetPlayerKey(): player index `%d` out of range", player);
	if (key_type < 0 || key_type >= PLAYER_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "SetPlayerKey(): invalid key type constant");
	if (keycode < 0 || key_type >= ALLEGRO_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "SetPlayerKey(): invalid key constant");
	set_player_key(player, key_type, keycode);
	return 0;
}

static duk_ret_t
js_BindJoystickButton(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);
	script_t* on_down_script = duk_require_sphere_script(ctx, 2, "[button-down script]");
	script_t* on_up_script = duk_require_sphere_script(ctx, 3, "[button-up script]");

	if (joy_index < 0 || joy_index >= MAX_JOYSTICKS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): joystick index `%d` out of range", joy_index);
	if (button < 0 || button >= MAX_JOY_BUTTONS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): button index `%d` out of range", button);
	bind_button(s_bound_buttons, joy_index, button, on_down_script, on_up_script);
	return 0;
}

static duk_ret_t
js_BindKey(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);
	script_t* on_down_script = duk_require_sphere_script(ctx, 1, "[key-down script]");
	script_t* on_up_script = duk_require_sphere_script(ctx, 2, "[key-up script]");

	if (keycode < 0 || keycode >= ALLEGRO_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindKey(): invalid key constant");
	bind_key(s_bound_map_keys, keycode, on_down_script, on_up_script);
	return 0;
}

static duk_ret_t
js_ClearKeyQueue(duk_context* ctx)
{
	clear_key_queue();
	return 0;
}

static duk_ret_t
js_UnbindJoystickButton(duk_context* ctx)
{
	int joy_index = duk_require_int(ctx, 0);
	int button = duk_require_int(ctx, 1);

	if (joy_index < 0 || joy_index >= MAX_JOYSTICKS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): joystick index `%d` out of range", joy_index);
	if (button < 0 || button >= MAX_JOY_BUTTONS)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "BindJoystickButton(): button index `%d` out of range", button);
	bind_button(s_bound_buttons, joy_index, button, NULL, NULL);
	return 0;
}

static duk_ret_t
js_UnbindKey(duk_context* ctx)
{
	int keycode = duk_require_int(ctx, 0);

	if (keycode < 0 || keycode >= ALLEGRO_KEY_MAX)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "UnbindKey(): invalid key constant");
	bind_key(s_bound_map_keys, keycode, NULL, NULL);
	return 0;
}
