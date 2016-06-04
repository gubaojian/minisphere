#include "minisphere.h"
#include "file.h"

#include "api.h"

static duk_ret_t js_DoesFileExist    (duk_context* ctx);
static duk_ret_t js_GetDirectoryList (duk_context* ctx);
static duk_ret_t js_GetFileList      (duk_context* ctx);
static duk_ret_t js_CreateDirectory  (duk_context* ctx);
static duk_ret_t js_RemoveDirectory  (duk_context* ctx);
static duk_ret_t js_RemoveFile       (duk_context* ctx);
static duk_ret_t js_Rename           (duk_context* ctx);

static duk_ret_t js_new_FileStream          (duk_context* ctx);
static duk_ret_t js_FileStream_finalize     (duk_context* ctx);
static duk_ret_t js_FileStream_get_position (duk_context* ctx);
static duk_ret_t js_FileStream_set_position (duk_context* ctx);
static duk_ret_t js_FileStream_get_length   (duk_context* ctx);
static duk_ret_t js_FileStream_close        (duk_context* ctx);
static duk_ret_t js_FileStream_read         (duk_context* ctx);
static duk_ret_t js_FileStream_readDouble   (duk_context* ctx);
static duk_ret_t js_FileStream_readFloat    (duk_context* ctx);
static duk_ret_t js_FileStream_readInt      (duk_context* ctx);
static duk_ret_t js_FileStream_readPString  (duk_context* ctx);
static duk_ret_t js_FileStream_readString   (duk_context* ctx);
static duk_ret_t js_FileStream_readUInt     (duk_context* ctx);
static duk_ret_t js_FileStream_write        (duk_context* ctx);
static duk_ret_t js_FileStream_writeDouble  (duk_context* ctx);
static duk_ret_t js_FileStream_writeFloat   (duk_context* ctx);
static duk_ret_t js_FileStream_writeInt     (duk_context* ctx);
static duk_ret_t js_FileStream_writePString (duk_context* ctx);
static duk_ret_t js_FileStream_writeString  (duk_context* ctx);
static duk_ret_t js_FileStream_writeUInt    (duk_context* ctx);

struct kevfile
{
	unsigned int    id;
	sandbox_t*      fs;
	ALLEGRO_CONFIG* conf;
	char*           filename;
	bool            is_dirty;
};

static bool read_vsize_int   (sfs_file_t* file, intmax_t* p_value, int size, bool little_endian);
static bool read_vsize_uint  (sfs_file_t* file, intmax_t* p_value, int size, bool little_endian);
static bool write_vsize_int  (sfs_file_t* file, intmax_t value, int size, bool little_endian);
static bool write_vsize_uint (sfs_file_t* file, intmax_t value, int size, bool little_endian);

static unsigned int s_next_file_id = 0;

kevfile_t*
kev_open(sandbox_t* fs, const char* filename, bool can_create)
{
	kevfile_t*    file;
	ALLEGRO_FILE* memfile = NULL;
	void*         slurp;
	size_t        slurp_size;
	
	console_log(2, "opening kevfile #%u as `%s`", s_next_file_id, filename);
	file = calloc(1, sizeof(kevfile_t));
	if (slurp = sfs_fslurp(fs, filename, NULL, &slurp_size)) {
		memfile = al_open_memfile(slurp, slurp_size, "rb");
		if (!(file->conf = al_load_config_file_f(memfile)))
			goto on_error;
		al_fclose(memfile);
		free(slurp);
	}
	else {
		console_log(3, "    `%s` doesn't exist", filename);
		if (!can_create || !(file->conf = al_create_config()))
			goto on_error;
	}
	file->fs = ref_sandbox(fs);
	file->filename = strdup(filename);
	file->id = s_next_file_id++;
	return file;

on_error:
	console_log(2, "    failed to open kevfile #%u", s_next_file_id++);
	if (memfile != NULL) al_fclose(memfile);
	if (file->conf != NULL)
		al_destroy_config(file->conf);
	free(file);
	return NULL;
}

void
kev_close(kevfile_t* file)
{
	if (file == NULL)
		return;
	
	console_log(3, "disposing kevfile #%u no longer in use", file->id);
	if (file->is_dirty)
		kev_save(file);
	al_destroy_config(file->conf);
	free_sandbox(file->fs);
	free(file);
}

