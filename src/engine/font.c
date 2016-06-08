#include "minisphere.h"
#include "font.h"

#include "api.h"
#include "atlas.h"
#include "color.h"
#include "image.h"
#include "unicode.h"

static void update_font_metrics (font_t* font);

struct font
{
	unsigned int       refcount;
	unsigned int       id;
	int                height;
	int                min_width;
	int                max_width;
	uint32_t           num_glyphs;
	struct font_glyph* glyphs;
};

struct font_glyph
{
	int      width, height;
	image_t* image;
};

struct wraptext
{
	int    num_lines;
	char*  buffer;
	size_t pitch;
};

#pragma pack(push, 1)
struct rfn_header
{
	char     signature[4];
	uint16_t version;
	uint16_t num_chars;
	char     reserved[248];
};

struct rfn_glyph_header
{
	uint16_t width;
	uint16_t height;
	char     reserved[28];
};
#pragma pack(pop)

static unsigned int s_next_font_id = 0;

font_t*
font_load(const char* filename)
{
	image_t*                atlas = NULL;
	int                     atlas_x, atlas_y;
	int                     atlas_size_x, atlas_size_y;
	sfs_file_t*             file;
	font_t*                 font = NULL;
	struct font_glyph*      glyph;
	struct rfn_glyph_header glyph_hdr;
	long                    glyph_start;
	uint8_t*                grayscale;
	image_lock_t*           lock = NULL;
	int                     max_x = 0, max_y = 0;
	int                     min_width = INT_MAX;
	int64_t                 n_glyphs_per_row;
	int                     pixel_size;
	struct rfn_header       rfn;
	uint8_t                 *psrc;
	color_t                 *pdest;

	int i, x, y;
	
	console_log(2, "loading font #%u as `%s`", s_next_font_id, filename);
	
	memset(&rfn, 0, sizeof(struct rfn_header));

	if ((file = sfs_fopen(g_fs, filename, "rb")) == NULL) goto on_error;
	if (!(font = calloc(1, sizeof(font_t)))) goto on_error;
	if (sfs_fread(&rfn, sizeof(struct rfn_header), 1, file) != 1)
		goto on_error;
	pixel_size = (rfn.version == 1) ? 1 : 4;
	if (!(font->glyphs = calloc(rfn.num_chars, sizeof(struct font_glyph))))
		goto on_error;

	// pass 1: load glyph headers and find largest glyph
	glyph_start = sfs_ftell(file);
	for (i = 0; i < rfn.num_chars; ++i) {
		glyph = &font->glyphs[i];
		if (sfs_fread(&glyph_hdr, sizeof(struct rfn_glyph_header), 1, file) != 1)
			goto on_error;
		sfs_fseek(file, glyph_hdr.width * glyph_hdr.height * pixel_size, SFS_SEEK_CUR);
		max_x = fmax(glyph_hdr.width, max_x);
		max_y = fmax(glyph_hdr.height, max_y);
		min_width = fmin(min_width, glyph_hdr.width);
		glyph->width = glyph_hdr.width;
		glyph->height = glyph_hdr.height;
	}
	font->num_glyphs = rfn.num_chars;
	font->min_width = min_width;
	font->max_width = max_x;
	font->height = max_y;

	// create glyph atlas
	n_glyphs_per_row = ceil(sqrt(rfn.num_chars));
	atlas_size_x = max_x * n_glyphs_per_row;
	atlas_size_y = max_y * n_glyphs_per_row;
	if ((atlas = image_new(atlas_size_x, atlas_size_y)) == NULL)
		goto on_error;

	// pass 2: load glyph data
	sfs_fseek(file, glyph_start, SFS_SEEK_SET);
	if (!(lock = image_lock(atlas))) goto on_error;
	for (i = 0; i < rfn.num_chars; ++i) {
		glyph = &font->glyphs[i];
		if (sfs_fread(&glyph_hdr, sizeof(struct rfn_glyph_header), 1, file) != 1)
			goto on_error;
		atlas_x = i % n_glyphs_per_row * max_x;
		atlas_y = i / n_glyphs_per_row * max_y;
		switch (rfn.version) {
		case 1: // RFN v1: 8-bit grayscale glyphs
			if (!(glyph->image = image_new_slice(atlas, atlas_x, atlas_y, glyph_hdr.width, glyph_hdr.height)))
				goto on_error;
			grayscale = malloc(glyph_hdr.width * glyph_hdr.height);
			if (sfs_fread(grayscale, glyph_hdr.width * glyph_hdr.height, 1, file) != 1)
				goto on_error;
			psrc = grayscale;
			pdest = lock->pixels + atlas_x + atlas_y * lock->pitch;
			for (y = 0; y < glyph_hdr.height; ++y) {
				for (x = 0; x < glyph_hdr.width; ++x)
					pdest[x] = color_new(psrc[x], psrc[x], psrc[x], 255);
				pdest += lock->pitch;
				psrc += glyph_hdr.width;
			}
			break;
		case 2: // RFN v2: 32-bit truecolor glyphs
			if (!(glyph->image = image_read_slice(file, atlas, atlas_x, atlas_y, glyph_hdr.width, glyph_hdr.height)))
				goto on_error;
			break;
		}
	}
	image_unlock(atlas, lock);
	sfs_fclose(file);
	image_free(atlas);
	
	font->id = s_next_font_id++;
	return font_ref(font);

on_error:
	console_log(2, "failed to load font #%u", s_next_font_id++);
	sfs_fclose(file);
	if (font != NULL) {
		for (i = 0; i < rfn.num_chars; ++i) {
			if (font->glyphs[i].image != NULL) image_free(font->glyphs[i].image);
		}
		free(font->glyphs);
		free(font);
	}
	if (lock != NULL) image_unlock(atlas, lock);
	if (atlas != NULL) image_free(atlas);
	return NULL;
}

