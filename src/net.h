#ifndef CD5_NET_H
#define CD5_NET_H

#include <stddef.h>

int cd5_net_init(void);
void cd5_net_fini(void);

/*
 * Descarga una URL HTTPS completa en memoria.
 *
 * Devuelve:
 *   >= 0  código HTTP
 *   <  0  error interno
 *
 * *out_data se reserva con malloc() y debe liberarse con free().
 */
int cd5_http_get(
    const char *url,
    char **out_data,
    size_t *out_size);

#endif
