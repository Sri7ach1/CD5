/* Copyright (c) 2026 Sri7ach1 — SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CD5_SCSI_POLICY_H
#define CD5_SCSI_POLICY_H
#include <stddef.h>
#include <stdint.h>
typedef enum { CD5_IO_OK, CD5_IO_RETRY, CD5_IO_MEDIA, CD5_IO_GONE, CD5_IO_STOP } cd5_io_kind;
typedef struct {
    cd5_io_kind kind;
    int os_error;
    unsigned cam_status;
    uint8_t scsi_status, key, asc, ascq;
} cd5_io_result;
/* SPC fixed and descriptor sense; only current, complete responses are used. */
static inline cd5_io_kind cd5_sense_policy(const uint8_t *s, size_t n,
                                          cd5_io_result *r) {
    if (!s || n < 8) return CD5_IO_STOP;
    unsigned response = s[0] & 0x7f;
    if (response == 0x70 && n >= 14 && s[7] >= 6) {
        r->key = s[2] & 15; r->asc = s[12]; r->ascq = s[13];
    } else if (response == 0x72) {
        r->key = s[1] & 15; r->asc = s[2]; r->ascq = s[3];
    } else return CD5_IO_STOP;
    /* Never resume an old LBA after UNIT ATTENTION or missing media. */
    if (r->key == 6 || (r->key == 2 && r->asc == 0x3a)) return CD5_IO_MEDIA;
    if (r->key == 0x0b) return CD5_IO_RETRY;
    return CD5_IO_STOP;
}
#endif
