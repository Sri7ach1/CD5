#!/usr/bin/env python3
"""Host fault-injection tests of the production recovery/audio functions.

Extract function bodies unchanged because the complete translation unit needs
PS5/CAM headers. This does not validate PS5 ABI, linking, or device behavior.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/cd5.c').read_text()

def function(name):
    start = source.index('static int ' + name + '(')
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# Use the production action values, not a second copy of the enum.
actions = re.search(r'enum \{\s*PLAYER_ACTION_NONE.*?\};', source, re.S).group()
stop_start = source.index('        if (track_action ==\n            PLAYER_ACTION_STOP)', source.index('track_finished:'))
stop_end = source.index('\n        /*', source.index('            continue;\n        }', stop_start))
stop_handler = source[stop_start:stop_end]
preamble = r'''
#include <assert.h>
#include <stdio.h>
#include "scsi_policy.h"
static cd5_io_result g_io;
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "version.h"
#define AUDIO_GRAIN 256
#define CHANNELS 2
typedef struct { uint8_t number; uint32_t start_lba, end_lba; } audio_track_t;
static int g_paused, ready, output_result, action, outputs, probes, notices;
static int resume_after, closed, sequence[4], sequence_count;
static int g_stopped, g_ui_state, g_ui_current_track;
static uint32_t g_ui_current_lba;
enum { UI_STATE_STOPPED, UI_STATE_PLAYING };
static int close(int fd) { assert(fd == 7); ++closed; return 0; }
static void notify(const char *s) { assert(s[0]); ++notices; }
static int disc_is_ready(int fd) { assert(fd == 7); ++probes; return ready; }
static int poll_pad(const audio_track_t *t, int i, int n) {
    (void)t; (void)i; (void)n;
    if (resume_after && outputs >= resume_after) g_paused = 0;
    if (sequence_count && outputs <= sequence_count) return sequence[outputs - 1];
    return action;
}
static int sceAudioOutOutput(int h, const void *p) {
    assert(h == 9 && p); ++outputs; assert(outputs < 100);
    return output_result;
}
static void reset(void) {
    g_paused = action = outputs = probes = notices = resume_after = 0;
    memset(&g_io, 0, sizeof(g_io));
    ready = 1; output_result = 0; closed = sequence_count = 0;
}
'''
stop_wrapper = '''
static int exercise_stop(void) {
    int track_action = PLAYER_ACTION_STOP, audio_handle = 9, pass_fd = 7;
    int current_track = 0, track_count = 2;
    audio_track_t tracks[2] = {{1, 0, 100}, {2, 100, 200}};
    for (int once = 0; once < 1; ++once) {
''' + stop_handler + '''
    }
    return 100 + current_track;
reload_disc:
    return -100;
}
'''
cases = r'''
int main(void) {
    float out[AUDIO_GRAIN * CHANNELS] = {0};
    audio_track_t track = {1, 0, 100};
    reset();
    assert(read_failure_action(7) == PLAYER_ACTION_STOP);
    assert(notices == 1 && probes == 1);
    reset(); g_io.kind = CD5_IO_MEDIA;
    assert(read_failure_action(7) == PLAYER_ACTION_DISC_CHANGED && probes == 0);
    reset(); g_io.kind = CD5_IO_GONE;
    assert(read_failure_action(7) == PLAYER_ACTION_DISC_CHANGED && probes == 0);
    reset(); ready = 0;
    assert(read_failure_action(7) == PLAYER_ACTION_DISC_CHANGED);
    assert(notices == 0);
    reset();
    assert(submit_audio_grain(9, 7, out, &track, 0, 1) == PLAYER_ACTION_NONE);
    assert(outputs == 1);
    reset(); output_result = -1;
    assert(submit_audio_grain(9, 7, out, &track, 0, 1) == PLAYER_ACTION_AUDIO_ERROR);
    reset(); g_paused = 1; output_result = -1;
    assert(submit_audio_grain(9, 7, out, &track, 0, 1) == PLAYER_ACTION_AUDIO_ERROR);
    assert(outputs == 1);
    reset(); g_paused = 1; ready = 0;
    assert(submit_audio_grain(9, 7, out, &track, 0, 1) == PLAYER_ACTION_DISC_CHANGED);
    assert(outputs == 20 && probes == 1);
    reset(); g_paused = 1; resume_after = 2;
    assert(submit_audio_grain(9, 7, out, &track, 0, 1) == PLAYER_ACTION_NONE);
    assert(outputs == 3);
    const int controls[] = {PLAYER_ACTION_STOP, PLAYER_ACTION_NEXT, PLAYER_ACTION_PREV};
    for (unsigned i = 0; i < sizeof(controls)/sizeof(*controls); ++i) {
        reset(); action = controls[i];
        assert(submit_audio_grain(9, 7, out, &track, 0, 1) == action);
        assert(outputs == 0);
    }
    reset(); action = PLAYER_ACTION_PLAY;
    assert(exercise_stop() == 100 && !g_stopped && outputs == 1);
    reset(); sequence_count = 2;
    sequence[0] = PLAYER_ACTION_NEXT; sequence[1] = PLAYER_ACTION_PLAY;
    assert(exercise_stop() == 101 && g_ui_current_track == 1);
    reset(); sequence_count = 2;
    sequence[0] = PLAYER_ACTION_PREV; sequence[1] = PLAYER_ACTION_PLAY;
    assert(exercise_stop() == 101);
    reset(); ready = 0;
    assert(exercise_stop() == -100 && probes == 1 && outputs == 20);
    reset(); output_result = -1;
    assert(exercise_stop() == EXIT_FAILURE && closed == 1 && notices == 1);
    assert(strcmp(CD5_USER_AGENT, "CD5/0.9.1-beta (https://github.com/Sri7ach1/CD5)") == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='cd5-recovery-') as temp:
    cfile = Path(temp) / 'recovery.c'
    binary = Path(temp) / 'recovery'
    cfile.write_text(preamble + actions + '\n' + function('read_failure_action') + '\n' + function('submit_audio_grain') + stop_wrapper + cases)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cfile), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=5)
print('PASS: read failure, media removal, AudioOut failure, pause/resume, controls, User-Agent')
