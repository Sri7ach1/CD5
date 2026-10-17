#ifndef CD5_DISCID_H
#define CD5_DISCID_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t first_track;
    uint8_t last_track;

    /*
     * MusicBrainz usa offsets absolutos en frames:
     * LBA del CD + 150.
     *
     * frame_offset[0] = lead-out
     * frame_offset[1..99] = pistas por número real.
     */
    uint32_t frame_offset[100];
} cd5_disc_toc_t;

int cd5_discid_generate(
    const cd5_disc_toc_t *toc,
    char out[29]);

int cd5_discid_make_toc_query(
    const cd5_disc_toc_t *toc,
    char *out,
    size_t out_size);

#endif
