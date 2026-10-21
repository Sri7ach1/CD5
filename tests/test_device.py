#!/usr/bin/env python3
"""Fault injection at the CAM boundary; production discovery/read functions."""
from pathlib import Path
import subprocess, tempfile
r=Path(__file__).resolve().parents[1];s=(r/'src/cd5.c').read_text()
def function(name,ret='int'):
    a=s.index('static '+ret+' '+name+'(');b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
preamble=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include "scsi_policy.h"
#define CD_SECTOR_BYTES 2352
static cd5_io_result g_io;
static const char *configured;
static int opens, closes, commands, cursor, many, mode, reads;
static char *fake_getenv(const char *key) {(void)key;return (char *)configured;}
static int fake_open(const char *path,int flags) {
    (void)flags; ++opens;
    int number=-1;assert(sscanf(path,"/dev/pass%d",&number)==1);
    return number+10;
}
static int fake_close(int fd) {assert(fd>=10);++closes;return 0;}
static DIR *fake_opendir(const char *p) {assert(!strcmp(p,"/dev"));cursor=0;return (DIR *)1;}
static int fake_closedir(DIR *d) {assert(d);return 0;}
static struct dirent *fake_readdir(DIR *d) {
    assert(d);static struct dirent e;
    if (cursor>=(many?30:3)) return NULL;
    snprintf(e.d_name,sizeof(e.d_name),"pass%d",cursor++);return &e;
}
static int fake_sleep(unsigned us) {assert(us==100000);return 0;}
#define open fake_open
#define close fake_close
#define getenv fake_getenv
#define opendir fake_opendir
#define readdir fake_readdir
#define closedir fake_closedir
#define usleep fake_sleep
static int cam_command(int fd,const uint8_t *cdb,uint8_t cdb_len,void *buf,uint32_t len,
                       uint32_t timeout,int short_ok,uint32_t *got) {
    (void)cdb_len;(void)len;(void)timeout;(void)short_ok;++commands;
    if (got) *got=0;
    if (cdb[0]==0xbe) {
        assert(cdb[1]==0x04);
        ++reads;
        if (mode==4 && reads==3) {*got=len;return 1;}
        g_io.kind=mode==5?CD5_IO_MEDIA:mode==6?CD5_IO_STOP:CD5_IO_RETRY;
        return 0;
    }
    if (mode==1) return 0;
    if (cdb[0]==0x12) {
        memset(buf,0,36);((uint8_t *)buf)[0]=(fd==10 || many)?0:5;
        *got=36;return 1;
    }
    if (cdb[0]==0) return fd==12;
    if (cdb[0]==0x43) {
        uint8_t toc[20]={0,18,1,1,0,0x10,1,0,0,0,0,0,0,0x10,0xaa,0,0,0,1,0};
        memcpy(buf,toc,sizeof(toc));*got=sizeof(toc);return 1;
    }
    assert(0);return 0;
}
static void reset(void) {configured=NULL;opens=closes=commands=cursor=many=mode=reads=0;}
'''
cases=r'''
int main(void) {
    assert(pass_name("pass123") && !pass_name("pass") && !pass_name("pass1/other") && !pass_name("disk0"));
    reset();assert(open_usb_pass()==12);assert(opens==3 && closes==2);close(12);
    reset();mode=1;assert(open_usb_pass()==-1 && opens==3 && closes==3);
    reset();configured="/dev/pass1";assert(open_usb_pass()==11 && opens==1);close(11);
    reset();configured="/etc/passwd";assert(open_usb_pass()==-1 && opens==0);
    reset();many=1;assert(open_usb_pass()==-1 && opens==16 && closes==16);
    reset();mode=1;assert(open_usb_pass()==-1);mode=0;assert(open_usb_pass()==12);close(12); /* reconnect */
    uint8_t audio[2352];
    reset();mode=4;assert(read_cdda_sectors(12,0,1,audio) && reads==3);
    reset();mode=0;assert(!read_cdda_sectors(12,0,1,audio) && reads==3);
    reset();mode=5;assert(!read_cdda_sectors(12,0,1,audio) && reads==1);
    reset();mode=6;assert(!read_cdda_sectors(12,0,1,audio) && reads==1);
    return 0;
}
'''
code=preamble+function('be16','uint16_t')+'\n'+''.join(function(n)+'\n' for n in ['pass_name','optical_candidate','open_usb_pass','read_cdda_sectors'])+cases
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(r/'src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=10)
print('PASS: optical selection, descriptor cleanup, absent/reconnected devices, scan/retry budgets')
