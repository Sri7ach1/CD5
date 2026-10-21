#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../src/cover.c"
const unsigned char cd5_icon0_png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,0,0,0,0,73,69,78,68,174,66,96,130};
const size_t cd5_icon0_png_len = sizeof(cd5_icon0_png);
static int response=200, bad;
int cd5_http_get(const char *url,char **body,size_t *n) {
    assert(strstr(url,"https://coverartarchive.org/")==url);
    *n=sizeof(cd5_icon0_png); *body=malloc(*n);
    memcpy(*body,cd5_icon0_png,*n);
    if (bad) memset(*body,0,*n);
    return response;
}
int main(void) {
    cd5_cover_t c={0};
    assert(cd5_cover_fetch_release("release",&c) && c.valid && c.status==1);
    cd5_cover_clear(&c);
    response=404;assert(!cd5_cover_fetch_release("release",&c) && c.status==2);
    assert(cd5_cover_load_fallback(&c) && c.status==2 && c.valid && !c.network);
    response=-7;assert(!cd5_cover_fetch_release("release",&c) && c.status==-1);
    response=200;bad=1;assert(!cd5_cover_fetch_release("release",&c) && c.status==-2);
    unsigned char huge[sizeof(cd5_icon0_png)];memcpy(huge,cd5_icon0_png,sizeof(huge));
    huge[16]=0;huge[17]=0;huge[18]=0x20;huge[19]=0; /* width 8192 */
    assert(!cd5_cover_decode(&c,huge,sizeof(huge),1));
    cd5_cover_clear(&c);
    puts("PASS: image decode, dimensions, cover absence/error and fallback state");
}
