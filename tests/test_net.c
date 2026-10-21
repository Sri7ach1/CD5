#define _DEFAULT_SOURCE
#define CURL_DISABLE_TYPECHECK
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
static int sleep_calls;
static int test_sleep(useconds_t us) { assert(us >= 1000000); ++sleep_calls; return 0; }
#define usleep test_sleep
#include "../src/net.c"
const unsigned char cd5_ca_bundle[] = "mock CA";
const size_t cd5_ca_bundle_len = sizeof(cd5_ca_bundle);
static int calls, fail_tls, flags;
static long statuses[2];
static cd5_http_buffer_t *sink;
CURLcode curl_global_init(long v) { (void)v;return CURLE_OK; }
void curl_global_cleanup(void) {}
CURL *curl_easy_init(void) { return (CURL *)1; }
void curl_easy_cleanup(CURL *c) { assert(c); }
CURLcode curl_easy_setopt(CURL *c, CURLoption option, ...) {
    assert(c);va_list args;va_start(args,option);
    switch(option) {
    case CURLOPT_SSL_VERIFYPEER: assert(va_arg(args,long)==1);flags|=1;break;
    case CURLOPT_SSL_VERIFYHOST: assert(va_arg(args,long)==2);flags|=2;break;
    case CURLOPT_PROTOCOLS_STR: case CURLOPT_REDIR_PROTOCOLS_STR:
        assert(!strcmp(va_arg(args,const char *),"https"));flags|=4;break;
    case CURLOPT_NOSIGNAL: assert(va_arg(args,long)==1);flags|=8;break;
    case CURLOPT_WRITEDATA: sink=va_arg(args,cd5_http_buffer_t *);break;
    case CURLOPT_USERAGENT: assert(!strcmp(va_arg(args,const char *),CD5_USER_AGENT));break;
    case CURLOPT_MAXFILESIZE_LARGE: assert(va_arg(args,curl_off_t)==CD5_HTTP_MAX_BYTES);flags|=16;break;
    default: break;
    }
    va_end(args);return CURLE_OK;
}
CURLcode curl_easy_perform(CURL *c) {
    assert(c); ++calls;
    if (fail_tls) return CURLE_PEER_FAILED_VERIFICATION;
    assert(cd5_curl_write("ok",1,2,sink)==2);
    return CURLE_OK;
}
CURLcode curl_easy_getinfo(CURL *c, CURLINFO info, ...) {
    assert(c && info==CURLINFO_RESPONSE_CODE);
    va_list args;va_start(args,info);*va_arg(args,long *)=statuses[calls-1];va_end(args);
    return CURLE_OK;
}
static void reset(void) { calls=fail_tls=flags=sleep_calls=0;statuses[0]=statuses[1]=200; }
int main(void) {
    char *body=(char *)1;size_t n=99;
    assert(cd5_net_init()==0); reset();
    assert(cd5_http_get("http://example.com",&body,&n)==-4001 && body==NULL && n==0 && calls==0);
    assert(cd5_http_get("file:///etc/passwd",&body,&n)==-4001);
    reset(); statuses[0]=503;
    assert(cd5_http_get("https://musicbrainz.org/ws/2/",&body,&n)==200);
    assert(calls==2 && sleep_calls==2 && flags==31 && n==2 && !strcmp(body,"ok"));free(body);
    reset();statuses[0]=404;
    assert(cd5_http_get("https://example.com",&body,&n)==404 && calls==1);free(body);
    reset();statuses[0]=429;
    assert(cd5_http_get("https://example.com",&body,&n)==429 && calls==1);free(body);
    reset();fail_tls=1;
    assert(cd5_http_get("https://example.com",&body,&n)==-CURLE_PEER_FAILED_VERIFICATION && calls==1 && body==NULL);
    reset();statuses[0]=statuses[1]=503;
    assert(cd5_http_get("https://example.com",&body,&n)==503 && calls==2);free(body);
    cd5_http_buffer_t b={0};
    assert(cd5_curl_write("x",1,CD5_HTTP_MAX_BYTES+1,&b)==0 && b.failed && b.data==NULL);
    memset(&b,0,sizeof(b));
    assert(cd5_curl_write("x",SIZE_MAX,2,&b)==0 && b.failed);
    puts("PASS: HTTP retry budget, TLS policy, protocol/size limits, 404/429 and response ownership");
}
