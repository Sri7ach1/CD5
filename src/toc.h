/* Copyright (c) 2026 Sri7ach1 — SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CD5_TOC_H
#define CD5_TOC_H
#include <stddef.h>
#include <stdint.h>
#include "discid.h"
#define MAX_AUDIO_TRACKS 99
typedef struct {
    uint8_t number;
    uint32_t start_lba, end_lba;
} audio_track_t;
typedef struct {
    audio_track_t audio[MAX_AUDIO_TRACKS];
    int count;
    cd5_disc_toc_t disc;
} cd5_toc_t;
/* Format 0 (LBA) plus optional format 2 (binary MSF) READ TOC responses.
 * Mixed discs require a valid full TOC: never guess a session gap. */
int cd5_toc_parse(const uint8_t *basic, size_t basic_size,
                  const uint8_t *full, size_t full_size, cd5_toc_t *out);
#endif
