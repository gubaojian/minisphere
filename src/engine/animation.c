#include "minisphere.h"
#include "animation.h"

#include <libmng.h>
#include "image.h"

static mng_ptr    mng_cb_malloc        (mng_size_t size);
static void       mng_cb_free          (mng_ptr ptr, mng_size_t size);
static mng_bool   mng_cb_openstream    (mng_handle stream);
static mng_bool   mng_cb_closestream   (mng_handle stream);
static mng_ptr    mng_cb_getcanvasline (mng_handle stream, mng_uint32 line_num);
static mng_uint32 mng_cb_gettickcount  (mng_handle stream);
static mng_bool   mng_cb_processheader (mng_handle stream, mng_uint32 width, mng_uint32 height);
static mng_bool   mng_cb_readdata      (mng_handle stream, mng_ptr buf, mng_uint32 n_bytes, mng_uint32p p_readsize);
static mng_bool   mng_cb_refresh       (mng_handle stream, mng_uint32 x, mng_uint32 y, mng_uint32 width, mng_uint32 height);
static mng_bool   mng_cb_settimer      (mng_handle stream, mng_uint32 msecs);

struct animation
{
	unsigned int  refcount;
	unsigned int  id;
	int           delay;
	sfs_file_t*   file;
	image_t*      frame;
	bool          is_frame_ready;
	image_lock_t* lock;
	mng_handle    stream;
	unsigned int  w, h;
};

static unsigned int s_next_animation_id = 0;

animation_t*
animation_new(const char* path)
{
	animation_t* anim;

	console_log(2, "loading animation #%u as `%s`", s_next_animation_id, path);
	
	if (!(anim = calloc(1, sizeof(animation_t))))
		goto on_error;
	if (!(anim->stream = mng_initialize(anim, mng_cb_malloc, mng_cb_free, NULL)))
		goto on_error;
	mng_setcb_openstream(anim->stream, mng_cb_openstream);
	mng_setcb_closestream(anim->stream, mng_cb_closestream);
	mng_setcb_getcanvasline(anim->stream, mng_cb_getcanvasline);
	mng_setcb_gettickcount(anim->stream, mng_cb_gettickcount);
	mng_setcb_processheader(anim->stream, mng_cb_processheader);
	mng_setcb_readdata(anim->stream, mng_cb_readdata);
	mng_setcb_refresh(anim->stream, mng_cb_refresh);
	mng_setcb_settimer(anim->stream, mng_cb_settimer);
	if (!(anim->file = sfs_fopen(g_fs, path, NULL, "rb")))
		goto on_error;
	if (mng_read(anim->stream) != MNG_NOERROR) goto on_error;
	anim->id = s_next_animation_id++;
	
	if (!animation_update(anim))
		goto on_error;
	
	return animation_ref(anim);

on_error:
	console_log(2, "failed to load animation #%u", s_next_animation_id++);
	if (anim != NULL) {
		if (anim->stream != NULL) mng_cleanup(&anim->stream);
		if (anim->file != NULL) sfs_fclose(anim->file);
		if (anim->frame != NULL) {
			image_free(anim->frame);
		}
		free(anim);
	}
	return NULL;
}

animation_t*
animation_ref(animation_t* animation)
{
	++animation->refcount;
	return animation;
}

void
animation_free(animation_t* animation)
{
	if (animation == NULL || --animation->refcount > 0)
		return;
	
	console_log(3, "disposing animation #%u no longer in use",
		animation->id);
	mng_cleanup(&animation->stream);
	sfs_fclose(animation->file);
	image_free(animation->frame);
	free(animation);
}

int
animation_delay(const animation_t* anim)
{
	return anim->delay;
}

image_t*
animation_frame(const animation_t* anim)
{
	return anim->frame;
}

int
animation_height(const animation_t* anim)
{
	return anim->h;
}

int
animation_num_frames(const animation_t* anim)
{
	return mng_get_framecount(anim->stream);
}

int
animation_width(const animation_t* anim)
{
	return anim->w;
}

bool
animation_update(animation_t* anim)
{
	if (!(anim->lock = image_lock(anim->frame)))
		return false;
	if (!anim->is_frame_ready)
		mng_display(anim->stream);
	else if (mng_display_resume(anim->stream) != MNG_NEEDTIMERWAIT)
		mng_display_reset(anim->stream);
	image_unlock(anim->frame, anim->lock);
	anim->is_frame_ready = true;
	return true;
}

static mng_ptr
mng_cb_malloc(mng_size_t size)
{
	return calloc(1, size);
}

static void
mng_cb_free(mng_ptr ptr, mng_size_t size)
{
	free(ptr);
}

static mng_bool
mng_cb_openstream(mng_handle stream)
{
	return MNG_TRUE;
}

static mng_bool
mng_cb_closestream(mng_handle stream)
{
	return MNG_TRUE;
}

static mng_ptr
mng_cb_getcanvasline(mng_handle stream, mng_uint32 line_num)
{
	animation_t* anim = mng_get_userdata(stream);

	return anim->lock->pixels + line_num * anim->lock->pitch;
}

static mng_uint32
mng_cb_gettickcount(mng_handle stream)
{
	return al_get_time() * 1000;
}

static mng_bool
mng_cb_processheader(mng_handle stream, mng_uint32 width, mng_uint32 height)
{
	animation_t* anim = mng_get_userdata(stream);

	anim->w = width; anim->h = height;
	image_free(anim->frame);
	if (!(anim->frame = image_new(anim->w, anim->h)))
		goto on_error;
	mng_set_canvasstyle(stream, MNG_CANVAS_RGBA8);
	return MNG_TRUE;

on_error:
	image_free(anim->frame);
	return MNG_FALSE;
}

static mng_bool
mng_cb_readdata(mng_handle stream, mng_ptr buf, mng_uint32 n_bytes, mng_uint32p p_readsize)
{
	animation_t* anim = mng_get_userdata(stream);

	*p_readsize = (mng_uint32)sfs_fread(buf, 1, n_bytes, anim->file);
	return MNG_TRUE;
}

static mng_bool
mng_cb_refresh(mng_handle stream, mng_uint32 x, mng_uint32 y, mng_uint32 width, mng_uint32 height)
{
	return MNG_TRUE;
}

static mng_bool
mng_cb_settimer(mng_handle stream, mng_uint32 msecs)
{
	animation_t* anim = mng_get_userdata(stream);

	anim->delay = msecs;
	return MNG_TRUE;
}