font_t*
font_ref(font_t* font)
{
	++font->refcount;
	return font;
}

void
font_free(font_t* font)
{
	uint32_t i;
	
	if (font == NULL || --font->refcount > 0)
		return;
	
	console_log(3, "disposing font #%u no longer in use", font->id);
	for (i = 0; i < font->num_glyphs; ++i) {
		image_free(font->glyphs[i].image);
	}
	free(font->glyphs);
	free(font);
}

image_t*
font_glyph_image(const font_t* font, int codepoint)
{
	return font->glyphs[codepoint].image;
}

int
font_glyph_width(const font_t* font, int codepoint)
{
	return font->glyphs[codepoint].width;
}

int
font_height(const font_t* font)
{
	return font->height;
}

void
font_get_metrics(const font_t* font, int* out_min_width, int* out_max_width, int* out_line_height)
{
	if (out_min_width) *out_min_width = font->min_width;
	if (out_max_width) *out_max_width = font->max_width;
	if (out_line_height) *out_line_height = font->height;
}

int
font_get_width(const font_t* font, const char* text)
{
	uint8_t  ch_byte;
	uint32_t cp;
	uint32_t utf8state;
	int      width = 0;
	
	for (;;) {
		utf8state = UTF8_ACCEPT;
		while (utf8decode(&utf8state, &cp, ch_byte = *text++) > UTF8_REJECT);
		if (utf8state == UTF8_REJECT && ch_byte == '\0')
			--text;  // don't eat NUL terminator
		cp = cp == 0x20AC ? 128
			: cp == 0x201A ? 130
			: cp == 0x0192 ? 131
			: cp == 0x201E ? 132
			: cp == 0x2026 ? 133
			: cp == 0x2020 ? 134
			: cp == 0x2021 ? 135
			: cp == 0x02C6 ? 136
			: cp == 0x2030 ? 137
			: cp == 0x0160 ? 138
			: cp == 0x2039 ? 139
			: cp == 0x0152 ? 140
			: cp == 0x017D ? 142
			: cp == 0x2018 ? 145
			: cp == 0x2019 ? 146
			: cp == 0x201C ? 147
			: cp == 0x201D ? 148
			: cp == 0x2022 ? 149
			: cp == 0x2013 ? 150
			: cp == 0x2014 ? 151
			: cp == 0x02DC ? 152
			: cp == 0x2122 ? 153
			: cp == 0x0161 ? 154
			: cp == 0x203A ? 155
			: cp == 0x0153 ? 156
			: cp == 0x017E ? 158
			: cp == 0x0178 ? 159
			: cp;
		cp = utf8state == UTF8_ACCEPT
			? cp < (uint32_t)font->num_glyphs ? cp : 0x1A
			: 0x1A;
		if (cp == '\0') break;
		width += font->glyphs[cp].width;
	}
	return width;
}

