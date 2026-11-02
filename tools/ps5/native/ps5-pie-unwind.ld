/*
 * The PS5 title's layout: ps5-native-app-boilerplate's (tooling/native/ps5-pie.ld, found on the
 * linker's -L path), with the bounds of the unwind tables, which the SDK's libunwind finds
 * through these symbols (C++ exceptions). The same definitions as PS5_Vulkan's
 * tooling/psbc/ps5-pie-unwind.ld (Mihawk, GPL-3.0-or-later).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

INCLUDE ps5-pie.ld

PROVIDE(__eh_frame_start = ADDR(.eh_frame));
PROVIDE(__eh_frame_end = ADDR(.eh_frame) + SIZEOF(.eh_frame));
PROVIDE(__eh_frame_hdr_start = ADDR(.eh_frame_hdr));
PROVIDE(__eh_frame_hdr_end = ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr));
