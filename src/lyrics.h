#ifndef CD5_LYRICS_H
#define CD5_LYRICS_H

#include <stdint.h>

#define CD5_LYRICS_MAX_LINES 256
#define CD5_LYRICS_TEXT_MAX  256

typedef struct {
    uint32_t time_ms;
    char text[CD5_LYRICS_TEXT_MAX];
} cd5_lyric_line_t;

typedef struct {
    int available;
    int synced;
    int instrumental;
    int count;

    cd5_lyric_line_t lines[CD5_LYRICS_MAX_LINES];
} cd5_lyrics_t;

void cd5_lyrics_clear(
    cd5_lyrics_t *lyrics);

/*
 * return:
 *   1 = letras encontradas
 *   2 = instrumental
 *   0 = no encontradas
 *  <0 = error de red / parseo
 */
int cd5_lyrics_fetch(
    const char *track,
    const char *artist,
    const char *album,
    int duration_seconds,
    cd5_lyrics_t *lyrics);

#endif
