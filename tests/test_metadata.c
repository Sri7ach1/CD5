#include "metadata.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static int parse(const char *s,int n,cd5_metadata_t *m) {
    return cd5_metadata_parse_musicbrainz(s,strlen(s),n,m);
}
int main(void) {
    cd5_metadata_t m;
    assert(parse("{",1,&m)==-1);
    assert(parse("{}",1,&m)==-1);
    assert(parse("{\"releases\":[]}",1,&m)==0 && !m.valid);
    const char *s="{\"releases\":[{\"id\":\"release\",\"title\":\"Album\",\"artist-credit\":[{\"name\":\"Artist\"}],\"media\":[{\"track-count\":1,\"tracks\":[{\"title\":\"Song\",\"position\":1}]}]}]}";
    assert(parse(s,1,&m)==1 && m.valid && m.track_count==1 && !strcmp(m.tracks[0],"Song"));
    assert(parse(s,2,&m)==0 && !m.valid); /* do not use another disc's medium */
    puts("PASS: metadata result/error distinction and expected track count");
}
