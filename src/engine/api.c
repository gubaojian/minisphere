#include "minisphere.h"
#include "api.h"

#include "async.h"
#include "audio.h"
#include "color.h"
#include "commonjs.h"
#include "console.h"
#include "debugger.h"
#include "file.h"
#include "font.h"
#include "galileo.h"
#include "image.h"
#include "input.h"
#include "map_engine.h"
#include "rng.h"
#include "shader.h"
#include "sockets.h"
#include "spriteset.h"
#include "surface.h"
#include "windowstyle.h"

#define SPHERE_API_VERSION 2
#define SPHERE_API_LEVEL   1

static const char* const SPHERE_EXTENSIONS[] =
{
	"sphere_fs_system_alias",
};

static duk_ret_t js_engine_get_apiLevel   (duk_context* ctx);
static duk_ret_t js_engine_get_apiVersion (duk_context* ctx);
static duk_ret_t js_engine_get_extensions (duk_context* ctx);
static duk_ret_t js_engine_get_game       (duk_context* ctx);
static duk_ret_t js_engine_get_name       (duk_context* ctx);
static duk_ret_t js_engine_get_time       (duk_context* ctx);
static duk_ret_t js_engine_get_version    (duk_context* ctx);
static duk_ret_t js_engine_dispatch       (duk_context* ctx);
static duk_ret_t js_engine_doEvents       (duk_context* ctx);
static duk_ret_t js_engine_exit           (duk_context* ctx);
static duk_ret_t js_engine_restart        (duk_context* ctx);
static duk_ret_t js_engine_sleep          (duk_context* ctx);
static duk_ret_t js_RequireSystemScript   (duk_context* ctx);
static duk_ret_t js_RequireScript         (duk_context* ctx);
static duk_ret_t js_EvaluateSystemScript  (duk_context* ctx);
static duk_ret_t js_EvaluateScript        (duk_context* ctx);
static duk_ret_t js_abort                 (duk_context* ctx);
static duk_ret_t js_alert                 (duk_context* ctx);
static duk_ret_t js_assert                (duk_context* ctx);

static vector_t*  s_extensions;

void
initialize_api(duk_context* ctx)
{
	int num_extensions;

	int i;

	console_log(1, "initializing Spherical API %d.%d", SPHERE_API_VERSION, SPHERE_API_LEVEL - 1);

	// register API extensions
	s_extensions = vector_new(sizeof(char*));
	num_extensions = sizeof SPHERE_EXTENSIONS / sizeof SPHERE_EXTENSIONS[0];
	for (i = 0; i < num_extensions; ++i) {
		console_log(1, "    %s", SPHERE_EXTENSIONS[i]);
		api_register_extension(SPHERE_EXTENSIONS[i]);
	}

	// register the 'global' global object alias (like Node.js!).
	// this provides direct access to the global object from any scope.
	duk_push_global_object(ctx);
	duk_push_string(ctx, "global");
	duk_push_global_object(ctx);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);

	// set up RequireScript() inclusion tracking table
	duk_push_global_stash(ctx);
	duk_push_object(ctx);
	duk_put_prop_string(ctx, -2, "RequireScript");
	duk_pop(ctx);

	// stash an object to hold prototypes for built-in objects
	duk_push_global_stash(ctx);
	duk_push_object(ctx);
	duk_put_prop_string(ctx, -2, "prototypes");
	duk_pop(ctx);

	api_register_static_prop(ctx, "engine", "apiLevel", js_engine_get_apiLevel, NULL);
	api_register_static_prop(ctx, "engine", "apiVersion", js_engine_get_apiVersion, NULL);
	api_register_static_prop(ctx, "engine", "extensions", js_engine_get_extensions, NULL);
	api_register_static_prop(ctx, "engine", "game", js_engine_get_game, NULL);
	api_register_static_prop(ctx, "engine", "name", js_engine_get_name, NULL);
	api_register_static_prop(ctx, "engine", "time", js_engine_get_time, NULL);
	api_register_static_prop(ctx, "engine", "version", js_engine_get_version, NULL);
	api_register_static_func(ctx, "engine", "dispatch", js_engine_dispatch);
	api_register_static_func(ctx, "engine", "doEvents", js_engine_doEvents);
	api_register_static_func(ctx, "engine", "exit", js_engine_exit);
	api_register_static_func(ctx, "engine", "restart", js_engine_restart);
	api_register_static_func(ctx, "engine", "sleep", js_engine_sleep);
	api_register_static_func(ctx, NULL, "abort", js_abort);
	api_register_static_func(ctx, NULL, "alert", js_alert);
	api_register_static_func(ctx, NULL, "assert", js_assert);

	api_register_method(ctx, NULL, "EvaluateScript", js_EvaluateScript);
	api_register_method(ctx, NULL, "EvaluateSystemScript", js_EvaluateSystemScript);
	api_register_method(ctx, NULL, "RequireScript", js_RequireScript);
	api_register_method(ctx, NULL, "RequireSystemScript", js_RequireSystemScript);

	// initialize subsystem APIs
	init_audio_api();
	init_color_api();
	init_commonjs_api();
	init_console_api();
	init_file_api();
	init_font_api(g_duk);
	init_galileo_api();
	init_image_api(g_duk);
	init_input_api();
	init_map_engine_api(g_duk);
	init_rng_api();
	init_screen_api();
	init_shader_api();
	init_sockets_api();
	init_spriteset_api(g_duk);
	init_surface_api();
	init_windowstyle_api();
}