int
kev_num_keys(kevfile_t* file)
{
	ALLEGRO_CONFIG_ENTRY* iter;
	const char*           key;
	int                   sum;

	sum = 0;
	key = al_get_first_config_entry(file->conf, NULL, &iter);
	while (key != NULL) {
		++sum;
		key = al_get_next_config_entry(&iter);
	}
	return sum;
}

const char*
kev_get_key(kevfile_t* file, int index)
{
	ALLEGRO_CONFIG_ENTRY* iter;
	const char*           name;

	int i = 0;

	name = al_get_first_config_entry(file->conf, NULL, &iter);
	while (name != NULL) {
		if (i == index)
			return name;
		++i;
		name = al_get_next_config_entry(&iter);
	}
	return NULL;
}

bool
kev_read_bool(kevfile_t* file, const char* key, bool def_value)
{
	const char* string;
	bool        value;

	string = kev_read_string(file, key, def_value ? "true" : "false");
	value = strcasecmp(string, "true") == 0;
	return value;
}

double
kev_read_float(kevfile_t* file, const char* key, double def_value)
{
	char        def_string[500];
	const char* string;
	double      value;

	sprintf(def_string, "%g", def_value);
	string = kev_read_string(file, key, def_string);
	value = atof(string);
	return value;
}

const char*
kev_read_string(kevfile_t* file, const char* key, const char* def_value)
{
	const char* value;
	
	console_log(2, "reading key `%s` from kevfile #%u", key, file->id);
	if (!(value = al_get_config_value(file->conf, NULL, key)))
		value = def_value;
	return value;
}

bool
kev_save(kevfile_t* file)
{
	void*         buffer = NULL;
	size_t        file_size;
	bool          is_aok = false;
	ALLEGRO_FILE* memfile;
	size_t        next_buf_size;
	sfs_file_t*   sfs_file = NULL;

	console_log(3, "saving kevfile #%u as `%s`", file->id, file->filename);
	next_buf_size = 4096;
	while (!is_aok) {
		buffer = realloc(buffer, next_buf_size);
		memfile = al_open_memfile(buffer, next_buf_size, "wb");
		next_buf_size *= 2;
		al_save_config_file_f(memfile, file->conf);
		is_aok = !al_feof(memfile);
		file_size = al_ftell(memfile);
		al_fclose(memfile); memfile = NULL;
	}
	if (!(sfs_file = sfs_fopen(file->fs, file->filename, NULL, "wt")))
		goto on_error;
	sfs_fwrite(buffer, file_size, 1, sfs_file);
	sfs_fclose(sfs_file);
	free(buffer);
	return true;

on_error:
	if (memfile != NULL)
		al_fclose(memfile);
	sfs_fclose(sfs_file);
	free(buffer);
	return false;
}

void
kev_write_bool(kevfile_t* file, const char* key, bool value)
{
	console_log(3, "writing boolean to kevfile #%u, key `%s`", file->id, key);
	al_set_config_value(file->conf, NULL, key, value ? "true" : "false");
	file->is_dirty = true;
}

void
kev_write_float(kevfile_t* file, const char* key, double value)
{
	char string[500];

	console_log(3, "writing number to kevfile #%u, key `%s`", file->id, key);
	sprintf(string, "%g", value);
	al_set_config_value(file->conf, NULL, key, string);
	file->is_dirty = true;
}

void
kev_write_string(kevfile_t* file, const char* key, const char* value)
{
	console_log(3, "writing string to kevfile #%u, key `%s`", file->id, key);
	al_set_config_value(file->conf, NULL, key, value);
	file->is_dirty = true;
}

static bool
read_vsize_int(sfs_file_t* file, intmax_t* p_value, int size, bool little_endian)
{
	// NOTE: supports decoding values up to 48-bit (6 bytes).  don't specify
	//       size > 6 unless you want a segfault!

	uint8_t data[6];
	int     mul = 1;
	
	int i;
	
	if (sfs_fread(data, 1, size, file) != size)
		return false;
	
	// variable-sized int decoding adapted from Node.js
	if (little_endian) {
		*p_value = data[i = 0];
		while (++i < size && (mul *= 0x100))
			*p_value += data[i] * mul;
	}
	else {
		*p_value = data[i = size - 1];
		while (i > 0 && (mul *= 0x100))
			*p_value += data[--i] * mul;
	}
	if (*p_value >= mul * 0x80)
		*p_value -= pow(2, 8 * size);

	return true;
}

