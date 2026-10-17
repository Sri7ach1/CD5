#include "lyrics.h"
#include "net.h"

#include <jsmn.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int token_eq(
    const char *json,
    const jsmntok_t *tok,
    const char *text)
{
    if (!json ||
        !tok ||
        !text ||
        tok->type != JSMN_STRING)
        return 0;

    size_t len =
        (size_t)(tok->end - tok->start);

    return
        strlen(text) == len &&
        memcmp(
            json + tok->start,
            text,
            len) == 0;
}


static int token_skip(
    const jsmntok_t *tokens,
    int index)
{
    const jsmntok_t *tok =
        &tokens[index];

    int next =
        index + 1;

    if (tok->type == JSMN_ARRAY) {

        for (int i = 0;
             i < tok->size;
             ++i) {

            next =
                token_skip(
                    tokens,
                    next);
        }

    } else if (tok->type == JSMN_OBJECT) {

        for (int i = 0;
             i < tok->size;
             ++i) {

            ++next; /* key */

            next =
                token_skip(
                    tokens,
                    next);
        }
    }

    return next;
}


static int object_get(
    const char *json,
    const jsmntok_t *tokens,
    int object,
    const char *key)
{
    if (tokens[object].type !=
        JSMN_OBJECT)
        return -1;

    int pos =
        object + 1;

    for (int i = 0;
         i < tokens[object].size;
         ++i) {

        int key_index =
            pos;

        int value_index =
            pos + 1;

        if (token_eq(
                json,
                &tokens[key_index],
                key))
            return value_index;

        pos =
            token_skip(
                tokens,
                value_index);
    }

    return -1;
}


static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';

    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';

    return -1;
}


static size_t utf8_emit(
    uint32_t cp,
    char *dst,
    size_t pos,
    size_t cap)
{
    if (cp <= 0x7f) {

        if (pos + 1 < cap)
            dst[pos++] =
                (char)cp;

    } else if (cp <= 0x7ff) {

        if (pos + 2 < cap) {

            dst[pos++] =
                (char)(
                    0xc0 |
                    (cp >> 6));

            dst[pos++] =
                (char)(
                    0x80 |
                    (cp & 0x3f));
        }

    } else if (cp <= 0xffff) {

        if (pos + 3 < cap) {

            dst[pos++] =
                (char)(
                    0xe0 |
                    (cp >> 12));

            dst[pos++] =
                (char)(
                    0x80 |
                    ((cp >> 6) & 0x3f));

            dst[pos++] =
                (char)(
                    0x80 |
                    (cp & 0x3f));
        }

    } else {

        if (pos + 4 < cap) {

            dst[pos++] =
                (char)(
                    0xf0 |
                    (cp >> 18));

            dst[pos++] =
                (char)(
                    0x80 |
                    ((cp >> 12) & 0x3f));

            dst[pos++] =
                (char)(
                    0x80 |
                    ((cp >> 6) & 0x3f));

            dst[pos++] =
                (char)(
                    0x80 |
                    (cp & 0x3f));
        }
    }

    return pos;
}


static char *json_string_dup(
    const char *json,
    const jsmntok_t *tok)
{
    if (!json ||
        !tok ||
        tok->type != JSMN_STRING)
        return NULL;

    size_t source_len =
        (size_t)(
            tok->end -
            tok->start);

    char *dst =
        (char *)malloc(
            source_len + 1);

    if (!dst)
        return NULL;

    size_t out = 0;

    for (int i = tok->start;
         i < tok->end;) {

        unsigned char c =
            (unsigned char)
            json[i++];

        if (c != '\\') {

            if (out + 1 <= source_len)
                dst[out++] =
                    (char)c;

            continue;
        }

        if (i >= tok->end)
            break;

        char esc =
            json[i++];

        switch (esc) {

        case '"':
        case '\\':
        case '/':

            dst[out++] = esc;
            break;

        case 'b':
            dst[out++] = '\b';
            break;

        case 'f':
            dst[out++] = '\f';
            break;

        case 'n':
            dst[out++] = '\n';
            break;

        case 'r':
            dst[out++] = '\r';
            break;

        case 't':
            dst[out++] = '\t';
            break;

        case 'u':

            if (i + 4 <=
                tok->end) {

                int a =
                    hex_value(
                        json[i]);

                int b =
                    hex_value(
                        json[i + 1]);

                int c2 =
                    hex_value(
                        json[i + 2]);

                int d =
                    hex_value(
                        json[i + 3]);

                if (a >= 0 &&
                    b >= 0 &&
                    c2 >= 0 &&
                    d >= 0) {

                    uint32_t cp =
                        ((uint32_t)a << 12) |
                        ((uint32_t)b << 8) |
                        ((uint32_t)c2 << 4) |
                        (uint32_t)d;

                    i += 4;

                    out =
                        utf8_emit(
                            cp,
                            dst,
                            out,
                            source_len + 1);
                }
            }

            break;

        default:

            dst[out++] = esc;
            break;
        }
    }

    dst[out] = '\0';

    return dst;
}