void
shutdown_api(void)
{
	console_log(1, "shutting down Spherical API");
}

bool
api_have_extension(const char* name)
{
	iter_t iter;
	char* *i_name;

	iter = vector_enum(s_extensions);
	while (i_name = vector_next(&iter))
		if (strcmp(*i_name, name) == 0) return true;
	return false;
}

double
api_version(void)
{
	return SPHERE_API_VERSION;
}

void
api_register_const(duk_context* ctx, const char* name, double value)
{
	duk_push_global_object(ctx);
	duk_push_string(ctx, name); duk_push_number(ctx, value);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	duk_pop(ctx);
}

void
api_register_ctor(duk_context* ctx, const char* name, duk_c_function fn, duk_c_function finalizer)
{
	duk_push_global_object(ctx);
	duk_push_c_function(ctx, fn, DUK_VARARGS);
	duk_push_string(ctx, "name");
	duk_push_string(ctx, name);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);

	// create a prototype. Duktape won't assign one for us.
	duk_push_object(ctx);
	duk_push_string(ctx, name);
	duk_put_prop_string(ctx, -2, "\xFF" "ctor");
	if (finalizer != NULL) {
		duk_push_c_function(ctx, finalizer, DUK_VARARGS);
		duk_put_prop_string(ctx, -2, "\xFF" "dtor");
	}

	// save the prototype in the prototype stash. for full compatibility with
	// Sphere 1.5, we have to allow native objects to be created through the
	// legacy APIs even if the corresponding constructor is overwritten or shadowed.
	// for that to work, the prototype must remain accessible.
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "prototypes");
	duk_dup(ctx, -3);
	duk_put_prop_string(ctx, -2, name);
	duk_pop_2(ctx);

	// attach prototype to constructor
	duk_push_string(ctx, "prototype");
	duk_insert(ctx, -2);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_SET_WRITABLE);
	duk_push_string(ctx, name);
	duk_insert(ctx, -2);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_SET_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	duk_pop(ctx);
}

bool
api_register_extension(const char* designation)
{
	char* string;

	if (!(string = strdup(designation))) return false;
	if (!vector_push(s_extensions, &string))
		return false;
	return true;
}

void
api_register_method(duk_context* ctx, const char* ctor_name, const char* name, duk_c_function fn)
{
	duk_push_global_object(ctx);
	if (ctor_name != NULL) {
		// load the prototype from the prototype stash
		duk_push_global_stash(ctx);
		duk_get_prop_string(ctx, -1, "prototypes");
		duk_get_prop_string(ctx, -1, ctor_name);
	}

	duk_push_c_function(ctx, fn, DUK_VARARGS);
	duk_push_string(ctx, "name");
	duk_push_string(ctx, name);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);

	// for a defprop, Duktape expects the key to be pushed first, then the value; however, since
	// we have the value (the function being registered) on the stack already by this point, we
	// need to shuffle things around to make everything work.
	duk_push_string(ctx, name);
	duk_insert(ctx, -2);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_SET_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);

	if (ctor_name != NULL)
		duk_pop_3(ctx);
	duk_pop(ctx);
}