static bool
read_vsize_uint(sfs_file_t* file, intmax_t* p_value, int size, bool little_endian)
{
	// NOTE: supports decoding values up to 48-bit (6 bytes).  don't specify
	//       size > 6 unless you want a segfault!

	uint8_t data[6];
	int     mul = 1;

	int i;

	if (sfs_fread(data, 1, size, file) != size)
		return false;

	// variable-sized uint decoding adapted from Node.js
	if (little_endian) {
		*p_value = data[i = 0];
		while (++i < size && (mul *= 0x100))
			*p_value += data[i] * mul;
	}
	else {
		*p_value = data[--size];
		while (size > 0 && (mul *= 0x100))
			*p_value += data[--size] * mul;
	}

	return true;
}

static bool
write_vsize_int(sfs_file_t* file, intmax_t value, int size, bool little_endian)
{
	// NOTE: supports encoding values up to 48-bit (6 bytes).  don't specify
	//       size > 6 unless you want a segfault!

	uint8_t data[6];
	int     mul = 1;
	int     sub = 0;

	int i;

	// variable-sized int encoding adapted from Node.js
	if (little_endian) {
		data[i = 0] = value & 0xFF;
		while (++i < size && (mul *= 0x100)) {
			if (value < 0 && sub == 0 && data[i - 1] != 0)
				sub = 1;
			data[i] = (value / mul - sub) & 0xFF;
		}
	}
	else {
		data[i = size - 1] = value & 0xFF;
		while (--i >= 0 && (mul *= 0x100)) {
			if (value < 0 && sub == 0 && data[i + 1] != 0)
				sub = 1;
			data[i] = (value / mul - sub) & 0xFF;
		}
	}
	
	return sfs_fwrite(data, 1, size, file) == size;
}

static bool
write_vsize_uint(sfs_file_t* file, intmax_t value, int size, bool little_endian)
{
	// NOTE: supports encoding values up to 48-bit (6 bytes).  don't specify
	//       size > 6 unless you want a segfault!
	
	uint8_t data[6];
	int     mul = 1;

	int i;

	// variable-sized uint encoding adapted from Node.js
	if (little_endian) {
		data[i = 0] = value & 0xFF;
		while (++i < size && (mul *= 0x100))
			data[i] = (value / mul) & 0xFF;
	}
	else {
		data[i = size - 1] = value & 0xFF;
		while (--i >= 0 && (mul *= 0x100))
			data[i] = (value / mul) & 0xFF;
	}

	return sfs_fwrite(data, 1, size, file) == size;
}

void
init_file_api(void)
{
	// File API functions
	api_register_method(g_duk, NULL, "DoesFileExist", js_DoesFileExist);
	api_register_method(g_duk, NULL, "GetDirectoryList", js_GetDirectoryList);
	api_register_method(g_duk, NULL, "GetFileList", js_GetFileList);
	api_register_method(g_duk, NULL, "CreateDirectory", js_CreateDirectory);
	api_register_method(g_duk, NULL, "RemoveDirectory", js_RemoveDirectory);
	api_register_method(g_duk, NULL, "RemoveFile", js_RemoveFile);
	api_register_method(g_duk, NULL, "Rename", js_Rename);

	// FileStream object
	api_register_ctor(g_duk, "FileStream", js_new_FileStream, js_FileStream_finalize);
	api_register_prop(g_duk, "FileStream", "length", js_FileStream_get_length, NULL);
	api_register_prop(g_duk, "FileStream", "position", js_FileStream_get_position, js_FileStream_set_position);
	api_register_prop(g_duk, "FileStream", "size", js_FileStream_get_length, NULL);
	api_register_method(g_duk, "FileStream", "close", js_FileStream_close);
	api_register_method(g_duk, "FileStream", "read", js_FileStream_read);
	api_register_method(g_duk, "FileStream", "readDouble", js_FileStream_readDouble);
	api_register_method(g_duk, "FileStream", "readFloat", js_FileStream_readFloat);
	api_register_method(g_duk, "FileStream", "readInt", js_FileStream_readInt);
	api_register_method(g_duk, "FileStream", "readPString", js_FileStream_readPString);
	api_register_method(g_duk, "FileStream", "readString", js_FileStream_readString);
	api_register_method(g_duk, "FileStream", "readUInt", js_FileStream_readUInt);
	api_register_method(g_duk, "FileStream", "write", js_FileStream_write);
	api_register_method(g_duk, "FileStream", "writeDouble", js_FileStream_writeDouble);
	api_register_method(g_duk, "FileStream", "writeFloat", js_FileStream_writeFloat);
	api_register_method(g_duk, "FileStream", "writeInt", js_FileStream_writeInt);
	api_register_method(g_duk, "FileStream", "writePString", js_FileStream_writePString);
	api_register_method(g_duk, "FileStream", "writeString", js_FileStream_writeString);
	api_register_method(g_duk, "FileStream", "writeUInt", js_FileStream_writeUInt);
}

