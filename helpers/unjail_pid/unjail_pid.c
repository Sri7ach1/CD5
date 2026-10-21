/* Copyright (C) 2026 Sri7ach1
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See ../../LICENSE.
 */
#include <ps5/kernel.h>

#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/syscall.h>

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define TARGET_PATH_FRAGMENT "/PPSA00001/eboot.bin"

typedef struct {
    char unused[45];
    char message[3075];
} notify_request_t;

extern int sceKernelSendNotificationRequest(
    int device,
    void *request,
    size_t size,
    int blocking
);

static void notify(const char *text)
{
    notify_request_t req;

    memset(&req, 0, sizeof(req));

    snprintf(
        req.message,
        sizeof(req.message),
        "%s",
        text
    );

    sceKernelSendNotificationRequest(
        0,
        &req,
        sizeof(req),
        0
    );
}

static void notify_num(
    const char *fmt,
    long value)
{
    notify_request_t req;

    memset(&req, 0, sizeof(req));

    snprintf(
        req.message,
        sizeof(req.message),
        fmt,
        value
    );

    sceKernelSendNotificationRequest(
        0,
        &req,
        sizeof(req),
        0
    );
}

/*
 * Wrapper directo de __sysctl.
 *
 * El SDK tiene SYS___sysctl = 202 aunque libc no exporta
 * una función sysctl() utilizable en nuestros stubs.
 */
static int raw_sysctl(
    int *name,
    unsigned int namelen,
    void *oldp,
    size_t *oldlenp,
    const void *newp,
    size_t newlen)
{
    return syscall(
        SYS___sysctl,
        name,
        namelen,
        oldp,
        oldlenp,
        newp,
        newlen
    );
}

static int get_process_path(
    pid_t pid,
    char *path,
    size_t path_size)
{
    int mib[4] = {
        CTL_KERN,
        KERN_PROC,
        KERN_PROC_PATHNAME,
        pid
    };

    memset(path, 0, path_size);

    size_t len = path_size;

    if (raw_sysctl(
            mib,
            4,
            path,
            &len,
            NULL,
            0) < 0) {
        return 0;
    }

    if (path_size > 0)
        path[path_size - 1] = '\0';

    return path[0] != '\0';
}

static pid_t find_cd_player(void)
{
    pid_t best_pid = -1;

    /* Resolve the actual executable path. Never use UID/newest PID as a
     * substitute: that could grant privileges to an unrelated application.
     * Refuse ambiguous matches and path-query failures. */
    for (pid_t pid = 1; pid < 4096; ++pid) {
        char path[1024];
        if (!get_process_path(pid, path, sizeof(path))) continue;
        size_t n = strlen(path), suffix = strlen(TARGET_PATH_FRAGMENT);
        if (n < suffix || strcmp(path + n - suffix, TARGET_PATH_FRAGMENT)) continue;
        if (best_pid > 0) return -1;
        best_pid = pid;
    }

    return best_pid;
}

int main(void)
{
    static const uint8_t caps[16] = {
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff
    };

    notify("UNJAIL: searching for CD Audio Player");

    pid_t target_pid =
        find_cd_player();

    if (target_pid <= 0) {
        notify("UNJAIL: PPSA00001 not found");
        return 1;
    }

    notify_num(
        "UNJAIL: found PID %ld",
        (long)target_pid
    );

    intptr_t root =
        kernel_get_root_vnode();

    if (!root) {
        notify("UNJAIL: root vnode failed");
        return 1;
    }

    int r;

    r = kernel_set_proc_rootdir(
        target_pid,
        root
    );

    notify_num(
        "UNJAIL: set rootdir ret=%ld",
        r
    );

    if (r != 0) return 1;

    /*
     * Conservamos exactamente el comportamiento que ya hemos
     * probado funcionando en tu consola.
     */
    r = kernel_set_proc_jaildir(
        target_pid,
        0
    );

    notify_num(
        "UNJAIL: set jaildir ret=%ld",
        r
    );

    if (r != 0) return 1;

    r = kernel_set_ucred_uid(
        target_pid,
        0
    );

    notify_num(
        "UNJAIL: set uid ret=%ld",
        r
    );

    if (r != 0) return 1;

    r = kernel_set_ucred_caps(
        target_pid,
        caps
    );

    notify_num(
        "UNJAIL: set caps ret=%ld",
        r
    );

    if (r != 0) return 1;

    notify("UNJAIL: CD Audio Player ready");

    return 0;
}