void
api_register_prop(duk_context* ctx, const char* ctor_name, const char* name, duk_c_function getter, duk_c_function setter)
{
	duk_uint_t flags;
	int        obj_index;

	duk_push_global_object(ctx);
	if (ctor_name != NULL) {
		duk_push_global_stash(ctx);
		duk_get_prop_string(ctx, -1, "prototypes");
		duk_get_prop_string(ctx, -1, ctor_name);
	}
	obj_index = duk_normalize_index(ctx, -1);
	duk_push_string(ctx, name);
	flags = DUK_DEFPROP_SET_CONFIGURABLE;
	if (getter != NULL) {
		duk_push_c_function(ctx, getter, DUK_VARARGS);
		flags |= DUK_DEFPROP_HAVE_GETTER;
	}
	if (setter != NULL) {
		duk_push_c_function(ctx, setter, DUK_VARARGS);
		flags |= DUK_DEFPROP_HAVE_SETTER;
	}
	duk_def_prop(g_duk, obj_index, flags);
	if (ctor_name != NULL)
		duk_pop_3(ctx);
	duk_pop(ctx);
}

void
api_register_static_func(duk_context* ctx, const char* namespace_name, const char* name, duk_c_function fn)
{
	duk_push_global_object(ctx);

	// ensure the namespace object exists
	if (namespace_name != NULL) {
		if (!duk_get_prop_string(ctx, -1, namespace_name)) {
			duk_pop(ctx);
			duk_push_string(ctx, namespace_name);
			duk_push_object(ctx);
			duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
				| DUK_DEFPROP_SET_WRITABLE
				| DUK_DEFPROP_SET_CONFIGURABLE);
			duk_get_prop_string(ctx, -1, namespace_name);
		}
	}

	duk_push_string(ctx, name);
	duk_push_c_function(ctx, fn, DUK_VARARGS);
	duk_push_string(ctx, "name");
	duk_push_string(ctx, name);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_SET_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	if (namespace_name != NULL)
		duk_pop(ctx);
	duk_pop(ctx);
}

void
api_register_static_prop(duk_context* ctx, const char* namespace_name, const char* name, duk_c_function getter, duk_c_function setter)
{
	int       flags;
	duk_idx_t obj_index;

	duk_push_global_object(ctx);

	// ensure the namespace object exists
	if (namespace_name != NULL) {
		if (!duk_get_prop_string(ctx, -1, namespace_name)) {
			duk_pop(ctx);
			duk_push_string(ctx, namespace_name);
			duk_push_object(ctx);
			duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
				| DUK_DEFPROP_SET_WRITABLE
				| DUK_DEFPROP_SET_CONFIGURABLE);
			duk_get_prop_string(ctx, -1, namespace_name);
		}
	}

	obj_index = duk_normalize_index(ctx, -1);
	duk_push_string(ctx, name);
	flags = DUK_DEFPROP_SET_CONFIGURABLE;
	if (getter != NULL) {
		duk_push_c_function(ctx, getter, DUK_VARARGS);
		flags |= DUK_DEFPROP_HAVE_GETTER;
	}
	if (setter != NULL) {
		duk_push_c_function(ctx, setter, DUK_VARARGS);
		flags |= DUK_DEFPROP_HAVE_SETTER;
	}
	duk_def_prop(g_duk, obj_index, flags);
	if (namespace_name != NULL)
		duk_pop(ctx);
	duk_pop(ctx);
}

void
api_register_type(duk_context* ctx, const char* name, duk_c_function finalizer)
{
	// construct a prototype for our new type
	duk_push_object(ctx);
	duk_push_string(ctx, name);
	duk_put_prop_string(ctx, -2, "\xFF" "ctor");
	if (finalizer != NULL) {
		duk_push_c_function(ctx, finalizer, DUK_VARARGS);
		duk_put_prop_string(ctx, -2, "\xFF" "dtor");
	}

	// stash the new prototype
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "prototypes");
	duk_dup(ctx, -3);
	duk_put_prop_string(ctx, -2, name);
	duk_pop_3(ctx);
}