static duk_ret_t
js_DoesFileExist(duk_context* ctx)
{
	const char* filename;

	filename = duk_require_path(ctx, 0, NULL, true);
	duk_push_boolean(ctx, sfs_fexist(g_fs, filename, NULL));
	return 1;
}

static duk_ret_t
js_GetDirectoryList(duk_context* ctx)
{
	int n_args = duk_get_top(ctx);
	const char* dirname = n_args >= 1
		? duk_require_path(ctx, 0, NULL, true)
		: "";

	vector_t*  list;
	lstring_t* *p_filename;

	iter_t iter;

	list = list_filenames(g_fs, dirname, NULL, true);
	duk_push_array(ctx);
	iter = vector_enum(list);
	while (p_filename = vector_next(&iter)) {
		duk_push_string(ctx, lstr_cstr(*p_filename));
		duk_put_prop_index(ctx, -2, (duk_uarridx_t)iter.index);
		lstr_free(*p_filename);
	}
	vector_free(list);
	return 1;
}

static duk_ret_t
js_GetFileList(duk_context* ctx)
{
	const char* directory_name;
	vector_t*   list;
	int         num_args;

	iter_t iter;
	lstring_t* *p_filename;

	num_args = duk_get_top(ctx);
	directory_name = num_args >= 1
		? duk_require_path(ctx, 0, NULL, true)
		: "save";
	
	list = list_filenames(g_fs, directory_name, NULL, false);
	duk_push_array(ctx);
	iter = vector_enum(list);
	while (p_filename = vector_next(&iter)) {
		duk_push_string(ctx, lstr_cstr(*p_filename));
		duk_put_prop_index(ctx, -2, (duk_uarridx_t)iter.index);
		lstr_free(*p_filename);
	}
	vector_free(list);
	return 1;
}