static void url_encode(
    const char *src,
    char *dst,
    size_t dst_size)
{
    static const char hex[] =
        "0123456789ABCDEF";

    if (!dst ||
        dst_size == 0)
        return;

    size_t out = 0;

    if (!src) {
        dst[0] = '\0';
        return;
    }

    while (*src &&
           out + 1 <
               dst_size) {

        unsigned char c =
            (unsigned char)
            *src++;

        int safe =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.' ||
            c == '~';

        if (safe) {

            dst[out++] =
                (char)c;

        } else {

            if (out + 3 >=
                dst_size)
                break;

            dst[out++] = '%';
            dst[out++] =
                hex[c >> 4];

            dst[out++] =
                hex[c & 15];
        }
    }

    dst[out] = '\0';
}


void cd5_lyrics_clear(
    cd5_lyrics_t *lyrics)
{
    if (lyrics)
        memset(
            lyrics,
            0,
            sizeof(*lyrics));
}


static int parse_timestamp(
    const char *p,
    uint32_t *out_ms,
    int *consumed)
{
    if (!p ||
        p[0] != '[')
        return 0;

    int i = 1;
    int minute = 0;
    int second = 0;
    int fraction = 0;
    int fraction_digits = 0;

    if (p[i] < '0' ||
        p[i] > '9')
        return 0;

    while (p[i] >= '0' &&
           p[i] <= '9') {

        minute =
            minute * 10 +
            (p[i] - '0');

        ++i;
    }

    if (p[i] != ':')
        return 0;

    ++i;

    if (p[i] < '0' ||
        p[i] > '9')
        return 0;

    while (p[i] >= '0' &&
           p[i] <= '9') {

        second =
            second * 10 +
            (p[i] - '0');

        ++i;
    }

    if (p[i] == '.' ||
        p[i] == ':') {

        ++i;

        while (p[i] >= '0' &&
               p[i] <= '9' &&
               fraction_digits < 3) {

            fraction =
                fraction * 10 +
                (p[i] - '0');

            ++fraction_digits;
            ++i;
        }

        while (p[i] >= '0' &&
               p[i] <= '9')
            ++i;
    }

    if (p[i] != ']')
        return 0;

    ++i;

    uint32_t frac_ms = 0;

    if (fraction_digits == 1)
        frac_ms =
            (uint32_t)fraction *
            100u;

    else if (fraction_digits == 2)
        frac_ms =
            (uint32_t)fraction *
            10u;

    else if (fraction_digits == 3)
        frac_ms =
            (uint32_t)fraction;

    *out_ms =
        (uint32_t)minute *
            60000u +
        (uint32_t)second *
            1000u +
        frac_ms;

    *consumed = i;

    return 1;
}


static int lyric_compare(
    const void *a,
    const void *b)
{
    const cd5_lyric_line_t *la =
        (const cd5_lyric_line_t *)a;

    const cd5_lyric_line_t *lb =
        (const cd5_lyric_line_t *)b;

    if (la->time_ms <
        lb->time_ms)
        return -1;

    if (la->time_ms >
        lb->time_ms)
        return 1;

    return 0;
}


static int parse_lrc(
    const char *text,
    cd5_lyrics_t *lyrics)
{
    if (!text ||
        !lyrics)
        return 0;

    const char *p = text;

    while (*p &&
           lyrics->count <
               CD5_LYRICS_MAX_LINES) {

        const char *line = p;

        while (*p &&
               *p != '\n' &&
               *p != '\r')
            ++p;

        size_t line_len =
            (size_t)(p - line);

        while (*p == '\n' ||
               *p == '\r')
            ++p;

        if (line_len == 0 ||
            line[0] != '[')
            continue;

        uint32_t timestamps[8];
        int timestamp_count = 0;

        size_t off = 0;

        while (off < line_len &&
               line[off] == '[' &&
               timestamp_count < 8) {

            uint32_t ms = 0;
            int used = 0;

            if (!parse_timestamp(
                    line + off,
                    &ms,
                    &used))
                break;

            timestamps[
                timestamp_count++] =
                    ms;

            off +=
                (size_t)used;
        }

        if (timestamp_count == 0)
            continue;

        while (off < line_len &&
               (line[off] == ' ' ||
                line[off] == '\t'))
            ++off;

        size_t text_len =
            line_len - off;

        if (text_len >=
            CD5_LYRICS_TEXT_MAX)
            text_len =
                CD5_LYRICS_TEXT_MAX -
                1;

        for (int t = 0;
             t < timestamp_count &&
             lyrics->count <
                 CD5_LYRICS_MAX_LINES;
             ++t) {

            cd5_lyric_line_t *dst =
                &lyrics->lines[
                    lyrics->count++];

            dst->time_ms =
                timestamps[t];

            memcpy(
                dst->text,
                line + off,
                text_len);

            dst->text[text_len] =
                '\0';
        }
    }

    if (lyrics->count <= 0)
        return 0;

    qsort(
        lyrics->lines,
        (size_t)lyrics->count,
        sizeof(lyrics->lines[0]),
        lyric_compare);

    lyrics->available = 1;
    lyrics->synced = 1;

    return 1;
}


