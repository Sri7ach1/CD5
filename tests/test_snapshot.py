#!/usr/bin/env python3
"""Stress the actual publication/copy functions with one owner and one UI reader."""
from pathlib import Path
import subprocess
import tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'src/cd5.c').read_text()
a=s.index('typedef struct {\n    audio_track_t tracks[')
b=s.index('/*\n * BGRA/',a)
code=r'''
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
#include "toc.h"
#include "metadata.h"
#include "cover.h"
static cd5_metadata_t g_metadata;
static int g_metadata_state;
static cd5_cover_t g_cover;
''' + s[a:b] + r'''
static atomic_int done;
static void *writer(void *unused) {
    (void)unused;
    for (int i=0;i<20000;++i) {
        audio_track_t tracks[2]={{1,0,100},{2,100,200}};
        int count=1+i%2;
        g_metadata.track_count=count;
        g_metadata.tracks[0][0]=(char)('0'+count);
        g_metadata_state=1;
        publish_disc(tracks,count);
        if (!(i%3)) publish_disc(NULL,0);
    }
    atomic_store(&done,1);return NULL;
}
int main(void) {
    assert(pthread_mutex_init(&g_snapshot_mutex,NULL)==0);
    pthread_t thread;assert(!pthread_create(&thread,NULL,writer,NULL));
    do {
        cd5_ui_snapshot snapshot;snapshot_disc(&snapshot);
        assert(snapshot.count>=0 && snapshot.count<=2);
        assert(snapshot.count==snapshot.metadata.track_count);
        if (snapshot.count) {
            assert(snapshot.metadata.tracks[0][0]=='0'+snapshot.count);
            assert(snapshot.metadata_state==1 && snapshot.tracks[snapshot.count-1].number==snapshot.count);
        }
    } while (!atomic_load(&done));
    pthread_join(thread,NULL);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-pthread','-I',str(r/'src'),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=15)
print('PASS: concurrent metadata/TOC publication and clear (20,000 updates)')