static duk_ret_t
js_CreateDirectory(duk_context* ctx)
{
	const char* name;

	name = duk_require_path(ctx, 0, "save", true);
	if (!sfs_mkdir(g_fs, name, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "CreateDirectory(): unable to create directory `%s`", name);
	return 0;
}

static duk_ret_t
js_RemoveDirectory(duk_context* ctx)
{
	const char* name;

	name = duk_require_path(ctx, 0, "save", true);
	if (!sfs_rmdir(g_fs, name, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "CreateDirectory(): unable to remove directory `%s`", name);
	return 0;
}

static duk_ret_t
js_RemoveFile(duk_context* ctx)
{
	const char* filename;
	
	filename = duk_require_path(ctx, 0, "save", true);
	if (!sfs_unlink(g_fs, filename, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "RemoveFile(): unable to delete file `%s`", filename);
	return 0;
}

static duk_ret_t
js_Rename(duk_context* ctx)
{
	const char* name1; 
	const char* name2;

	name1 = duk_require_path(ctx, 0, "save", true);
	name2 = duk_require_path(ctx, 1, "save", true);
	if (!sfs_rename(g_fs, name1, name2, NULL))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "Rename(): unable to rename file `%s` to `%s`", name1, name2);
	return 0;
}

static duk_ret_t
js_new_FileStream(duk_context* ctx)
{
	// new FileStream(filename);
	// Arguments:
	//     filename: The SphereFS path of the file to open.
	//     mode:     A string specifying how to open the file, in the same format as
	//               would be passed to C fopen().

	sfs_file_t* file;
	const char* filename;
	const char* mode;

	filename = duk_require_path(ctx, 0, NULL, false);
	mode = duk_require_string(ctx, 1);
	file = sfs_fopen(g_fs, filename, NULL, mode);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to open file `%s` in mode `%s`",
			filename, mode);
	duk_push_sphere_obj(ctx, "FileStream", file);
	return 1;
}

static duk_ret_t
js_FileStream_finalize(duk_context* ctx)
{
	sfs_file_t* file;

	file = duk_require_sphere_obj(ctx, 0, "FileStream");
	if (file != NULL) sfs_fclose(file);
	return 0;
}

static duk_ret_t
js_FileStream_get_position(duk_context* ctx)
{
	sfs_file_t* file;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_push_number(ctx, sfs_ftell(file));
	return 1;
}

static duk_ret_t
js_FileStream_set_position(duk_context* ctx)
{
	sfs_file_t* file;
	long long   new_pos;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	new_pos = duk_require_number(ctx, 0);
	sfs_fseek(file, new_pos, SFS_SEEK_SET);
	return 0;
}

static duk_ret_t
js_FileStream_get_length(duk_context* ctx)
{
	sfs_file_t* file;
	long        file_pos;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	file_pos = sfs_ftell(file);
	sfs_fseek(file, 0, SEEK_END);
	duk_push_number(ctx, sfs_ftell(file));
	sfs_fseek(file, file_pos, SEEK_SET);
	return 1;
}

static duk_ret_t
js_FileStream_close(duk_context* ctx)
{
	sfs_file_t* file;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_push_pointer(ctx, NULL);
	duk_put_prop_string(ctx, -2, "\xFF" "udata");
	sfs_fclose(file);
	return 0;
}

static duk_ret_t
js_FileStream_read(duk_context* ctx)
{
	// FileStream:read([numBytes]);
	// Reads data from the stream, returning it in an ArrayBuffer.
	// Arguments:
	//     numBytes: Optional. The number of bytes to read. If not provided, the
	//               entire file is read.

	int          argc;
	void*        buffer;
	sfs_file_t*  file;
	int          num_bytes;
	long         pos;

	argc = duk_get_top(ctx);
	num_bytes = argc >= 1 ? duk_require_int(ctx, 0) : 0;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (argc < 1) {  // if no arguments, read entire file back to front
		pos = sfs_ftell(file);
		num_bytes = (sfs_fseek(file, 0, SEEK_END), sfs_ftell(file));
		sfs_fseek(file, 0, SEEK_SET);
	}
	if (num_bytes < 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "string length must be zero or greater");
	buffer = duk_push_fixed_buffer(ctx, num_bytes);
	num_bytes = (int)sfs_fread(buffer, 1, num_bytes, file);
	if (argc < 1)  // reset file position after whole-file read
		sfs_fseek(file, pos, SEEK_SET);
	duk_push_buffer_object(ctx, -1, 0, num_bytes, DUK_BUFOBJ_ARRAYBUFFER);
	return 1;
}

static duk_ret_t
js_FileStream_readDouble(duk_context* ctx)
{
	int         argc;
	uint8_t     data[8];
	sfs_file_t* file;
	bool        little_endian;
	double      value;

	int i;

	argc = duk_get_top(ctx);
	little_endian = argc >= 1 ? duk_require_boolean(ctx, 0) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (sfs_fread(data, 1, 8, file) != 8)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read double from file");
	if (little_endian == is_cpu_little_endian())
		memcpy(&value, data, 8);
	else {
		for (i = 0; i < 8; ++i)
			((uint8_t*)&value)[i] = data[7 - i];
	}
	duk_push_number(ctx, value);
	return 1;
}

static duk_ret_t
js_FileStream_readFloat(duk_context* ctx)
{
	int         argc;
	uint8_t     data[8];
	sfs_file_t* file;
	bool        little_endian;
	float       value;

	int i;

	argc = duk_get_top(ctx);
	little_endian = argc >= 1 ? duk_require_boolean(ctx, 0) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (sfs_fread(data, 1, 4, file) != 4)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read float from file");
	if (little_endian == is_cpu_little_endian())
		memcpy(&value, data, 4);
	else {
		for (i = 0; i < 4; ++i)
			((uint8_t*)&value)[i] = data[3 - i];
	}
	duk_push_number(ctx, value);
	return 1;
}

static duk_ret_t
js_FileStream_readInt(duk_context* ctx)
{
	int         argc;
	sfs_file_t* file;
	bool        little_endian;
	int         num_bytes;
	intmax_t    value;

	argc = duk_get_top(ctx);
	num_bytes = duk_require_int(ctx, 0);
	little_endian = argc >= 2 ? duk_require_boolean(ctx, 1) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (num_bytes < 1 || num_bytes > 6)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "int byte size must be in [1-6] range");
	if (!read_vsize_int(file, &value, num_bytes, little_endian))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read int from file");
	duk_push_number(ctx, (double)value);
	return 1;
}

