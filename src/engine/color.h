#ifndef MINISPHERE__COLOR_H__INCLUDED
#define MINISPHERE__COLOR_H__INCLUDED

typedef
struct color
{
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t alpha;
} color_t;

ALLEGRO_COLOR nativecolor (color_t color);
color_t       color_mix   (color_t color, color_t other, float w1, float w2);
color_t       color_new   (uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);

void init_color_api (void);

void          duk_push_sphere_color    (duk_context* ctx, color_t color);
color_t       duk_require_sphere_color (duk_context* ctx, duk_idx_t index);

#endif // MINISPHERE__COLOR_H__INCLUDED
