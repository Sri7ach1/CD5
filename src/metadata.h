#ifndef CD5_METADATA_H
#define CD5_METADATA_H

#include <stddef.h>

#define CD5_METADATA_MAX_TRACKS 99
#define CD5_METADATA_TEXT_MAX   256

typedef struct {
    int valid;

    char release_id[64];
    char album[CD5_METADATA_TEXT_MAX];
    char artist[CD5_METADATA_TEXT_MAX];

    int track_count;

    char tracks[CD5_METADATA_MAX_TRACKS]
               [CD5_METADATA_TEXT_MAX];
} cd5_metadata_t;

void cd5_metadata_clear(
    cd5_metadata_t *meta);

int cd5_metadata_parse_musicbrainz(
    const char *json,
    size_t json_size,
    int expected_track_count,
    cd5_metadata_t *meta);

#endif