static duk_ret_t
js_FileStream_readPString(duk_context* ctx)
{
	int          argc;
	void*        buffer;
	sfs_file_t*  file;
	intmax_t     length;
	bool         little_endian;
	int          uint_size;

	argc = duk_get_top(ctx);
	uint_size = duk_require_int(ctx, 0);
	little_endian = argc >= 2 ? duk_require_boolean(ctx, 1) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (uint_size < 1 || uint_size > 4)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "length bytes must be in [1-4] range (got: %d)", uint_size);
	if (!read_vsize_uint(file, &length, uint_size, little_endian))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read pstring from file");
	buffer = malloc((size_t)length);
	if (sfs_fread(buffer, 1, (size_t)length, file) != (size_t)length)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read pstring from file");
	duk_push_lstring(ctx, buffer, (size_t)length);
	free(buffer);
	return 1;
}

static duk_ret_t
js_FileStream_readString(duk_context* ctx)
{
	// FileStream:readString([numBytes]);
	// Reads data from the stream, returning it in an ArrayBuffer.
	// Arguments:
	//     numBytes: Optional. The number of bytes to read. If not provided, the
	//               entire file is read.

	int          argc;
	void*        buffer;
	sfs_file_t*  file;
	int          num_bytes;
	long         pos;

	argc = duk_get_top(ctx);
	num_bytes = argc >= 1 ? duk_require_int(ctx, 0) : 0;
	
	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (argc < 1) {  // if no arguments, read entire file back to front
		pos = sfs_ftell(file);
		num_bytes = (sfs_fseek(file, 0, SEEK_END), sfs_ftell(file));
		sfs_fseek(file, 0, SEEK_SET);
	}
	if (num_bytes < 0)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "string length must be zero or greater");
	buffer = malloc(num_bytes);
	num_bytes = (int)sfs_fread(buffer, 1, num_bytes, file);
	if (argc < 1)  // reset file position after whole-file read
		sfs_fseek(file, pos, SEEK_SET);
	duk_push_lstring(ctx, buffer, num_bytes);
	free(buffer);
	return 1;
}

static duk_ret_t
js_FileStream_readUInt(duk_context* ctx)
{
	int         argc;
	sfs_file_t* file;
	bool        little_endian;
	int         num_bytes;
	intmax_t     value;

	argc = duk_get_top(ctx);
	num_bytes = duk_require_int(ctx, 0);
	little_endian = argc >= 2 ? duk_require_boolean(ctx, 1) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (num_bytes < 1 || num_bytes > 6)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "uint byte size must be in [1-6] range");
	if (!read_vsize_uint(file, &value, num_bytes, little_endian))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to read uint from file");
	duk_push_number(ctx, (double)value);
	return 1;
}

static duk_ret_t
js_FileStream_write(duk_context* ctx)
{
	const void* data;
	sfs_file_t* file;
	duk_size_t  num_bytes;

	duk_require_stack_top(ctx, 1);
	data = duk_require_buffer_data(ctx, 0, &num_bytes);

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (sfs_fwrite(data, 1, num_bytes, file) != num_bytes)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write data to file");
	return 0;
}

static duk_ret_t
js_FileStream_writeDouble(duk_context* ctx)
{
	int         argc;
	uint8_t     data[8];
	sfs_file_t* file;
	bool        little_endian;
	double      value;

	int i;

	argc = duk_get_top(ctx);
	value = duk_require_number(ctx, 0);
	little_endian = argc >= 2 ? duk_require_boolean(ctx, 1) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (little_endian == is_cpu_little_endian())
		memcpy(data, &value, 8);
	else {
		for (i = 0; i < 8; ++i)
			data[i] = ((uint8_t*)&value)[7 - i];
	}
	if (sfs_fwrite(data, 1, 8, file) != 8)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write double to file");
	return 0;
}

