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

#define CELL_VERSION "2.0.0"

#include "fs.h"

extern duk_context* g_duk;

#endif // CELL__CELL_H__INCLUDED
