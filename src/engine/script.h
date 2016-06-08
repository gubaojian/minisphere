#ifndef MINISPHERE__SCRIPT_H__INCLUDED
#define MINISPHERE__SCRIPT_H__INCLUDED

typedef struct script script_t;

void             initialize_scripts (void);
void             shutdown_scripts   (void);
script_t*        compile_script     (const lstring_t* script, const char* fmt_name, ...);
script_t*        ref_script         (script_t* script);
void             free_script        (script_t* script);
void             run_script         (script_t* script, bool allow_reentry);

script_t* duk_require_sphere_script (duk_context* ctx, duk_idx_t index, const char* name);

#endif // MINISPHERE__SCRIPT_H__INCLUDED