static duk_ret_t
js_FileStream_writeFloat(duk_context* ctx)
{
	int         argc;
	uint8_t     data[4];
	sfs_file_t* file;
	bool        little_endian;
	float       value;

	int i;

	argc = duk_get_top(ctx);
	value = duk_require_number(ctx, 0);
	little_endian = argc >= 2 ? duk_require_boolean(ctx, 1) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (little_endian == is_cpu_little_endian())
		memcpy(data, &value, 4);
	else {
		for (i = 0; i < 4; ++i)
			data[i] = ((uint8_t*)&value)[3 - i];
	}
	if (sfs_fwrite(data, 1, 4, file) != 4)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write float to file");
	return 0;
}

static duk_ret_t
js_FileStream_writeInt(duk_context* ctx)
{
	int         argc;
	sfs_file_t* file;
	bool        little_endian;
	intmax_t    min_value;
	intmax_t    max_value;
	int         num_bytes;
	intmax_t    value;

	argc = duk_get_top(ctx);
	value = (intmax_t)duk_require_number(ctx, 0);
	num_bytes = duk_require_int(ctx, 1);
	little_endian = argc >= 3 ? duk_require_boolean(ctx, 2) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (num_bytes < 1 || num_bytes > 6)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "int byte size must be in [1-6] range");
	min_value = pow(-2, num_bytes * 8 - 1);
	max_value = pow(2, num_bytes * 8 - 1) - 1;
	if (value < min_value || value > max_value)
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "value is unrepresentable in `%d` bytes", num_bytes);
	if (!write_vsize_int(file, value, num_bytes, little_endian))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write int to file");
	return 0;
}

static duk_ret_t
js_FileStream_writePString(duk_context* ctx)
{
	int         argc;
	sfs_file_t* file;
	bool        little_endian;
	intmax_t    max_len;
	intmax_t    num_bytes;
	const char* string;
	duk_size_t  string_len;
	int         uint_size;

	argc = duk_get_top(ctx);
	string = duk_require_lstring(ctx, 0, &string_len);
	uint_size = duk_require_int(ctx, 1);
	little_endian = argc >= 3 ? duk_require_boolean(ctx, 2) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (uint_size < 1 || uint_size > 4)
		duk_error_ni(ctx, -1, DUK_ERR_RANGE_ERROR, "length bytes must be in [1-4] range", uint_size);
	max_len = pow(2, uint_size * 8) - 1;
	num_bytes = (intmax_t)string_len;
	if (num_bytes > max_len)
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "string is too long for `%d`-byte length", uint_size);
	if (!write_vsize_uint(file, num_bytes, uint_size, little_endian)
		|| sfs_fwrite(string, 1, string_len, file) != string_len)
	{
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write pstring to file");
	}
	return 0;
}

static duk_ret_t
js_FileStream_writeString(duk_context* ctx)
{
	const void* data;
	sfs_file_t* file;
	duk_size_t  num_bytes;

	duk_require_stack_top(ctx, 1);
	data = duk_get_lstring(ctx, 0, &num_bytes);

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (sfs_fwrite(data, 1, num_bytes, file) != num_bytes)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write string to file");
	return 0;
}

static duk_ret_t
js_FileStream_writeUInt(duk_context* ctx)
{
	int         argc;
	sfs_file_t* file;
	bool        little_endian;
	intmax_t    max_value;
	int         num_bytes;
	intmax_t    value;

	argc = duk_get_top(ctx);
	value = (intmax_t)duk_require_number(ctx, 0);
	num_bytes = duk_require_int(ctx, 1);
	little_endian = argc >= 3 ? duk_require_boolean(ctx, 2) : false;

	duk_push_this(ctx);
	file = duk_require_sphere_obj(ctx, -1, "FileStream");
	duk_pop(ctx);
	if (file == NULL)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "FileStream was closed");
	if (num_bytes < 1 || num_bytes > 6)
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "uint byte size must be in [1-6] range");
	max_value = pow(2, num_bytes * 8) - 1;
	if (value < 0 || value > max_value)
		duk_error_ni(ctx, -1, DUK_ERR_TYPE_ERROR, "value is unrepresentable in `%d` bytes", num_bytes);
	if (!write_vsize_uint(file, value, num_bytes, little_endian))
		duk_error_ni(ctx, -1, DUK_ERR_ERROR, "unable to write int to file");
	return 0;
}
