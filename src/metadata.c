#include "metadata.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#include <jsmn.h>


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
            dst[pos++] = (char)cp;

    } else if (cp <= 0x7ff) {

        if (pos + 2 < cap) {
            dst[pos++] =
                (char)(0xc0 | (cp >> 6));

            dst[pos++] =
                (char)(0x80 | (cp & 0x3f));
        }

    } else {

        if (pos + 3 < cap) {
            dst[pos++] =
                (char)(0xe0 | (cp >> 12));

            dst[pos++] =
                (char)(0x80 |
                    ((cp >> 6) & 0x3f));

            dst[pos++] =
                (char)(0x80 |
                    (cp & 0x3f));
        }
    }

    return pos;
}


/*
 * Copia un string JSON eliminando escapes.
 *
 * MusicBrainz devuelve UTF-8, pero además pueden aparecer
 * escapes JSON normales y \uXXXX.
 */
static int token_copy_string(
    const char *json,
    const jsmntok_t *tok,
    char *dst,
    size_t dst_size)
{
    if (!json ||
        !tok ||
        !dst ||
        dst_size == 0 ||
        tok->type != JSMN_STRING)
        return 0;

    size_t out = 0;

    for (int i = tok->start;
         i < tok->end;) {

        unsigned char c =
            (unsigned char)json[i++];

        if (c != '\\') {

            if (out + 1 < dst_size)
                dst[out++] = (char)c;

            continue;
        }

        if (i >= tok->end)
            break;

        char esc = json[i++];

        switch (esc) {

        case '"':
        case '\\':
        case '/':
            if (out + 1 < dst_size)
                dst[out++] = esc;
            break;

        case 'b':
            if (out + 1 < dst_size)
                dst[out++] = '\b';
            break;

        case 'f':
            if (out + 1 < dst_size)
                dst[out++] = '\f';
            break;

        case 'n':
            if (out + 1 < dst_size)
                dst[out++] = '\n';
            break;

        case 'r':
            if (out + 1 < dst_size)
                dst[out++] = '\r';
            break;

        case 't':
            if (out + 1 < dst_size)
                dst[out++] = '\t';
            break;

        case 'u':
            if (i + 4 <= tok->end) {

                int h0 = hex_value(json[i + 0]);
                int h1 = hex_value(json[i + 1]);
                int h2 = hex_value(json[i + 2]);
                int h3 = hex_value(json[i + 3]);

                if (h0 >= 0 &&
                    h1 >= 0 &&
                    h2 >= 0 &&
                    h3 >= 0) {

                    uint32_t cp =
                        ((uint32_t)h0 << 12) |
                        ((uint32_t)h1 << 8) |
                        ((uint32_t)h2 << 4) |
                        (uint32_t)h3;

                    i += 4;

                    out =
                        utf8_emit(
                            cp,
                            dst,
                            out,
                            dst_size);
                }
            }
            break;

        default:
            if (out + 1 < dst_size)
                dst[out++] = esc;
            break;
        }
    }

    dst[out] = '\0';

    return 1;
}


static int token_to_int(
    const char *json,
    const jsmntok_t *tok)
{
    if (!json || !tok)
        return -1;

    int value = 0;

    for (int i = tok->start;
         i < tok->end;
         ++i) {

        char c = json[i];

        if (c < '0' || c > '9')
            return -1;

        value =
            value * 10 +
            (c - '0');
    }

    return value;
}


/*
 * Devuelve el índice del primer token posterior
 * al árbol cuyo root es tokens[index].
 */
