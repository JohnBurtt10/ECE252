#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include "./starter/png_util/lab_png.h"

int is_file_png(char* filename);
int process_png(const char* filename);
