/* In place of websrv's src/fs.h (which declares its web server's handlers too) for hbldr.c.

   SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

uint8_t* fs_readfile(const char* path, size_t* size);
