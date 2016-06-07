#include "minisphere.h"
#include "surface.h"

#include "api.h"
#include "color.h"
#include "image.h"

static duk_ret_t js_screen_get_frameRate (duk_context* ctx);
static duk_ret_t js_screen_set_frameRate (duk_context* ctx);
static duk_ret_t js_screen_flip          (duk_context* ctx);
static duk_ret_t js_screen_resize        (duk_context* ctx);
static duk_ret_t js_new_Surface          (duk_context* ctx);
static duk_ret_t js_Surface_finalize     (duk_context* ctx);
static duk_ret_t js_Surface_get_height   (duk_context* ctx);
static duk_ret_t js_Surface_get_width    (duk_context* ctx);
static duk_ret_t js_Surface_toImage      (duk_context* ctx);

void
init_surface_api(void)
{
	api_register_ctor(g_duk, "Surface", js_new_Surface, js_Surface_finalize);
	api_register_prop(g_duk, "Surface", "height", js_Surface_get_height, NULL);
	api_register_prop(g_duk, "Surface", "width", js_Surface_get_width, NULL);
	api_register_method(g_duk, "Surface", "toImage", js_Surface_toImage);

	duk_push_global_object(g_duk);
	duk_push_string(g_duk, "screen");
	duk_push_sphere_obj(g_duk, "Surface", NULL);
	duk_def_prop(g_duk, -3, DUK_DEFPROP_HAVE_VALUE
		| DUK_DEFPROP_CLEAR_ENUMERABLE
		| DUK_DEFPROP_CLEAR_WRITABLE
		| DUK_DEFPROP_SET_CONFIGURABLE);
	api_register_static_prop(g_duk, "screen", "frameRate", js_screen_get_frameRate, js_screen_set_frameRate);
	api_register_static_func(g_duk, "screen", "flip", js_screen_flip);
	api_register_static_func(g_duk, "screen", "resize", js_screen_resize);
}

static duk_ret_t
js_screen_get_frameRate(duk_context* ctx)
{
	duk_push_int(ctx, g_framerate);
	return 1;
}

static duk_ret_t
js_screen_set_frameRate(duk_context* ctx)
{
	int framerate;

	framerate = duk_require_int(ctx, 0);

	if (framerate < 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "frameRate cannot be negative");
	g_framerate = framerate;
	return 0;
}

static duk_ret_t
js_screen_flip(duk_context* ctx)
{
	screen_flip(g_screen, g_framerate);
	return 0;
}

static duk_ret_t
js_screen_resize(duk_context* ctx)
{
	int  res_width;
	int  res_height;

	res_width = duk_require_int(ctx, 0);
	res_height = duk_require_int(ctx, 1);

	if (res_width < 0 || res_height < 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "illegal screen resolution");
	screen_resize(g_screen, res_width, res_height);
	return 0;
}

static duk_ret_t
js_new_Surface(duk_context* ctx)
{
	int         n_args;
	const char* filename;
	color_t     fill_color;
	image_t*    image;
	image_t*    src_image;
	int         width, height;

	n_args = duk_get_top(ctx);
	if (n_args >= 2) {
		width = duk_require_int(ctx, 0);
		height = duk_require_int(ctx, 1);
		fill_color = n_args >= 3 ? duk_require_sphere_color(ctx, 2) : color_new(0, 0, 0, 0);
		if (!(image = create_image(width, height)))
			duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to create surface");
		fill_image(image, fill_color);
	}
	else if (duk_is_sphere_obj(ctx, 0, "Image")) {
		src_image = duk_require_sphere_image(ctx, 0);
		if (!(image = clone_image(src_image)))
			duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to create surface from image");
	}
	else {
		filename = duk_require_path(ctx, 0, NULL, false);
		image = load_image(filename);
		if (image == NULL)
			duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to load image `%s`", filename);
	}
	duk_push_sphere_obj(ctx, "Surface", image);
	return 1;
}

static duk_ret_t
js_Surface_finalize(duk_context* ctx)
{
	image_t* image;
	
	image = duk_require_sphere_obj(ctx, 0, "Surface");
	free_image(image);
	return 0;
}

static duk_ret_t
js_Surface_get_height(duk_context* ctx)
{
	image_t* image;

	duk_push_this(ctx);
	image = duk_require_sphere_obj(ctx, -1, "Surface");

	if (image != NULL)
		duk_push_int(ctx, get_image_height(image));
	else
		duk_push_int(ctx, g_res_y);
	return 1;
}

static duk_ret_t
js_Surface_get_width(duk_context* ctx)
{
	image_t* image;

	duk_push_this(ctx);
	image = duk_require_sphere_obj(ctx, -1, "Surface");

	if (image != NULL)
		duk_push_int(ctx, get_image_width(image));
	else
		duk_push_int(ctx, g_res_x);
	return 1;
}

static duk_ret_t
js_Surface_toImage(duk_context* ctx)
{
	image_t* image;
	image_t* new_image;

	duk_push_this(ctx);
	image = duk_require_sphere_obj(ctx, -1, "Surface");

	if ((new_image = clone_image(image)) == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to create image");
	duk_push_sphere_image(ctx, new_image);
	free_image(new_image);
	return 1;
}
