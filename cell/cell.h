#ifndef CELL__CELL_H__INCLUDED
#define CELL__CELL_H__INCLUDED

#ifdef _MSC_VER
#define _CRT_NONSTDC_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include "duktape.h"
#include "lstring.h"
#include "path.h"
#include "spk_writer.h"
#include "utility.h"
#include "vector.h"

#include "posix.h"

#define CELL_VERSION "v2.0-WIP"

extern bool g_want_dry_run;
extern bool g_want_source_map;

extern void print_v (const char* fmt, ...);

#endif // CELL__CELL_H__INCLUDED