static int parse_plain(
    const char *text,
    cd5_lyrics_t *lyrics)
{
    if (!text ||
        !lyrics)
        return 0;

    const char *p = text;

    while (*p &&
           lyrics->count <
               CD5_LYRICS_MAX_LINES) {

        const char *line = p;

        while (*p &&
               *p != '\n' &&
               *p != '\r')
            ++p;

        size_t len =
            (size_t)(p - line);

        while (*p == '\n' ||
               *p == '\r')
            ++p;

        if (len == 0)
            continue;

        if (len >=
            CD5_LYRICS_TEXT_MAX)
            len =
                CD5_LYRICS_TEXT_MAX -
                1;

        cd5_lyric_line_t *dst =
            &lyrics->lines[
                lyrics->count++];

        dst->time_ms = 0;

        memcpy(
            dst->text,
            line,
            len);

        dst->text[len] = '\0';
    }

    if (lyrics->count <= 0)
        return 0;

    lyrics->available = 1;
    lyrics->synced = 0;

    return 1;
}


int cd5_lyrics_fetch(
    const char *track,
    const char *artist,
    const char *album,
    int duration_seconds,
    cd5_lyrics_t *lyrics)
{
    if (!track ||
        !artist ||
        !lyrics)
        return -1;

    cd5_lyrics_clear(lyrics);

    char enc_track[768];
    char enc_artist[768];
    char enc_album[768];

    url_encode(
        track,
        enc_track,
        sizeof(enc_track));

    url_encode(
        artist,
        enc_artist,
        sizeof(enc_artist));

    url_encode(
        album ? album : "",
        enc_album,
        sizeof(enc_album));

    char url[3072];

    int n =
        snprintf(
            url,
            sizeof(url),
            "https://lrclib.net/api/get"
            "?track_name=%s"
            "&artist_name=%s"
            "&album_name=%s"
            "&duration=%d",
            enc_track,
            enc_artist,
            enc_album,
            duration_seconds);

    if (n <= 0 ||
        (size_t)n >= sizeof(url))
        return -2;

    char *body = NULL;
    size_t body_size = 0;

    int status =
        cd5_http_get(
            url,
            &body,
            &body_size);

    if (status == 404) {
        free(body);
        return 0;
    }

    if (status != 200 ||
        !body ||
        body_size == 0) {

        free(body);
        return -3;
    }

    unsigned int capacity = 256;
    jsmntok_t *tokens = NULL;
    int parsed = JSMN_ERROR_NOMEM;

    while (capacity <= 4096) {

        tokens =
            (jsmntok_t *)malloc(
                sizeof(jsmntok_t) *
                capacity);

        if (!tokens) {
            free(body);
            return -4;
        }

        jsmn_parser parser;
        jsmn_init(&parser);

        parsed =
            jsmn_parse(
                &parser,
                body,
                body_size,
                tokens,
                capacity);

        if (parsed !=
            JSMN_ERROR_NOMEM)
            break;

        free(tokens);
        tokens = NULL;

        capacity *= 2;
    }

    if (!tokens ||
        parsed <= 0 ||
        tokens[0].type !=
            JSMN_OBJECT) {

        free(tokens);
        free(body);

        return -5;
    }

    int instrumental =
        object_get(
            body,
            tokens,
            0,
            "instrumental");

    if (instrumental >= 0 &&
        tokens[instrumental].type ==
            JSMN_PRIMITIVE) {

        size_t len =
            (size_t)(
                tokens[instrumental].end -
                tokens[instrumental].start);

        if (len == 4 &&
            memcmp(
                body +
                    tokens[instrumental].start,
                "true",
                4) == 0) {

            lyrics->instrumental = 1;

            free(tokens);
            free(body);

            return 2;
        }
    }

    int synced =
        object_get(
            body,
            tokens,
            0,
            "syncedLyrics");

    if (synced >= 0 &&
        tokens[synced].type ==
            JSMN_STRING) {

        char *text =
            json_string_dup(
                body,
                &tokens[synced]);

        if (text) {

            int ok =
                parse_lrc(
                    text,
                    lyrics);

            free(text);

            if (ok) {

                free(tokens);
                free(body);

                return 1;
            }
        }
    }

    int plain =
        object_get(
            body,
            tokens,
            0,
            "plainLyrics");

    if (plain >= 0 &&
        tokens[plain].type ==
            JSMN_STRING) {

        char *text =
            json_string_dup(
                body,
                &tokens[plain]);

        if (text) {

            int ok =
                parse_plain(
                    text,
                    lyrics);

            free(text);

            if (ok) {

                free(tokens);
                free(body);

                return 1;
            }
        }
    }

    free(tokens);
    free(body);

    return 0;
}
