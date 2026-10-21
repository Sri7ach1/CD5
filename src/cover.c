#include "cover.h"
#include "net.h"

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>


extern const unsigned char cd5_icon0_png[];
extern const size_t cd5_icon0_png_len;


void cd5_cover_clear(
    cd5_cover_t *cover)
{
    if (!cover)
        return;

    if (cover->rgba)
        stbi_image_free(cover->rgba);

    memset(
        cover,
        0,
        sizeof(*cover));
}


static int cd5_cover_decode(
    cd5_cover_t *cover,
    const unsigned char *data,
    size_t size,
    int network)
{
    if (!cover ||
        !data ||
        size == 0)
        return 0;

    int width = 0;
    int height = 0;
    int channels = 0;

    if (size > INT_MAX || !stbi_info_from_memory(data, (int)size, &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return 0;
    unsigned char *rgba =
        stbi_load_from_memory(
            data,
            (int)size,
            &width,
            &height,
            &channels,
            4);

    if (!rgba ||
        width <= 0 ||
        height <= 0) {

        if (rgba)
            stbi_image_free(rgba);

        return 0;
    }

    cd5_cover_clear(cover);

    cover->rgba = rgba;
    cover->width = width;
    cover->height = height;
    cover->valid = 1;
    cover->network = network;

    return 1;
}


int cd5_cover_load_fallback(
    cd5_cover_t *cover)
{
    if (!cover) return 0;
    int status = cover->status;
    int ok = cd5_cover_decode(cover, cd5_icon0_png, cd5_icon0_png_len, 0);
    cover->status = status;
    return ok;
}


int cd5_cover_fetch_release(
    const char *release_id,
    cd5_cover_t *cover)
{
    if (!release_id ||
        !release_id[0] ||
        !cover)
        return 0;

    char url[256];

    int n =
        snprintf(
            url,
            sizeof(url),
            "https://coverartarchive.org/release/%s/front-500",
            release_id);

    if (n <= 0 ||
        (size_t)n >= sizeof(url))
        return 0;

    char *body = NULL;
    size_t body_size = 0;

    int status =
        cd5_http_get(
            url,
            &body,
            &body_size);

    if (status != 200 ||
        !body ||
        body_size == 0) {

        cover->status = status == 404 ? 2 : status == 200 ? -2 : -1;
        free(body);
        return 0;
    }

    int ok =
        cd5_cover_decode(
            cover,
            (const unsigned char *)body,
            body_size,
            1);

    cover->status = ok ? 1 : -2;
    free(body);

    return ok;
}
