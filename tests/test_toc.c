#include "toc.h"
#include "scsi_policy.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t basic[804], full[4096];
static size_t nb, nf;
static void size_field(uint8_t *b, size_t n) { b[0]=(n-2)>>8; b[1]=(n-2)&255; }
static void reset(unsigned last) {
    memset(basic,0,sizeof(basic));memset(full,0,sizeof(full));
    basic[2]=full[2]=1;basic[3]=last;full[3]=1;nb=nf=4;
}
static void entry(unsigned track,unsigned control,uint32_t lba) {
    uint8_t *p=basic+nb;p[1]=0x10|control;p[2]=track;
    p[4]=lba>>24;p[5]=lba>>16;p[6]=lba>>8;p[7]=lba;
    nb+=8;size_field(basic,nb);
}
static void full_entry(unsigned session,unsigned track,unsigned control,uint32_t lba) {
    uint8_t *p=full+nf;p[0]=session;p[1]=0x10|control;p[3]=track;
    lba+=150;p[8]=lba/(60*75);p[9]=lba/75%60;p[10]=lba%75;
    nf+=11;size_field(full,nf);
}
int main(void) {
    cd5_toc_t toc;
    reset(2);entry(1,0,0);entry(2,0,10000);entry(0xaa,0,20000);
    assert(cd5_toc_parse(basic,nb,NULL,0,&toc));
    assert(toc.count==2 && toc.audio[0].end_lba==10000 && toc.audio[1].end_lba==20000);
    assert(toc.disc.frame_offset[0]==20150 && toc.disc.frame_offset[1]==150);
    assert(!cd5_toc_parse(basic,nb-1,NULL,0,&toc));
    basic[12+2]=1;assert(!cd5_toc_parse(basic,nb,NULL,0,&toc));
    reset(2);entry(1,0,10000);entry(2,0,0);entry(0xaa,0,20000);
    assert(!cd5_toc_parse(basic,nb,NULL,0,&toc));
    reset(2);entry(1,0,0);entry(2,4,31400);entry(0xaa,4,40000);
    assert(!cd5_toc_parse(basic,nb,NULL,0,&toc)); /* no guessed gap */
    full[3]=2;full_entry(1,1,0,0);full_entry(1,0xa2,0,20000);
    full_entry(2,2,4,31400);full_entry(2,0xa2,4,40000);
    assert(cd5_toc_parse(basic,nb,full,nf,&toc));
    assert(toc.count==1 && toc.audio[0].end_lba==20000);
    assert(toc.disc.last_track==1 && toc.disc.frame_offset[0]==20150 && !toc.disc.frame_offset[2]);
    assert(!cd5_toc_parse(basic,nb,full,nf-1,&toc));
    full[4+10]=1;assert(!cd5_toc_parse(basic,nb,full,nf,&toc)); /* inconsistent start */
    reset(2);entry(1,4,0);entry(2,0,10000);entry(0xaa,0,20000);
    full_entry(1,1,4,0);full_entry(1,2,0,10000);full_entry(1,0xa2,0,20000);
    assert(cd5_toc_parse(basic,nb,full,nf,&toc));
    assert(toc.count==1 && toc.audio[0].number==2 && toc.disc.first_track==1);
    reset(1);entry(1,4,0);entry(0xaa,4,20000);full_entry(1,1,4,0);full_entry(1,0xa2,4,20000);
    assert(!cd5_toc_parse(basic,nb,full,nf,&toc));
    reset(1);entry(1,0,0);entry(0xaa,0,20000);full_entry(1,1,0,0);full_entry(1,0xa2,0,20000);
    full[4+9]=60;assert(!cd5_toc_parse(basic,nb,full,nf,&toc));
    uint8_t sense[18]={0x70,0,6,0,0,0,0,10};cd5_io_result r={0};
    assert(cd5_sense_policy(sense,18,&r)==CD5_IO_MEDIA);
    sense[2]=3;assert(cd5_sense_policy(sense,18,&r)==CD5_IO_STOP);
    sense[2]=0xb;assert(cd5_sense_policy(sense,18,&r)==CD5_IO_RETRY);
    sense[2]=2;sense[12]=0x3a;assert(cd5_sense_policy(sense,18,&r)==CD5_IO_MEDIA);
    assert(cd5_sense_policy(sense,7,&r)==CD5_IO_STOP);
    sense[0]=0x72;sense[1]=6;assert(cd5_sense_policy(sense,8,&r)==CD5_IO_MEDIA);
    sense[0]=0x71;assert(cd5_sense_policy(sense,18,&r)==CD5_IO_STOP);
    puts("PASS: TOC audio/data/multisession, malformed input and sense policy");
}