noreturn
duk_error_ni(duk_context* ctx, int blame_offset, duk_errcode_t err_code, const char* fmt, ...)
{
	va_list ap;
	char*   filename;
	int     line_number;

	// get filename and line number from Duktape call stack
	duk_get_global_string(g_duk, "Duktape");
	duk_get_prop_string(g_duk, -1, "act");
	duk_push_int(g_duk, -2 + blame_offset);
	duk_call(g_duk, 1);
	if (!duk_is_object(g_duk, -1)) {
		duk_pop(g_duk);
		duk_get_prop_string(g_duk, -1, "act");
		duk_push_int(g_duk, -2);
		duk_call(g_duk, 1);
	}
	duk_get_prop_string(g_duk, -1, "lineNumber");
	duk_get_prop_string(g_duk, -2, "function");
	duk_get_prop_string(g_duk, -1, "fileName");
	filename = strdup(duk_safe_to_string(g_duk, -1));
	line_number = duk_to_int(g_duk, -3);
	duk_pop_n(g_duk, 5);

	// construct an Error object
	va_start(ap, fmt);
	duk_push_error_object_va(ctx, err_code, fmt, ap);
	va_end(ap);
	duk_push_string(ctx, filename);
	duk_put_prop_string(ctx, -2, "fileName");
	duk_push_int(ctx, line_number);
	duk_put_prop_string(ctx, -2, "lineNumber");
	free(filename);
	
	duk_throw(ctx);
}

duk_bool_t
duk_is_sphere_obj(duk_context* ctx, duk_idx_t index, const char* ctor_name)
{
	const char* obj_ctor_name;
	duk_bool_t  result;

	index = duk_require_normalize_index(ctx, index);
	if (!duk_is_object_coercible(ctx, index))
		return 0;

	duk_get_prop_string(ctx, index, "\xFF" "ctor");
	obj_ctor_name = duk_safe_to_string(ctx, -1);
	result = strcmp(obj_ctor_name, ctor_name) == 0;
	duk_pop(ctx);
	return result;
}

void
duk_push_sphere_obj(duk_context* ctx, const char* ctor_name, void* udata)
{
	duk_idx_t obj_index;

	duk_push_object(ctx);
	obj_index = duk_normalize_index(ctx, -1);
	duk_push_pointer(ctx, udata);
	duk_put_prop_string(ctx, -2, "\xFF" "udata");
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "prototypes");
	duk_get_prop_string(ctx, -1, ctor_name);
	if (duk_get_prop_string(ctx, -1, "\xFF" "dtor")) {
		duk_set_finalizer(ctx, obj_index);
	}
	else
		duk_pop(ctx);
	duk_set_prototype(ctx, obj_index);
	duk_pop_2(ctx);
}

void*
duk_require_sphere_obj(duk_context* ctx, duk_idx_t index, const char* ctor_name)
{
	void* udata;

	index = duk_require_normalize_index(ctx, index);
	if (!duk_is_sphere_obj(ctx, index, ctor_name))
		duk_error(ctx, DUK_ERR_TYPE_ERROR, "expected a %s object", ctor_name);
	duk_get_prop_string(ctx, index, "\xFF" "udata");
	udata = duk_get_pointer(ctx, -1);
	duk_pop(ctx);
	return udata;
}

static duk_ret_t
js_engine_get_game(duk_context* ctx)
{
	duk_push_lstring_t(ctx, get_game_manifest(g_fs));
	duk_json_decode(ctx, -1);

	duk_push_this(ctx);
	duk_push_string(ctx, "game");
	duk_dup(ctx, -3);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_CLEAR_ENUMERABLE
		| DUK_DEFPROP_CLEAR_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	duk_pop(ctx);

	return 1;
}

static duk_ret_t
js_engine_get_apiLevel(duk_context* ctx)
{
	duk_push_int(ctx, SPHERE_API_LEVEL);
	return 1;
}

static duk_ret_t
js_engine_get_apiVersion(duk_context* ctx)
{
	duk_push_int(ctx, SPHERE_API_VERSION);
	return 1;
}

