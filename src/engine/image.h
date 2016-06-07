#ifndef MINISPHERE__IMAGE_H__INCLUDED
#define MINISPHERE__IMAGE_H__INCLUDED

typedef struct image image_t;

typedef
struct image_lock
{
	color_t*  pixels;
	ptrdiff_t pitch;
	int       num_lines;
} image_lock_t;

image_t*        image_new                (int width, int height);
image_t*        image_new_slice          (image_t* parent, int x, int y, int width, int height);
image_t*        image_clone              (const image_t* image);
image_t*        image_load               (const char* filename);
image_t*        image_read               (sfs_file_t* file, int width, int height);
image_t*        image_read_slice         (sfs_file_t* file, image_t* parent, int x, int y, int width, int height);
image_t*        image_ref                (image_t* image);
void            image_free               (image_t* image);
ALLEGRO_BITMAP* image_bitmap             (image_t* image);
int             image_height             (const image_t* image);
int             image_width              (const image_t* image);
void            image_draw               (image_t* image, int x, int y);
void            image_draw_masked        (image_t* image, color_t mask, int x, int y);
void            image_draw_scaled        (image_t* image, int x, int y, int width, int height);
void            image_draw_scaled_masked (image_t* image, color_t mask, int x, int y, int width, int height);
void            image_draw_tiled         (image_t* image, int x, int y, int width, int height);
void            image_draw_tiled_masked  (image_t* image, color_t mask, int x, int y, int width, int height);
void            image_fill               (image_t* image, color_t color);
image_lock_t*   image_lock               (image_t* image);
bool            image_resize             (image_t* image, int width, int height);
void            image_unlock             (image_t* image, image_lock_t* lock);

#endif // MINISPHERE__IMAGE_H__INCLUDED