static int token_skip(
    const jsmntok_t *tokens,
    int index)
{
    const jsmntok_t *tok =
        &tokens[index];

    int next = index + 1;

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

            /*
             * key
             */
            ++next;

            /*
             * value
             */
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
    int object_index,
    const char *key)
{
    if (tokens[object_index].type !=
        JSMN_OBJECT)
        return -1;

    int pos =
        object_index + 1;

    for (int i = 0;
         i < tokens[object_index].size;
         ++i) {

        int key_index = pos;
        int value_index = pos + 1;

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


static int array_get(
    const jsmntok_t *tokens,
    int array_index,
    int element)
{
    if (tokens[array_index].type !=
        JSMN_ARRAY ||
        element < 0 ||
        element >= tokens[array_index].size)
        return -1;

    int pos =
        array_index + 1;

    for (int i = 0;
         i < element;
         ++i) {

        pos =
            token_skip(
                tokens,
                pos);
    }

    return pos;
}


void cd5_metadata_clear(
    cd5_metadata_t *meta)
{
    if (meta)
        memset(
            meta,
            0,
            sizeof(*meta));
}


static int parse_medium(
    const char *json,
    const jsmntok_t *tokens,
    int medium_index,
    cd5_metadata_t *meta)
{
    int tracks_index =
        object_get(
            json,
            tokens,
            medium_index,
            "tracks");

    if (tracks_index < 0 ||
        tokens[tracks_index].type !=
            JSMN_ARRAY)
        return 0;

    int count =
        tokens[tracks_index].size;

    if (count <= 0)
        return 0;

    if (count >
        CD5_METADATA_MAX_TRACKS)
        count =
            CD5_METADATA_MAX_TRACKS;

    meta->track_count = count;

    for (int i = 0;
         i < count;
         ++i) {

        int track_index =
            array_get(
                tokens,
                tracks_index,
                i);

        if (track_index < 0)
            continue;

        /*
         * MusicBrainz suele proporcionar title tanto
         * en track como dentro de recording.
         */
        int title_index =
            object_get(
                json,
                tokens,
                track_index,
                "title");

        if (title_index < 0) {

            int recording_index =
                object_get(
                    json,
                    tokens,
                    track_index,
                    "recording");

            if (recording_index >= 0) {

                title_index =
                    object_get(
                        json,
                        tokens,
                        recording_index,
                        "title");
            }
        }

        if (title_index >= 0) {

            token_copy_string(
                json,
                &tokens[title_index],
                meta->tracks[i],
                sizeof(meta->tracks[i]));
        }
    }

    return 1;
}


static int parse_release(
    const char *json,
    const jsmntok_t *tokens,
    int release_index,
    int expected_track_count,
    cd5_metadata_t *meta)
{
    int index;

    index =
        object_get(
            json,
            tokens,
            release_index,
            "id");

    if (index >= 0) {

        token_copy_string(
            json,
            &tokens[index],
            meta->release_id,
            sizeof(meta->release_id));
    }

    index =
        object_get(
            json,
            tokens,
            release_index,
            "title");

    if (index >= 0) {

        token_copy_string(
            json,
            &tokens[index],
            meta->album,
            sizeof(meta->album));
    }

    /*
     * Primer artist-credit.
     * Para la primera integración es suficiente y evita
     * inventar separadores de créditos complejos.
     */
    index =
        object_get(
            json,
            tokens,
            release_index,
            "artist-credit");

    if (index >= 0 &&
        tokens[index].type ==
            JSMN_ARRAY &&
        tokens[index].size > 0) {

        int credit =
            array_get(
                tokens,
                index,
                0);

        if (credit >= 0) {

            int name =
                object_get(
                    json,
                    tokens,
                    credit,
                    "name");

            if (name >= 0) {

                token_copy_string(
                    json,
                    &tokens[name],
                    meta->artist,
                    sizeof(meta->artist));
            }
        }
    }

    int media =
        object_get(
            json,
            tokens,
            release_index,
            "media");

    if (media < 0 ||
        tokens[media].type !=
            JSMN_ARRAY)
        return 0;

    int fallback_medium = -1;

    for (int i = 0;
         i < tokens[media].size;
         ++i) {

        int medium =
            array_get(
                tokens,
                media,
                i);

        if (medium < 0)
            continue;

        if (fallback_medium < 0)
            fallback_medium = medium;

        int track_count_index =
            object_get(
                json,
                tokens,
                medium,
                "track-count");

        if (track_count_index < 0)
            continue;

        int medium_tracks =
            token_to_int(
                json,
                &tokens[track_count_index]);

        if (expected_track_count > 0 &&
            medium_tracks ==
                expected_track_count) {

            return
                parse_medium(
                    json,
                    tokens,
                    medium,
                    meta);
        }
    }

    /*
     * Si MusicBrainz no da un track-count coincidente,
     * usamos el primer medium como fallback.
     */
    if (fallback_medium >= 0) {

        return
            parse_medium(
                json,
                tokens,
                fallback_medium,
                meta);
    }

    return 0;
}


int cd5_metadata_parse_musicbrainz(
    const char *json,
    size_t json_size,
    int expected_track_count,
    cd5_metadata_t *meta)
{
    if (!json ||
        json_size == 0 ||
        !meta)
        return 0;

    cd5_metadata_clear(meta);

    unsigned int token_capacity =
        4096;

    jsmntok_t *tokens = NULL;
    int parsed = JSMN_ERROR_NOMEM;

    while (token_capacity <= 32768) {

        tokens =
            (jsmntok_t *)malloc(
                sizeof(jsmntok_t) *
                token_capacity);

        if (!tokens)
            return 0;

        jsmn_parser parser;
        jsmn_init(&parser);

        parsed =
            jsmn_parse(
                &parser,
                json,
                json_size,
                tokens,
                token_capacity);

        if (parsed !=
            JSMN_ERROR_NOMEM)
            break;

        free(tokens);
        tokens = NULL;

        token_capacity *= 2;
    }

    if (!tokens ||
        parsed <= 0 ||
        tokens[0].type !=
            JSMN_OBJECT) {

        free(tokens);
        return 0;
    }

    int releases =
        object_get(
            json,
            tokens,
            0,
            "releases");

    if (releases < 0 ||
        tokens[releases].type !=
            JSMN_ARRAY ||
        tokens[releases].size <= 0) {

        free(tokens);
        return 0;
    }

    /*
     * Primera versión:
     * intentamos releases en orden hasta encontrar una que
     * contenga un medium utilizable.
     */
    for (int i = 0;
         i < tokens[releases].size;
         ++i) {

        int release =
            array_get(
                tokens,
                releases,
                i);

        if (release < 0)
            continue;

        cd5_metadata_t candidate;
        cd5_metadata_clear(
            &candidate);

        if (parse_release(
                json,
                tokens,
                release,
                expected_track_count,
                &candidate)) {

            candidate.valid = 1;

            *meta = candidate;

            free(tokens);
            return 1;
        }
    }

    free(tokens);
    return 0;
}
