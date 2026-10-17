#include "net.h"

#include <curl/curl.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/*
 * Generados durante scripts/build.sh a partir del ca-bundle.crt
 * incluido en el PS5 Payload SDK.
 *
 * El bundle queda embebido dentro de eboot.bin.
 */
extern const unsigned char cd5_ca_bundle[];
extern const size_t cd5_ca_bundle_len;


#define CD5_USER_AGENT \
    "CD5/0.1 (PS5 Audio CD Player)"


typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    int failed;
} cd5_http_buffer_t;


static int g_curl_ready = 0;


static size_t cd5_curl_write(
    char *ptr,
    size_t size,
    size_t nmemb,
    void *userdata)
{
    cd5_http_buffer_t *buffer =
        (cd5_http_buffer_t *)userdata;

    if (!buffer || buffer->failed)
        return 0;

    if (size != 0 &&
        nmemb > SIZE_MAX / size) {

        buffer->failed = 1;
        return 0;
    }

    size_t bytes =
        size * nmemb;

    if (bytes == 0)
        return 0;

    if (buffer->size >
        SIZE_MAX - bytes - 1) {

        buffer->failed = 1;
        return 0;
    }

    size_t required =
        buffer->size + bytes + 1;

    if (required > buffer->capacity) {

        size_t new_capacity =
            buffer->capacity
                ? buffer->capacity
                : 16 * 1024;

        while (new_capacity < required) {

            if (new_capacity >
                SIZE_MAX / 2) {

                new_capacity =
                    required;

                break;
            }

            new_capacity *= 2;
        }

        char *new_data =
            (char *)realloc(
                buffer->data,
                new_capacity);

        if (!new_data) {
            buffer->failed = 1;
            return 0;
        }

        buffer->data =
            new_data;

        buffer->capacity =
            new_capacity;
    }

    memcpy(
        buffer->data + buffer->size,
        ptr,
        bytes);

    buffer->size += bytes;
    buffer->data[buffer->size] = '\0';

    return bytes;
}


int cd5_net_init(void)
{
    if (g_curl_ready)
        return 0;

    CURLcode result =
        curl_global_init(
            CURL_GLOBAL_DEFAULT);

    if (result != CURLE_OK)
        return -(1000 + (int)result);

    g_curl_ready = 1;

    return 0;
}


void cd5_net_fini(void)
{
    if (!g_curl_ready)
        return;

    curl_global_cleanup();

    g_curl_ready = 0;
}


int cd5_http_get(
    const char *url,
    char **out_data,
    size_t *out_size)
{
    if (!url ||
        !out_data ||
        !out_size ||
        !g_curl_ready)
        return -1;

    *out_data = NULL;
    *out_size = 0;

    CURL *curl =
        curl_easy_init();

    if (!curl)
        return -2;

    cd5_http_buffer_t buffer;
    memset(
        &buffer,
        0,
        sizeof(buffer));

    struct curl_blob ca_blob;

    ca_blob.data =
        (void *)cd5_ca_bundle;

    ca_blob.len =
        cd5_ca_bundle_len;

    ca_blob.flags =
        CURL_BLOB_NOCOPY;


#define CD5_CURL_SETOPT(option, value)       \
    do {                                     \
        CURLcode _r =                        \
            curl_easy_setopt(                \
                curl,                        \
                (option),                    \
                (value));                    \
        if (_r != CURLE_OK) {                \
            curl_easy_cleanup(curl);         \
            free(buffer.data);               \
            return -(2000 + (int)_r);        \
        }                                    \
    } while (0)


    CD5_CURL_SETOPT(
        CURLOPT_URL,
        url);

    CD5_CURL_SETOPT(
        CURLOPT_USERAGENT,
        CD5_USER_AGENT);

    CD5_CURL_SETOPT(
        CURLOPT_WRITEFUNCTION,
        cd5_curl_write);

    CD5_CURL_SETOPT(
        CURLOPT_WRITEDATA,
        &buffer);

    /*
     * Certificados raíz cargados directamente desde RAM.
     */
    CD5_CURL_SETOPT(
        CURLOPT_CAINFO_BLOB,
        &ca_blob);

    CD5_CURL_SETOPT(
        CURLOPT_SSL_VERIFYPEER,
        1L);

    CD5_CURL_SETOPT(
        CURLOPT_SSL_VERIFYHOST,
        2L);

    CD5_CURL_SETOPT(
        CURLOPT_FOLLOWLOCATION,
        1L);

    CD5_CURL_SETOPT(
        CURLOPT_MAXREDIRS,
        5L);

    CD5_CURL_SETOPT(
        CURLOPT_CONNECTTIMEOUT,
        15L);

    CD5_CURL_SETOPT(
        CURLOPT_TIMEOUT,
        30L);

    /*
     * MusicBrainz soporta HTTP/1.1 perfectamente.
     * Evitamos depender de HTTP/2 para esta prueba.
     */
    CD5_CURL_SETOPT(
        CURLOPT_HTTP_VERSION,
        CURL_HTTP_VERSION_1_1);


#undef CD5_CURL_SETOPT


    CURLcode result =
        curl_easy_perform(curl);

    if (result != CURLE_OK) {

        curl_easy_cleanup(curl);
        free(buffer.data);

        /*
         * Ejemplo:
         * CURLE_PEER_FAILED_VERIFICATION = 60
         * se mostrará como -60.
         */
        return -(int)result;
    }

    if (buffer.failed) {

        curl_easy_cleanup(curl);
        free(buffer.data);

        return -3;
    }

    long http_status = 0;

    result =
        curl_easy_getinfo(
            curl,
            CURLINFO_RESPONSE_CODE,
            &http_status);

    curl_easy_cleanup(curl);

    if (result != CURLE_OK) {
        free(buffer.data);
        return -(3000 + (int)result);
    }

    /*
     * Incluso una respuesta HTTP sin body devuelve un string
     * válido para simplificar al consumidor.
     */
    if (!buffer.data) {

        buffer.data =
            (char *)malloc(1);

        if (!buffer.data)
            return -4;

        buffer.data[0] = '\0';
        buffer.size = 0;
    }

    *out_data =
        buffer.data;

    *out_size =
        buffer.size;

    return (int)http_status;
}
