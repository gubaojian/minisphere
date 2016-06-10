#include "minisphere.h"
#include "image.h"

#include "api.h"
#include "color.h"

struct image
{
	unsigned int    refcount;
	unsigned int    id;
	ALLEGRO_BITMAP* bitmap;
	unsigned int    cache_hits;
	image_lock_t    lock;
	unsigned int    lock_count;
	color_t*        pixel_cache;
	int             width;
	int             height;
	image_t*        parent;
};


static unsigned int s_next_image_id = 0;

image_t*
image_new(int width, int height)
{
	image_t* image;

	console_log(3, "creating image #%u at %ix%i", s_next_image_id, width, height);
	image = calloc(1, sizeof(image_t));
	if ((image->bitmap = al_create_bitmap(width, height)) == NULL)
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_new_slice(image_t* parent, int x, int y, int width, int height)
{
	image_t* image;

	console_log(3, "creating image #%u as %ix%i subimage of image #%u", s_next_image_id, width, height, parent->id);
	image = calloc(1, sizeof(image_t));
	if (!(image->bitmap = al_create_sub_bitmap(parent->bitmap, x, y, width, height)))
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	image->parent = image_ref(parent);
	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_clone(const image_t* src_image)
{
	image_t* image;

	console_log(3, "cloning image #%u from source image #%u",
		s_next_image_id, src_image->id);
	
	image = calloc(1, sizeof(image_t));
	if (!(image->bitmap = al_clone_bitmap(src_image->bitmap)))
		goto on_error;
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	
	return image_ref(image);

on_error:
	free(image);
	return NULL;
}

image_t*
image_load(const char* filename)
{
	ALLEGRO_FILE* al_file = NULL;
	const char*   file_ext;
	size_t        file_size;
	uint8_t       first_16[16];
	image_t*      image;
	void*         slurp = NULL;

	console_log(2, "loading image #%u as `%s`", s_next_image_id, filename);
	
	image = calloc(1, sizeof(image_t));
	if (!(slurp = sfs_fslurp(g_fs, filename, &file_size)))
		goto on_error;
	al_file = al_open_memfile(slurp, file_size, "rb");

	// look at the first 16 bytes of the file to determine its actual type.
	// Allegro won't load it if the content doesn't match the file extension, so
	// we have to inspect the file ourselves.
	al_fread(al_file, first_16, 16);
	al_fseek(al_file, 0, ALLEGRO_SEEK_SET);
	file_ext = strrchr(filename, '.');
	if (memcmp(first_16, "BM", 2) == 0) file_ext = ".bmp";
	if (memcmp(first_16, "\211PNG\r\n\032\n", 8) == 0) file_ext = ".png";
	if (memcmp(first_16, "\0xFF\0xD8", 2) == 0) file_ext = ".jpg";

	if (!(image->bitmap = al_load_bitmap_f(al_file, file_ext)))
		goto on_error;
	al_fclose(al_file);
	free(slurp);
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	
	image->id = s_next_image_id++;
	return image_ref(image);

on_error:
	console_log(2, "    failed to load image #%u", s_next_image_id++);
	if (al_file != NULL)
		al_fclose(al_file);
	free(slurp);
	free(image);
	return NULL;
}

image_t*
image_read(sfs_file_t* file, int width, int height)
{
	long                   file_pos;
	image_t*               image;
	uint8_t*               line_ptr;
	size_t                 line_size;
	ALLEGRO_LOCKED_REGION* lock = NULL;

	int i_y;

	console_log(3, "reading %ix%i image #%u from open file", width, height, s_next_image_id);
	image = calloc(1, sizeof(image_t));
	file_pos = sfs_ftell(file);
	if (!(image->bitmap = al_create_bitmap(width, height))) goto on_error;
	if (!(lock = al_lock_bitmap(image->bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888, ALLEGRO_LOCK_WRITEONLY)))
		goto on_error;
	line_size = width * 4;
	for (i_y = 0; i_y < height; ++i_y) {
		line_ptr = (uint8_t*)lock->data + i_y * lock->pitch;
		if (sfs_fread(line_ptr, line_size, 1, file) != 1)
			goto on_error;
	}
	al_unlock_bitmap(image->bitmap);
	image->id = s_next_image_id++;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	return image_ref(image);

on_error:
	console_log(3, "    failed!");
	sfs_fseek(file, file_pos, SEEK_SET);
	if (lock != NULL) al_unlock_bitmap(image->bitmap);
	if (image != NULL) {
		if (image->bitmap != NULL) al_destroy_bitmap(image->bitmap);
		free(image);
	}
	return NULL;
}

image_t*
image_read_slice(sfs_file_t* file, image_t* parent, int x, int y, int width, int height)
{
	long          file_pos;
	image_t*      image;
	image_lock_t* lock = NULL;
	color_t       *pline;

	int i_y;

	file_pos = sfs_ftell(file);
	if (!(image = image_new_slice(parent, x, y, width, height))) goto on_error;
	if (!(lock = image_lock(parent))) goto on_error;
	for (i_y = 0; i_y < height; ++i_y) {
		pline = lock->pixels + x + (i_y + y) * lock->pitch;
		if (sfs_fread(pline, width * 4, 1, file) != 1)
			goto on_error;
	}
	image_unlock(parent, lock);
	return image;

on_error:
	sfs_fseek(file, file_pos, SEEK_SET);
	if (lock != NULL)
		image_unlock(parent, lock);
	image_free(image);
	return NULL;
}

image_t*
image_ref(image_t* image)
{
	
	if (image != NULL)
		++image->refcount;
	return image;
}

void
image_free(image_t* image)
{
	if (image == NULL || --image->refcount > 0)
		return;
	
	console_log(3, "disposing image #%u no longer in use",
		image->id);
	al_destroy_bitmap(image->bitmap);
	image_free(image->parent);
	free(image);
}

ALLEGRO_BITMAP*
image_bitmap(image_t* image)
{
	return image->bitmap;
}

int
image_height(const image_t* image)
{
	return image->height;
}

int
image_width(const image_t* image)
{
	return image->width;
}

void
image_draw(image_t* image, int x, int y)
{
	al_draw_bitmap(image->bitmap, x, y, 0x0);
}

void
image_draw_masked(image_t* image, color_t mask, int x, int y)
{
	al_draw_tinted_bitmap(image->bitmap, al_map_rgba(mask.r, mask.g, mask.b, mask.a), x, y, 0x0);
}

void
image_draw_scaled(image_t* image, int x, int y, int width, int height)
{
	al_draw_scaled_bitmap(image->bitmap,
		0, 0, al_get_bitmap_width(image->bitmap), al_get_bitmap_height(image->bitmap),
		x, y, width, height, 0x0);
}

void
image_draw_scaled_masked(image_t* image, color_t mask, int x, int y, int width, int height)
{
	al_draw_tinted_scaled_bitmap(image->bitmap, nativecolor(mask),
		0, 0, al_get_bitmap_width(image->bitmap), al_get_bitmap_height(image->bitmap),
		x, y, width, height, 0x0);
}

void
image_draw_tiled(image_t* image, int x, int y, int width, int height)
{
	image_draw_tiled_masked(image, color_new(255, 255, 255, 255), x, y, width, height);
}

void
image_draw_tiled_masked(image_t* image, color_t mask, int x, int y, int width, int height)
{
	ALLEGRO_COLOR native_mask = nativecolor(mask);
	int           img_w, img_h;
	bool          is_drawing_held;
	int           tile_w, tile_h;

	int i_x, i_y;

	img_w = image->width; img_h = image->height;
	if (img_w >= 16 && img_h >= 16) {
		// tile in hardware whenever possible
		ALLEGRO_VERTEX vbuf[] = {
			{ x, y, 0, 0, 0, native_mask },
			{ x + width, y, 0, width, 0, native_mask },
			{ x, y + height, 0, 0, height, native_mask },
			{ x + width, y + height, 0, width, height, native_mask }
		};
		al_draw_prim(vbuf, NULL, image->bitmap, 0, 4, ALLEGRO_PRIM_TRIANGLE_STRIP);
	}
	else {
		// texture smaller than 16x16, tile it in software (Allegro pads it)
		is_drawing_held = al_is_bitmap_drawing_held();
		al_hold_bitmap_drawing(true);
		for (i_x = width / img_w; i_x >= 0; --i_x) for (i_y = height / img_h; i_y >= 0; --i_y) {
			tile_w = i_x == width / img_w ? width % img_w : img_w;
			tile_h = i_y == height / img_h ? height % img_h : img_h;
			al_draw_tinted_bitmap_region(image->bitmap, native_mask,
				0, 0, tile_w, tile_h,
				x + i_x * img_w, y + i_y * img_h, 0x0);
		}
		al_hold_bitmap_drawing(is_drawing_held);
	}
}

void
image_fill(image_t* image, color_t color)
{
	int             clip_x, clip_y, clip_w, clip_h;
	ALLEGRO_BITMAP* last_target;

	al_get_clipping_rectangle(&clip_x, &clip_y, &clip_w, &clip_h);
	al_reset_clipping_rectangle();
	last_target = al_get_target_bitmap();
	al_set_target_bitmap(image->bitmap);
	al_clear_to_color(al_map_rgba(color.r, color.g, color.b, color.a));
	al_set_target_bitmap(last_target);
	al_set_clipping_rectangle(clip_x, clip_y, clip_w, clip_h);
}

image_lock_t*
image_lock(image_t* image)
{
	ALLEGRO_LOCKED_REGION* ll_lock;

	if (image->lock_count == 0) {
		if (!(ll_lock = al_lock_bitmap(image->bitmap, ALLEGRO_PIXEL_FORMAT_ABGR_8888_LE, ALLEGRO_LOCK_READWRITE)))
			return NULL;
		image_ref(image);
		image->lock.pixels = ll_lock->data;
		image->lock.pitch = ll_lock->pitch / 4;
		image->lock.num_lines = image->height;
	}
	++image->lock_count;
	return &image->lock;
}

bool
image_resize(image_t* image, int width, int height)
{
	ALLEGRO_BITMAP* new_bitmap;
	ALLEGRO_BITMAP* old_target;

	if (width == image->width && height == image->height)
		return true;
	if (!(new_bitmap = al_create_bitmap(width, height)))
		return false;
	old_target = al_get_target_bitmap();
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);
	al_set_target_bitmap(new_bitmap);
	al_draw_scaled_bitmap(image->bitmap, 0, 0, image->width, image->height, 0, 0, width, height, 0x0);
	al_set_target_bitmap(old_target);
	al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
	al_destroy_bitmap(image->bitmap);
	image->bitmap = new_bitmap;
	image->width = al_get_bitmap_width(image->bitmap);
	image->height = al_get_bitmap_height(image->bitmap);
	return true;
}

void
image_unlock(image_t* image, image_lock_t* lock)
{
	// if the caller provides the wrong lock pointer, the image
	// won't be unlocked. this prevents accidental unlocking.
	if (lock != &image->lock) return;

	if (image->lock_count == 0 || --image->lock_count > 0)
		return;
	al_unlock_bitmap(image->bitmap);
	image_free(image);
}
