/* Copyright (c) 2026 Sri7ach1 — SPDX-License-Identifier: GPL-3.0-or-later */
#include "toc.h"
#include <string.h>
#include <limits.h>
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static int length(const uint8_t *p, size_t n, size_t stride, size_t *end) {
    if (!p || n < 4) return 0;
    *end = ((size_t)p[0]<<8 | p[1]) + 2;
    return *end >= 4 && *end <= n && (*end - 4) % stride == 0;
}
static int msf(const uint8_t *p, uint32_t *lba) {
    if (p[1] >= 60 || p[2] >= 75) return 0;
    uint32_t frames = ((uint32_t)p[0]*60 + p[1])*75 + p[2];
    if (frames < 150) return 0;
    *lba = frames - 150;
    return 1;
}
int cd5_toc_parse(const uint8_t *basic, size_t basic_size,
                  const uint8_t *full, size_t full_size, cd5_toc_t *out) {
    size_t end;
    cd5_toc_t result;
    memset(&result, 0, sizeof(result));
    if (!out || !length(basic, basic_size, 8, &end)) return 0;
    unsigned first = basic[2], last = basic[3];
    if (!first || first > last || last > 99) return 0;
    uint32_t starts[100] = {0}, leadout = 0;
    uint8_t controls[100] = {0}, seen[100] = {0};
    int have_leadout = 0, mixed = 0;
    for (size_t i = 4; i < end; i += 8) {
        unsigned t = basic[i+2];
        if ((basic[i+1] >> 4) != 1) return 0;
        uint32_t lba = u32(basic+i+4);
        if (lba > INT32_MAX - 150u) return 0;
        if (t == 0xaa) {
            if (have_leadout++) return 0;
            leadout = lba;
        } else {
            if (t < first || t > last || seen[t]++) return 0;
            starts[t] = lba;
            controls[t] = basic[i+1] & 15;
            if (controls[t] & 4) mixed = 1;
        }
    }
    if (!have_leadout) return 0;
    for (unsigned t = first; t <= last; ++t)
        if (!seen[t] || (t > first && starts[t] <= starts[t-1]) || starts[t] >= leadout) return 0;

    uint8_t session[100] = {0};
    uint32_t session_end[100] = {0};
    if (full_size) {
        if (!length(full, full_size, 11, &end) || !full[2] || full[2] > full[3] || full[3] > 99) return 0;
        for (size_t i = 4; i < end; i += 11) {
            unsigned sn = full[i], point = full[i+3];
            if (sn < full[2] || sn > full[3]) return 0;
            if ((full[i+1] >> 4) != 1) continue;
            uint32_t lba;
            if (point == 0xa2) {
                if (!msf(full+i+8, &lba) || (session_end[sn] && session_end[sn] != lba)) return 0;
                session_end[sn] = lba;
            } else if (point >= 1 && point <= 99) {
                if (point < first || point > last || !msf(full+i+8, &lba) || lba != starts[point] ||
                    (full[i+1] & 15) != controls[point] || (session[point] && session[point] != sn)) return 0;
                session[point] = sn;
            }
        }
        if (session_end[full[3]] != leadout) return 0;
        for (unsigned t = first; t <= last; ++t) {
            if (!session[t] || !session_end[session[t]] || session_end[session[t]] <= starts[t] ||
                session_end[session[t]] > leadout) return 0;
            if (t < last && (session[t] > session[t+1] ||
                (session[t] == session[t+1] && session_end[session[t]] <= starts[t+1]) ||
                (session[t] != session[t+1] && session_end[session[t]] > starts[t+1]))) return 0;
        }
    } else if (mixed) {
        return 0;
    }
    unsigned last_audio = 0;
    for (unsigned t = first; t <= last; ++t) {
        if (controls[t] & 4) continue;
        audio_track_t *track = &result.audio[result.count++];
        track->number = t;
        track->start_lba = starts[t];
        track->end_lba = t < last ? starts[t+1] : leadout;
        if (full_size && session_end[session[t]] < track->end_lba)
            track->end_lba = session_end[session[t]];
        last_audio = t;
    }
    if (!result.count) return 0;
    /* Retain leading data offsets for MusicBrainz numbering; exclude trailing
     * data sessions. The audio session's actual A2 bounds its final track. */
    result.disc.first_track = first;
    result.disc.last_track = last_audio;
    result.disc.frame_offset[0] = result.audio[result.count-1].end_lba + 150;
    for (unsigned t = first; t <= last_audio; ++t)
        result.disc.frame_offset[t] = starts[t] + 150;
    *out = result;
    return 1;
}