void
font_draw_text(const font_t* font, color_t color, int x, int y, text_align_t alignment, const char* text)
{
	uint8_t  ch_byte;
	uint32_t cp;
	int      tab_width;
	uint32_t utf8state;
	
	if (alignment == TEXT_ALIGN_CENTER)
		x -= font_get_width(font, text) / 2;
	else if (alignment == TEXT_ALIGN_RIGHT)
		x -= font_get_width(font, text);
	
	tab_width = font->glyphs[' '].width * 3;
	al_hold_bitmap_drawing(true);
	for (;;) {
		utf8state = UTF8_ACCEPT;
		while (utf8decode(&utf8state, &cp, ch_byte = *text++) > UTF8_REJECT);
		if (utf8state == UTF8_REJECT && ch_byte == '\0')
			--text;  // don't eat NUL terminator
		cp = cp == 0x20AC ? 128
			: cp == 0x201A ? 130
			: cp == 0x0192 ? 131
			: cp == 0x201E ? 132
			: cp == 0x2026 ? 133
			: cp == 0x2020 ? 134
			: cp == 0x2021 ? 135
			: cp == 0x02C6 ? 136
			: cp == 0x2030 ? 137
			: cp == 0x0160 ? 138
			: cp == 0x2039 ? 139
			: cp == 0x0152 ? 140
			: cp == 0x017D ? 142
			: cp == 0x2018 ? 145
			: cp == 0x2019 ? 146
			: cp == 0x201C ? 147
			: cp == 0x201D ? 148
			: cp == 0x2022 ? 149
			: cp == 0x2013 ? 150
			: cp == 0x2014 ? 151
			: cp == 0x02DC ? 152
			: cp == 0x2122 ? 153
			: cp == 0x0161 ? 154
			: cp == 0x203A ? 155
			: cp == 0x0153 ? 156
			: cp == 0x017E ? 158
			: cp == 0x0178 ? 159
			: cp;
		cp = utf8state == UTF8_ACCEPT
			? cp < (uint32_t)font->num_glyphs ? cp : 0x1A
			: 0x1A;
		if (cp == '\0')
			break;
		else if (cp == '\t')
			x += tab_width;
		else {
			image_draw_masked(font->glyphs[cp].image, color, x, y);
			x += font->glyphs[cp].width;
		}
	}
	al_hold_bitmap_drawing(false);
}

