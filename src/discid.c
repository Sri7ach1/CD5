#include "discid.h"

#include <stdio.h>
#include <string.h>

#include <mbedtls/sha1.h>


static void mb_base64(
    const unsigned char *src,
    size_t len,
    char *dst)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789._";

    size_t i = 0;
    size_t o = 0;

    while (i + 3 <= len) {

        uint32_t v =
            ((uint32_t)src[i] << 16) |
            ((uint32_t)src[i + 1] << 8) |
            ((uint32_t)src[i + 2]);

        dst[o++] = alphabet[(v >> 18) & 63];
        dst[o++] = alphabet[(v >> 12) & 63];
        dst[o++] = alphabet[(v >> 6) & 63];
        dst[o++] = alphabet[v & 63];

        i += 3;
    }

    size_t remain = len - i;

    if (remain == 1) {

        uint32_t v =
            (uint32_t)src[i] << 16;

        dst[o++] = alphabet[(v >> 18) & 63];
        dst[o++] = alphabet[(v >> 12) & 63];

        /*
         * MusicBrainz sustituye '=' por '-'.
         */
        dst[o++] = '-';
        dst[o++] = '-';

    } else if (remain == 2) {

        uint32_t v =
            ((uint32_t)src[i] << 16) |
            ((uint32_t)src[i + 1] << 8);

        dst[o++] = alphabet[(v >> 18) & 63];
        dst[o++] = alphabet[(v >> 12) & 63];
        dst[o++] = alphabet[(v >> 6) & 63];
        dst[o++] = '-';
    }

    dst[o] = '\0';
}


int cd5_discid_generate(
    const cd5_disc_toc_t *toc,
    char out[29])
{
    if (!toc || !out)
        return 0;

    if (toc->first_track < 1 ||
        toc->first_track > 99 ||
        toc->last_track < toc->first_track ||
        toc->last_track > 99 ||
        toc->frame_offset[0] == 0)
        return 0;

    /*
     * 2 + 2 + (100 * 8) = 804 caracteres.
     */
    char hash_input[805];
    size_t pos = 0;

    int n =
        snprintf(
            hash_input + pos,
            sizeof(hash_input) - pos,
            "%02X%02X",
            toc->first_track,
            toc->last_track);

    if (n != 4)
        return 0;

    pos += 4;

    /*
     * Índice 0 = lead-out.
     * 1..99 = offsets por número de pista.
     */
    for (int i = 0; i < 100; ++i) {

        n =
            snprintf(
                hash_input + pos,
                sizeof(hash_input) - pos,
                "%08X",
                toc->frame_offset[i]);

        if (n != 8)
            return 0;

        pos += 8;
    }

    unsigned char digest[20];

    if (mbedtls_sha1(
            (const unsigned char *)hash_input,
            pos,
            digest) != 0)
        return 0;

    mb_base64(
        digest,
        sizeof(digest),
        out);

    return strlen(out) == 28;
}


int cd5_discid_make_toc_query(
    const cd5_disc_toc_t *toc,
    char *out,
    size_t out_size)
{
    if (!toc ||
        !out ||
        out_size == 0)
        return 0;

    int track_count =
        (int)toc->last_track -
        (int)toc->first_track + 1;

    int n =
        snprintf(
            out,
            out_size,
            "%u+%d+%u",
            (unsigned)toc->first_track,
            track_count,
            (unsigned)toc->frame_offset[0]);

    if (n < 0 ||
        (size_t)n >= out_size)
        return 0;

    size_t pos = (size_t)n;

    for (int track = toc->first_track;
         track <= toc->last_track;
         ++track) {

        n =
            snprintf(
                out + pos,
                out_size - pos,
                "+%u",
                (unsigned)toc->frame_offset[track]);

        if (n < 0 ||
            (size_t)n >= out_size - pos)
            return 0;

        pos += (size_t)n;
    }

    return 1;
}
