#!/usr/bin/env python3
"""Check the production helper selector without privileged operations."""
from pathlib import Path
import subprocess,tempfile
s=Path('helpers/unjail_pid/unjail_pid.c').read_text()
f=s[s.index('static pid_t find_cd_player'):s.index('int main(void)')]
code=r"""
#include <sys/types.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define TARGET_PATH_FRAGMENT "/PPSA00001/eboot.bin"
static const char *paths[4096];
static int get_process_path(pid_t p,char *out,size_t n) {
 if(!paths[p]) return 0;
 snprintf(out,n,"%s",paths[p]); return 1;
}
"""+f+r"""
int main(void) {
 assert(find_cd_player()==-1);
 paths[20]="/app0/PPSA00002/eboot.bin";
 paths[21]="/app0/PPSA00001/eboot.bin.extra";
 paths[22]="/app0/PPSA00001/eboot.bin";
 paths[4000]="/other/eboot.bin";
 assert(find_cd_player()==22);
 paths[40]="/different/PPSA00001/eboot.bin";
 assert(find_cd_player()==-1);
 paths[40]=0; paths[22]=0;
 assert(find_cd_player()==-1);
 puts("PASS: unjail exact application path, unrelated newer process, ambiguity and absence");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