wraptext_t*
wraptext_new(const char* text, const font_t* font, int width)
{
	char*       buffer = NULL;
	uint8_t     ch_byte;
	char*		carry;
	size_t      ch_size;
	uint32_t    cp;
	int         glyph_width;
	bool        is_line_end = false;
	int         line_idx;
	int         line_width;
	int         max_lines = 10;
	char*       last_break;
	char*       last_space;
	char*       last_tab;
	char*       line_buffer;
	size_t      line_length;
	char*       new_buffer;
	size_t      pitch;
	uint32_t    utf8state;
	wraptext_t* wraptext;
	const char  *p, *start;

	if (!(wraptext = calloc(1, sizeof(wraptext_t)))) goto on_error;
	
	// allocate initial buffer
	font_get_metrics(font, &glyph_width, NULL, NULL);
	pitch = 4 * (glyph_width > 0 ? width / glyph_width : width) + 3;
	if (!(buffer = malloc(max_lines * pitch))) goto on_error;
	if (!(carry = malloc(pitch))) goto on_error;

	// run through one character at a time, carrying as necessary
	line_buffer = buffer; line_buffer[0] = '\0';
	line_idx = 0; line_width = 0; line_length = 0;
	memset(line_buffer, 0, pitch);  // fill line with NULs
	p = text;
	do {
		utf8state = UTF8_ACCEPT; start = p;
		while (utf8decode(&utf8state, &cp, ch_byte = *p++) > UTF8_REJECT);
		if (utf8state == UTF8_REJECT && ch_byte == '\0')
			--p;  // don't eat NUL terminator
		ch_size = p - start;
		cp = cp == 0x20AC ? 128
			: cp == 0x201A ? 130
			: cp == 0x0192 ? 131
			: cp == 0x201E ? 132
			: cp == 0x2026 ? 133
			: cp == 0x2020 ? 134
			: cp == 0x2021 ? 135
			: cp == 0x02C6 ? 136
			: cp == 0x2030 ? 137
			: cp == 0x0160 ? 138
			: cp == 0x2039 ? 139
			: cp == 0x0152 ? 140
			: cp == 0x017D ? 142
			: cp == 0x2018 ? 145
			: cp == 0x2019 ? 146
			: cp == 0x201C ? 147
			: cp == 0x201D ? 148
			: cp == 0x2022 ? 149
			: cp == 0x2013 ? 150
			: cp == 0x2014 ? 151
			: cp == 0x02DC ? 152
			: cp == 0x2122 ? 153
			: cp == 0x0161 ? 154
			: cp == 0x203A ? 155
			: cp == 0x0153 ? 156
			: cp == 0x017E ? 158
			: cp == 0x0178 ? 159
			: cp;
		cp = utf8state == UTF8_ACCEPT
			? cp < (uint32_t)font->num_glyphs ? cp : 0x1A
			: 0x1A;
		switch (cp) {
		case '\n': case '\r':  // explicit newline
			if (cp == '\r' && *p == '\n') ++text;  // CRLF
			is_line_end = true;
			break;
		case '\t':  // tab
			line_buffer[line_length++] = cp;
			line_width += font_get_width(font, "   ");
			is_line_end = false;
			break;
		case '\0':  // NUL terminator
			is_line_end = line_length > 0;  // commit last line on EOT
			break;
		default:  // default case, copy character as-is
			memcpy(line_buffer + line_length, start, ch_size);
			line_length += ch_size;
			line_width += font_glyph_width(font, cp);
			is_line_end = false;
		}
		if (is_line_end) carry[0] = '\0';
		if (line_width > width || line_length >= pitch - 1) {
			// wrap width exceeded, carry current word to next line
			is_line_end = true;
			last_space = strrchr(line_buffer, ' ');
			last_tab = strrchr(line_buffer, '\t');
			last_break = last_space > last_tab ? last_space : last_tab;
			if (last_break != NULL)  // word break (space or tab) found
				strcpy(carry, last_break + 1);
			else  // no word break, so just carry last character
				sprintf(carry, "%c", line_buffer[line_length - 1]);
			line_buffer[line_length - strlen(carry)] = '\0';
		}
		if (is_line_end) {
			// do we need to enlarge the buffer?
			if (++line_idx >= max_lines) {
				max_lines *= 2;
				if (!(new_buffer = realloc(buffer, max_lines * pitch)))
					goto on_error;
				buffer = new_buffer;
				line_buffer = buffer + line_idx * pitch;
			}
			else
				line_buffer += pitch;
			
			memset(line_buffer, 0, pitch);  // fill line with NULs

			// copy carry text into new line
			line_width = font_get_width(font, carry);
			line_length = strlen(carry);
			strcpy(line_buffer, carry);
		}
	} while (cp != '\0');
	free(carry);
	wraptext->num_lines = line_idx;
	wraptext->buffer = buffer;
	wraptext->pitch = pitch;
	return wraptext;

on_error:
	free(buffer);
	free(wraptext);
	return NULL;
}

void
wraptext_free(wraptext_t* wraptext)
{
	free(wraptext->buffer);
	free(wraptext);
}

const char*
wraptext_line(const wraptext_t* wraptext, int line_index)
{
	return wraptext->buffer + line_index * wraptext->pitch;
}

int
wraptext_len(const wraptext_t* wraptext)
{
	return wraptext->num_lines;
}

static void
update_font_metrics(font_t* font)
{
	int max_x = 0, max_y = 0;
	int min_width = INT_MAX;

	uint32_t i;

	for (i = 0; i < font->num_glyphs; ++i) {
		font->glyphs[i].width = image_width(font->glyphs[i].image);
		font->glyphs[i].height = image_height(font->glyphs[i].image);
		min_width = fmin(font->glyphs[i].width, min_width);
		max_x = fmax(font->glyphs[i].width, max_x);
		max_y = fmax(font->glyphs[i].height, max_y);
	}
	font->min_width = min_width;
	font->max_width = max_x;
	font->height = max_y;
}
