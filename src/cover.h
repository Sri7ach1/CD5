#ifndef CD5_COVER_H
#define CD5_COVER_H

typedef struct {
    unsigned char *rgba;
    int width;
    int height;
    int valid;
    int network;
} cd5_cover_t;

void cd5_cover_clear(cd5_cover_t *cover);

int cd5_cover_load_fallback(
    cd5_cover_t *cover);

int cd5_cover_fetch_release(
    const char *release_id,
    cd5_cover_t *cover);

#endif