static duk_ret_t
js_engine_get_extensions(duk_context* ctx)
{
	char**  p_string;

	iter_t iter;
	int    i;

	duk_push_array(ctx);
	iter = vector_enum(s_extensions);
	i = 0;
	while (p_string = vector_next(&iter)) {
		duk_push_string(ctx, *p_string);
		duk_put_prop_index(ctx, -2, i++);
	}

	duk_push_this(ctx);
	duk_push_string(ctx, "extensions");
	duk_dup(ctx, -3);
	duk_def_prop(ctx, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_CLEAR_ENUMERABLE
		| DUK_DEFPROP_CLEAR_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	duk_pop(ctx);

	return 1;
}

static duk_ret_t
js_engine_get_name(duk_context* ctx)
{
	duk_push_string(ctx, PRODUCT_NAME);
	return 1;
}

static duk_ret_t
js_engine_get_time(duk_context* ctx)
{
	duk_push_number(ctx, al_get_time());
	return 1;
}

static duk_ret_t
js_engine_get_version(duk_context* ctx)
{
	duk_push_string(ctx, VERSION_NAME);
	return 1;
}

static duk_ret_t
js_engine_dispatch(duk_context* ctx)
{
	script_t* script;

	script = duk_require_sphere_script(ctx, 0, "synth:async.js");

	if (!queue_async_script(script))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to dispatch async script");
	return 0;
}

static duk_ret_t
js_engine_doEvents(duk_context* ctx)
{
	do_events();
	duk_push_boolean(ctx, true);
	return 1;
}

static duk_ret_t
js_engine_exit(duk_context* ctx)
{
	exit_game(false);
}

static duk_ret_t
js_engine_restart(duk_context* ctx)
{
	restart_engine();
}

static duk_ret_t
js_engine_sleep(duk_context* ctx)
{
	double timeout;

	timeout = duk_require_number(ctx, 0);

	delay(timeout);
	return 0;
}

static duk_ret_t
js_EvaluateScript(duk_context* ctx)
{
	const char* filename;

	filename = duk_require_path(ctx, 0, "scripts", true);
	if (!sfs_fexist(g_fs, filename, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "EvaluateScript(): file `%s` not found", filename);
	if (!evaluate_script(filename, false))
		duk_throw(ctx);
	return 1;
}

static duk_ret_t
js_EvaluateSystemScript(duk_context* ctx)
{
	const char* filename = duk_require_string(ctx, 0);

	char path[SPHERE_PATH_MAX];

	sprintf(path, "scripts/lib/%s", filename);
	if (!sfs_fexist(g_fs, path, NULL))
		sprintf(path, "#/scripts/%s", filename);
	if (!sfs_fexist(g_fs, path, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "EvaluateSystemScript(): system script `%s` not found", filename);
	if (!evaluate_script(path, false))
		duk_throw(ctx);
	return 1;
}

static duk_ret_t
js_RequireScript(duk_context* ctx)
{
	const char* filename;
	bool        is_required;

	filename = duk_require_path(ctx, 0, "scripts", true);
	if (!sfs_fexist(g_fs, filename, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "RequireScript(): file `%s` not found", filename);
	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "RequireScript");
	duk_get_prop_string(ctx, -1, filename);
	is_required = duk_get_boolean(ctx, -1);
	duk_pop(ctx);
	if (!is_required) {
		duk_push_true(ctx);
		duk_put_prop_string(ctx, -2, filename);
		if (!evaluate_script(filename, false))
			duk_throw(ctx);
	}
	duk_pop_3(ctx);
	return 0;
}

static duk_ret_t
js_RequireSystemScript(duk_context* ctx)
{
	const char* filename = duk_require_string(ctx, 0);

	bool is_required;
	char path[SPHERE_PATH_MAX];

	sprintf(path, "scripts/lib/%s", filename);
	if (!sfs_fexist(g_fs, path, NULL))
		sprintf(path, "#/scripts/%s", filename);
	if (!sfs_fexist(g_fs, path, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "RequireSystemScript(): system script `%s` not found", filename);

	duk_push_global_stash(ctx);
	duk_get_prop_string(ctx, -1, "RequireScript");
	duk_get_prop_string(ctx, -1, path);
	is_required = duk_get_boolean(ctx, -1);
	duk_pop(ctx);
	if (!is_required) {
		duk_push_true(ctx);
		duk_put_prop_string(ctx, -2, path);
		if (!evaluate_script(path, false))
			duk_throw(ctx);
	}
	duk_pop_2(ctx);
	return 0;
}

static duk_ret_t
js_abort(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	const char* message = n_args >= 1 ? duk_to_string(ctx, 0) : "Some type of weird pig just ate your game!\n\n\n\n\n\n\n\n...and you*munch*";
	int stack_offset = n_args >= 2 ? duk_require_int(ctx, 1) : 0;

	if (stack_offset > 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "Abort(): stack offset must be negative");
	duk_error_ni(ctx, -1 + stack_offset, DUK_ERR_ERROR, "%s", message);
}

static duk_ret_t
js_alert(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	const char* text = n_args >= 1 && !duk_is_null_or_undefined(ctx, 0)
		? duk_to_string(ctx, 0) : "It's 8:12... do you know where the pig is?\n\nIt's...\n\n\n\n\n\n\nBEHIND YOU! *MUNCH*";
	int stack_offset = n_args >= 2 ? duk_require_int(ctx, 1) : 0;

	const char* caller_info;
	const char* filename;
	int         line_number;

	if (stack_offset > 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "Alert(): stack offset must be negative");

	// get filename and line number of Alert() call
	duk_push_global_object(ctx);
	duk_get_prop_string(ctx, -1, "Duktape");
	duk_get_prop_string(ctx, -1, "act"); duk_push_int(ctx, -3 + stack_offset); duk_call(ctx, 1);
	if (!duk_is_object(ctx, -1)) {
		duk_pop(ctx);
		duk_get_prop_string(ctx, -1, "act"); duk_push_int(ctx, -3); duk_call(ctx, 1);
	}
	duk_remove(ctx, -2);
	duk_get_prop_string(ctx, -1, "lineNumber"); line_number = duk_get_int(ctx, -1); duk_pop(ctx);
	duk_get_prop_string(ctx, -1, "function");
	duk_get_prop_string(ctx, -1, "fileName"); filename = duk_get_string(ctx, -1); duk_pop(ctx);
	duk_pop_2(ctx);

	// show the message
	screen_show_mouse(g_screen, true);
	caller_info =
		duk_push_sprintf(ctx, "%s (line %i)", filename, line_number),
		duk_get_string(ctx, -1);
	al_show_native_message_box(screen_display(g_screen), "Alert from Sphere game", caller_info, text, NULL, 0x0);
	screen_show_mouse(g_screen, false);

	return 0;
}

static duk_ret_t
js_assert(duk_context* ctx)
{
	const char* filename;
	int         line_number;
	const char* message;
	int         num_args;
	bool        result;
	int         stack_offset;
	lstring_t*  text;

	num_args = duk_get_top(ctx);
	result = duk_to_boolean(ctx, 0);
	message = duk_require_string(ctx, 1);
	stack_offset = num_args >= 3 ? duk_require_int(ctx, 2)
		: 0;

	if (stack_offset > 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "Assert(): stack offset must be negative");

	if (!result) {
		// get the offending script and line number from the call stack
		duk_push_global_object(ctx);
		duk_get_prop_string(ctx, -1, "Duktape");
		duk_get_prop_string(ctx, -1, "act"); duk_push_int(ctx, -3 + stack_offset); duk_call(ctx, 1);
		if (!duk_is_object(ctx, -1)) {
			duk_pop(ctx);
			duk_get_prop_string(ctx, -1, "act"); duk_push_int(ctx, -3); duk_call(ctx, 1);
		}
		duk_remove(ctx, -2);
		duk_get_prop_string(ctx, -1, "lineNumber"); line_number = duk_get_int(ctx, -1); duk_pop(ctx);
		duk_get_prop_string(ctx, -1, "function");
		duk_get_prop_string(ctx, -1, "fileName"); filename = duk_get_string(ctx, -1); duk_pop(ctx);
		duk_pop_2(ctx);
		fprintf(stderr, "ASSERT: `%s:%i` : %s\n", filename, line_number, message);

		// if an assertion fails in a game being debugged:
		//   - the user may choose to ignore it, in which case execution continues.  this is useful
		//     in some debugging scenarios.
		//   - if the user chooses not to continue, a prompt breakpoint will be triggered, turning
		//     over control to the attached debugger.
		if (is_debugger_attached()) {
			text = lstr_newf("%s (line: %i)\n%s\n\nYou can ignore the error, or pause execution, turning over control to the attached debugger.  If you choose to debug, execution will pause at the statement following the failed Assert().\n\nIgnore the error and continue?", filename, line_number, message);
			if (!al_show_native_message_box(screen_display(g_screen), "Script Error", "Assertion failed!",
				lstr_cstr(text), NULL, ALLEGRO_MESSAGEBOX_WARN | ALLEGRO_MESSAGEBOX_YES_NO))
			{
				duk_debugger_pause(ctx);
			}
			lstr_free(text);
		}
	}
	duk_dup(ctx, 0);
	return 1;
}
