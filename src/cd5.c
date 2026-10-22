/*
 * CD5 - Native Audio CD Player for PlayStation 5
 *
 * Copyright (c) 2026 Sri7ach1
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Developed using PS5SX2 as a technical foundation.
 * https://github.com/Swordpdf/PS5SX2
 *
 * Third-party components retain their original copyright
 * notices and applicable licenses.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_message.h>
#include <cam/scsi/scsi_pass.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>

#include "net.h"
#include "discid.h"
#include "toc.h"
#include "scsi_policy.h"
#include <stdatomic.h>
#include "metadata.h"
#include "cover.h"
#include "lyrics.h"
#include "version.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>


/* ---- Pad / UserService minimal ABI ---- */

extern int sceUserServiceInitialize(void *param);
extern int sceUserServiceGetInitialUser(int *user_id);

extern int scePadInit(void);
extern int scePadOpen(int user_id, int type, int index, void *param);
extern int scePadReadState(int handle, void *data);

#define PAD_BUTTON_OPTIONS   0x0008u
#define PAD_BUTTON_RIGHT     0x0020u
#define PAD_BUTTON_LEFT      0x0080u
#define PAD_BUTTON_CROSS     0x4000u
#define PAD_BUTTON_SQUARE    0x8000u

/*
 * ScePadData: 120 bytes.
 * buttons is at offset 0x00.
 * CD5 currently only consumes the digital button mask.
 */
typedef struct {
    uint32_t buttons;
    uint8_t rest[116];
} pad_data_t;

#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

extern int sceSystemServiceHideSplashScreen(void);

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

static void notify(const char *msg)
{
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.message, sizeof(req.message), "%s", msg);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

extern int sceAudioOutInit(void);
extern int sceAudioOutOpen(
    int user_id,
    int type,
    int index,
    uint32_t len,
    uint32_t freq,
    uint32_t param
);
extern int sceAudioOutOutput(int handle, const void *ptr);

#define CD_SECTOR_BYTES   2352
#define CD_FRAMES_SECTOR  588
#define CD_RATE           44100

#define AUDIO_GRAIN       256
#define AUDIO_RATE        48000
#define CHANNELS          2

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

/* CAM and this diagnostic record are owned exclusively by the player thread. */
static cd5_io_result g_io;

static int cam_command(
    int fd,
    const uint8_t *cdb,
    uint8_t cdb_len,
    void *buf,
    uint32_t len,
    uint32_t timeout_ms,
    int allow_short,
    uint32_t *actual)
{
    if (actual)
        *actual = 0;

    memset(&g_io, 0, sizeof(g_io));
    g_io.kind = CD5_IO_STOP;
    union ccb c;
    memset(&c, 0, sizeof(c));

    cam_fill_csio(
        &c.csio,
        1,
        NULL,
        (len ? CAM_DIR_IN : CAM_DIR_NONE) | CAM_DEV_QFRZDIS,
        MSG_SIMPLE_Q_TAG,
        (uint8_t *)buf,
        len,
        SSD_FULL_SIZE,
        cdb_len,
        timeout_ms
    );

    memcpy(c.csio.cdb_io.cdb_bytes, cdb, cdb_len);

    if (ioctl(fd, CAMIOCOMMAND, &c) != 0) {
        g_io.os_error = errno;
        if (errno == ENXIO || errno == ENODEV || errno == EBADF)
            g_io.kind = CD5_IO_GONE;
        else if (errno == EINTR || errno == EAGAIN)
            g_io.kind = CD5_IO_RETRY;
        return 0;
    }
    g_io.cam_status = c.ccb_h.status & CAM_STATUS_MASK;
    g_io.scsi_status = c.csio.scsi_status;
    if (g_io.cam_status != CAM_REQ_CMP) {
        if (g_io.cam_status == CAM_DEV_NOT_THERE || g_io.cam_status == CAM_SEL_TIMEOUT)
            g_io.kind = CD5_IO_GONE;
        else if (g_io.cam_status == CAM_BUSY || g_io.cam_status == CAM_SCSI_BUSY)
            g_io.kind = CD5_IO_RETRY;
        if (c.ccb_h.status & CAM_AUTOSNS_VALID) {
            size_t n = c.csio.sense_len;
            if (n > sizeof(c.csio.sense_data)) n = sizeof(c.csio.sense_data);
            n = c.csio.sense_resid <= n ? n - c.csio.sense_resid : 0;
            g_io.kind = cd5_sense_policy((const uint8_t *)&c.csio.sense_data, n, &g_io);
        }
        return 0;
    }

    uint32_t resid = c.csio.resid;
    uint32_t got = resid <= len ? len - resid : 0;

    if (actual)
        *actual = got;

    if (len && resid && !allow_short)
        return 0;

    g_io.kind = CD5_IO_OK;
    return 1;
}

static int pass_name(const char *name)
{
    if (strncmp(name, "pass", 4) || !name[4]) return 0;
    for (const char *p = name + 4; *p; ++p)
        if (*p < '0' || *p > '9') return 0;
    return 1;
}

/* INQUIRY type 5 identifies an optical candidate, not CDDA compatibility.
 * Prefer a ready candidate with audio tracks; actual playback requires the
 * validated TOC and READ CD. A configured device is never silently replaced. */
static int optical_candidate(const char *path, int *has_audio)
{
    int fd = open(path, O_RDWR);
    if (fd < 0) fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    uint8_t inquiry[36] = {0}, cdb[6] = {0x12, 0, 0, 0, 36, 0};
    uint32_t got = 0;
    if (!cam_command(fd, cdb, 6, inquiry, sizeof(inquiry), 1000, 1, &got) ||
        got < 36 || (inquiry[0] & 0xe0) || (inquiry[0] & 31) != 5) {
        close(fd); return -1;
    }
    uint8_t tur[6] = {0};
    *has_audio = 0;
    if (cam_command(fd, tur, 6, NULL, 0, 1000, 0, NULL)) {
        uint8_t toc[804] = {0}, read_toc[10] = {0x43,0,0,0,0,0,0,3,36,0};
        if (cam_command(fd, read_toc, 10, toc, sizeof(toc), 1000, 1, &got)) {
            uint32_t end = got >= 4 ? (uint32_t)be16(toc) + 2 : 0;
            if (end <= got)
                for (uint32_t i = 4; i + 8 <= end; i += 8)
                    if (toc[i+2] >= 1 && toc[i+2] <= 99 && !(toc[i+1] & 4))
                        *has_audio = 1;
        }
    }
    return fd;
}

static int open_usb_pass(void)
{
    const char *configured = getenv("CD5_DEVICE");
    int audio = 0;
    if (configured && *configured) {
        if (strncmp(configured, "/dev/", 5) || !pass_name(configured + 5)) return -1;
        return optical_candidate(configured, &audio);
    }
    DIR *dir = opendir("/dev");
    if (!dir) return -1;
    int fallback = -1, selected = -1, probes = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) && probes < 16) {
        if (!pass_name(entry->d_name)) continue;
        ++probes;
        char path[300];
        int n = snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        int fd = optical_candidate(path, &audio);
        if (fd < 0) continue;
        if (audio) { selected = fd; break; }
        if (fallback < 0) fallback = fd;
        else close(fd);
    }
    closedir(dir);
    if (selected >= 0) { if (fallback >= 0) close(fallback); return selected; }
    return fallback;
}

/* A single validated TOC supplies both playback bounds and Disc ID. */
static int read_disc_toc(int fd, cd5_toc_t *toc)
{
    uint8_t basic[804] = {0}, full[4096] = {0};
    uint32_t basic_size = 0, full_size = 0;
    uint8_t cdb[10] = {0x43, 0, 0, 0, 0, 0, 0, 3, 36, 0};
    if (!cam_command(fd, cdb, sizeof(cdb), basic, sizeof(basic),
                     3000, 1, &basic_size)) return 0;
    cdb[1] = 2; /* MSF; full TOC returns binary MSF session descriptors. */
    cdb[2] = 2;
    cdb[7] = sizeof(full) >> 8;
    cdb[8] = sizeof(full) & 255;
    if (!cam_command(fd, cdb, sizeof(cdb), full, sizeof(full),
                     3000, 1, &full_size)) {
        /* Only an unsupported command/format permits the basic-TOC fallback.
         * Never accept a TOC captured before a media/transport error. */
        if (g_io.key != SSD_KEY_ILLEGAL_REQUEST) return 0;
        full_size = 0;
    }
    return cd5_toc_parse(basic, basic_size, full, full_size, toc);
}

#define READ_SECTORS 16
#define READ_BYTES   (CD_SECTOR_BYTES * READ_SECTORS)

static int read_cdda_sectors(
    int pass_fd,
    uint32_t lba,
    uint32_t sectors,
    uint8_t *buffer)
{
    uint8_t read_cdb[12];
    memset(read_cdb, 0, sizeof(read_cdb));

    read_cdb[0] = 0xBE;
    read_cdb[1] = 0x04; /* MMC expected sector type 001b: CD-DA only. */
    read_cdb[2] = (uint8_t)(lba >> 24);
    read_cdb[3] = (uint8_t)(lba >> 16);
    read_cdb[4] = (uint8_t)(lba >> 8);
    read_cdb[5] = (uint8_t)lba;

    read_cdb[6] = (uint8_t)(sectors >> 16);
    read_cdb[7] = (uint8_t)(sectors >> 8);
    read_cdb[8] = (uint8_t)sectors;

    read_cdb[9] = 0xF8;

    uint32_t bytes = sectors * CD_SECTOR_BYTES;
    uint32_t received = 0;

    for (int attempt = 0; attempt < 3; ++attempt) {
        if (cam_command(pass_fd, read_cdb, sizeof(read_cdb), buffer, bytes,
                        5000, 0, &received)) return received == bytes;
        if (g_io.kind != CD5_IO_RETRY || attempt == 2) return 0;
        usleep(100000);
    }

    return received == bytes;
}

static int16_t read_le16(const uint8_t *p)
{
    uint16_t v =
        ((uint16_t)p[0]) |
        ((uint16_t)p[1] << 8);

    return (int16_t)v;
}



static uint16_t net16(uint16_t v)
{
    return (uint16_t)(
        ((v & 0x00ffu) << 8) |
        ((v & 0xff00u) >> 8)
    );
}

static uint32_t net32(uint32_t v)
{
    return
        ((v & 0x000000ffu) << 24) |
        ((v & 0x0000ff00u) << 8)  |
        ((v & 0x00ff0000u) >> 8)  |
        ((v & 0xff000000u) >> 24);
}

/*
 * Pide al Payload Manager residente en la propia PS5 que ejecute
 * el helper autónomo:
 *
 *   /data/pldmgr/payloads/unjail_pid/unjail_pid.elf
 *
 * No necesitamos conocer nuestro PID aquí: el helper lo busca.
 */
static int trigger_local_unjail(void)
{
    static const char request[] =
        "POST /loadpayload:unjail_pid%2Funjail_pid.elf HTTP/1.1\r\n"
        "Host: 127.0.0.1:8084\r\n"
        "X-PLDMGR-Request: 1\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n";

    int fd = socket(
        AF_INET,
        SOCK_STREAM,
        0);

    if (fd < 0) {
        notify("CDDA: autounjail socket FAILED");
        return 0;
    }

    struct timeval timeout = {2, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(fd); return 0;
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

#ifdef __FreeBSD__
    addr.sin_len = sizeof(addr);
#endif

    addr.sin_family = AF_INET;
    addr.sin_port = net16(8084);

    /*
     * 127.0.0.1 en orden de red.
     * Evitamos depender de inet_addr()/inet_pton().
     */
    addr.sin_addr.s_addr =
        net32(0x7f000001u);

    if (connect(
            fd,
            (const struct sockaddr *)&addr,
            sizeof(addr)) < 0) {

        close(fd);
        notify("CDDA: autounjail connect FAILED");
        return 0;
    }

    size_t total = 0;
    size_t request_len =
        sizeof(request) - 1;

    while (total < request_len) {

        ssize_t n = send(
            fd,
            request + total,
            request_len - total,
            0);

        if (n <= 0) {
            close(fd);
            notify("CDDA: autounjail send FAILED");
            return 0;
        }

        total += (size_t)n;
    }

    /*
     * No necesitamos procesar el cuerpo HTTP.
     * Leemos algo únicamente para dar tiempo a PLDMGR a aceptar
     * la petición antes de cerrar.
     */
    char response[256];

    (void)recv(
        fd,
        response,
        sizeof(response),
        0);

    close(fd);


    return 1;
}


static int g_paused = 0;

/* Renderer CPU inspirado en demo_renderer.cpp de PS5SX2.
 * 1920x1080, dos buffers, bus OVERLAY.
 */

#define UI_WIDTH        1920u
#define UI_HEIGHT       1080u
#define UI_FRAME_BYTES  0x1000000UL
#define UI_MEMORY_BYTES (UI_FRAME_BYTES * 2UL)
#define UI_ALIGNMENT    0x200000UL

#define UI_MEMORY_TYPE  3
#define UI_MAP_PROT     0x33

#define UI_PIXEL_FORMAT UINT64_C(0x8000000022000000)

typedef struct {
    void *data;
    void *metadata;
    void *reserved0;
    void *reserved1;
} ui_video_buffer_t;

typedef struct {
    uint8_t reserved[80];
} ui_video_attribute_t;

extern size_t sceKernelGetDirectMemorySize(void);

extern int sceKernelAllocateDirectMemory(
    int64_t search_start,
    int64_t search_end,
    size_t length,
    size_t alignment,
    int memory_type,
    int64_t *physical_address);

extern int sceKernelMapDirectMemory(
    void **address,
    size_t length,
    int protection,
    int flags,
    int64_t physical_address,
    size_t alignment);

extern int sceVideoOutOpen(
    int32_t user_id,
    int32_t bus_type,
    int32_t index,
    const void *param);

extern int sceVideoOutSetFlipRate(
    int32_t handle,
    int32_t rate);

extern void sceVideoOutSetBufferAttribute2(
    ui_video_attribute_t *attribute,
    uint64_t pixel_format,
    uint32_t tiling_mode,
    uint32_t width,
    uint32_t height,
    uint64_t option,
    uint32_t dcc_control,
    uint64_t dcc_clear_color);

extern int sceVideoOutRegisterBuffers2(
    int32_t handle,
    int32_t set_index,
    int32_t buffer_index_start,
    ui_video_buffer_t *buffers,
    int32_t buffer_count,
    ui_video_attribute_t *attribute,
    int32_t category,
    void *option);

extern int sceVideoOutSubmitFlip(
    int32_t handle,
    int32_t buffer_index,
    uint32_t flip_mode,
    int64_t flip_argument);

extern int sceVideoOutWaitVblank(
    int32_t handle);

extern int sceVideoOutGetFlipStatus(
    int32_t handle,
    void *status);


/*
 * Estado que publica el reproductor.
 *
 * Sólo lo lee el hilo UI.
 */

static _Atomic int g_ui_current_track = 0;
static _Atomic uint32_t g_ui_current_lba = 0;
static _Atomic uint64_t g_ui_lba_anchor_ms = 0;

static uint64_t cd5_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000u +
           (uint64_t)ts.tv_nsec / 1000000u;
}


enum {
    UI_STATE_NO_DISC = 0,
    UI_STATE_LOADING,
    UI_STATE_PLAYING,
    UI_STATE_PAUSED,
    UI_STATE_STOPPED,
    UI_STATE_NO_DRIVE,
    UI_STATE_DISC_ERROR
};

static _Atomic int g_ui_state = UI_STATE_NO_DISC;

/*
 * Estado temporal de diagnóstico de red.
 *
 *  0 = aún no probado
 *  1 = online / HTTP 200
 * -1 = error
 */
static _Atomic int g_ui_network_state = 0;
static _Atomic int g_ui_network_http = -1;
static _Atomic uint32_t g_ui_network_bytes = 0;
static _Atomic uint32_t g_ui_network_url_len = 0;

static cd5_metadata_t g_metadata;
/* Player-owned: 0 untried, 1 valid, 2 absent, -1 unavailable, -2 invalid response. */
static int g_metadata_state = 0;

static cd5_cover_t g_cover;

#define CD5_COVER_RENDER_SIZE 330

static uint32_t g_cover_render[
    CD5_COVER_RENDER_SIZE * CD5_COVER_RENDER_SIZE
];

static int g_cover_render_valid = 0;

static pthread_mutex_t g_cover_mutex;

static void cd5_cover_render_clear(void)
{
    pthread_mutex_lock(&g_cover_mutex);
    g_cover_render_valid = 0;
    pthread_mutex_unlock(&g_cover_mutex);
}

static void cd5_cover_render_build(void)
{
    pthread_mutex_lock(&g_cover_mutex);

    g_cover_render_valid = 0;

    if (g_cover.valid &&
        g_cover.rgba &&
        g_cover.width > 0 &&
        g_cover.height > 0) {

        int crop = g_cover.width < g_cover.height
            ? g_cover.width : g_cover.height;

        int ox = (g_cover.width - crop) / 2;
        int oy = (g_cover.height - crop) / 2;

        for (int y = 0; y < CD5_COVER_RENDER_SIZE; ++y) {
            int sy = oy + (int)(
                (uint64_t)y * crop / CD5_COVER_RENDER_SIZE
            );

            for (int x = 0; x < CD5_COVER_RENDER_SIZE; ++x) {
                int sx = ox + (int)(
                    (uint64_t)x * crop / CD5_COVER_RENDER_SIZE
                );

                const unsigned char *src =
                    g_cover.rgba +
                    ((size_t)sy * g_cover.width + sx) * 4;

                g_cover_render[
                    y * CD5_COVER_RENDER_SIZE + x
                ] = UINT32_C(0xff000000) |
                    ((uint32_t)src[0] << 16) |
                    ((uint32_t)src[1] << 8) |
                    src[2];
            }
        }

        g_cover_render_valid = 1;
    }

    pthread_mutex_unlock(&g_cover_mutex);
}



/*
 * Letras en RAM, sin caché en disco.
 *
 * state:
 *   0 = vacío
 *   1 = cargando
 *   2 = listo
 *   3 = instrumental
 *  -1 = no disponible
 *  -2 = error
 */
static cd5_lyrics_t g_lyrics;
static _Atomic int g_lyrics_state = 0;
static _Atomic int g_lyrics_generation = 0;

static pthread_mutex_t g_lyrics_mutex;


typedef struct {
    int generation;
    char track[CD5_METADATA_TEXT_MAX];
    char artist[CD5_METADATA_TEXT_MAX];
    char album[CD5_METADATA_TEXT_MAX];
    int duration;
} cd5_lyrics_job_t;


static void cd5_lyrics_cancel_and_clear(void)
{
    pthread_mutex_lock(
        &g_lyrics_mutex);

    ++g_lyrics_generation;

    cd5_lyrics_clear(
        &g_lyrics);

    g_lyrics_state = 0;

    pthread_mutex_unlock(
        &g_lyrics_mutex);
}


static void *cd5_lyrics_thread(
    void *arg)
{
    cd5_lyrics_job_t *job =
        (cd5_lyrics_job_t *)arg;

    if (!job)
        return NULL;

    cd5_lyrics_t result;

    int rc =
        cd5_lyrics_fetch(
            job->track,
            job->artist,
            job->album,
            job->duration,
            &result);

    pthread_mutex_lock(
        &g_lyrics_mutex);

    if (job->generation ==
        g_lyrics_generation) {

        if (rc == 1) {

            g_lyrics = result;
            g_lyrics_state = 2;

        } else if (rc == 2) {

            cd5_lyrics_clear(
                &g_lyrics);

            g_lyrics.instrumental = 1;
            g_lyrics_state = 3;

        } else if (rc == 0) {

            cd5_lyrics_clear(
                &g_lyrics);

            g_lyrics_state = -1;

        } else {

            cd5_lyrics_clear(
                &g_lyrics);

            g_lyrics_state = -2;
        }
    }

    pthread_mutex_unlock(
        &g_lyrics_mutex);

    free(job);

    return NULL;
}


static void cd5_lyrics_request(
    int track_index,
    int duration_seconds)
{
    if (g_metadata_state != 1 ||
        track_index < 0 ||
        track_index >=
            g_metadata.track_count ||
        !g_metadata.tracks[
            track_index][0] ||
        !g_metadata.artist[0])
        return;

    cd5_lyrics_job_t *job =
        (cd5_lyrics_job_t *)calloc(
            1,
            sizeof(*job));

    if (!job)
        return;

    pthread_mutex_lock(
        &g_lyrics_mutex);

    ++g_lyrics_generation;

    job->generation =
        g_lyrics_generation;

    cd5_lyrics_clear(
        &g_lyrics);

    g_lyrics_state = 1;

    pthread_mutex_unlock(
        &g_lyrics_mutex);

    snprintf(
        job->track,
        sizeof(job->track),
        "%s",
        g_metadata.tracks[
            track_index]);

    snprintf(
        job->artist,
        sizeof(job->artist),
        "%s",
        g_metadata.artist);

    snprintf(
        job->album,
        sizeof(job->album),
        "%s",
        g_metadata.album);

    job->duration =
        duration_seconds;

    pthread_t thread;
    pthread_attr_t attr;

    if (pthread_attr_init(&attr) != 0) {

        int generation =
            job->generation;

        free(job);

        pthread_mutex_lock(
            &g_lyrics_mutex);

        if (generation ==
            g_lyrics_generation)
            g_lyrics_state = -2;

        pthread_mutex_unlock(
            &g_lyrics_mutex);

        return;
    }

    (void)pthread_attr_setstacksize(
        &attr,
        512 * 1024);

    int thread_result =
        pthread_create(
            &thread,
            &attr,
            cd5_lyrics_thread,
            job);

    (void)pthread_attr_destroy(
        &attr);

    if (thread_result != 0) {

        int generation =
            job->generation;

        free(job);

        pthread_mutex_lock(
            &g_lyrics_mutex);

        if (generation ==
            g_lyrics_generation)
            g_lyrics_state = -2;

        pthread_mutex_unlock(
            &g_lyrics_mutex);

        return;
    }

    pthread_detach(thread);
}

static _Atomic int g_ui_uid_before = -1;
static _Atomic int g_ui_uid_after = -1;
static _Atomic int g_ui_euid_before = -1;
static _Atomic int g_ui_euid_after = -1;

typedef struct {
    audio_track_t tracks[MAX_AUDIO_TRACKS];
    int count, metadata_state, cover_state;
    cd5_metadata_t metadata;
} cd5_ui_snapshot;
static cd5_ui_snapshot g_snapshot;
static pthread_mutex_t g_snapshot_mutex;

static void publish_disc(const audio_track_t *tracks, int count)
{
    pthread_mutex_lock(&g_snapshot_mutex);
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    if (tracks && count > 0 && count <= MAX_AUDIO_TRACKS) {
        memcpy(g_snapshot.tracks, tracks, (size_t)count * sizeof(*tracks));
        g_snapshot.count = count;
        g_snapshot.metadata = g_metadata;
        g_snapshot.metadata_state = g_metadata_state;
        g_snapshot.cover_state = g_cover.status;
    }
    pthread_mutex_unlock(&g_snapshot_mutex);
}

static void snapshot_disc(cd5_ui_snapshot *snapshot)
{
    pthread_mutex_lock(&g_snapshot_mutex);
    *snapshot = g_snapshot;
    pthread_mutex_unlock(&g_snapshot_mutex);
}


/*
 * BGRA/RGBA32 tal como lo utiliza el framebuffer de referencia.
 */
#define UI_BG        UINT32_C(0xff121212)
#define UI_PANEL     UINT32_C(0xff202020)
#define UI_PANEL2    UINT32_C(0xff181818)
#define UI_WHITE     UINT32_C(0xffffffff)
#define UI_GREY      UINT32_C(0xffa7a7a7)
#define UI_DARKGREY  UINT32_C(0xff535353)
#define UI_GREEN     UINT32_C(0xff1ed760)
#define UI_BLACK     UINT32_C(0xff000000)
#define UI_SOFT      UINT32_C(0xff2a2a2a)


static size_t ui_tiled_offset(
    unsigned x,
    unsigned y)
{
    uint32_t offset =
        ((y << 4) & 0x70u) ^
        ((y << 5) & 0xf00u) ^
        ((y << 9) & 0x1000u) ^
        ((y << 8) & 0x4000u) ^
        ((x << 2) & 0xcu) ^
        ((x << 5) & 0x380u) ^
        ((x << 4) & 0x400u) ^
        ((x << 6) & 0x800u) ^
        ((x << 9) & 0xa000u);

    uint32_t blocks_per_row =
        (UI_WIDTH + 127u) >> 7;

    uint32_t block_index =
        (y >> 7) * blocks_per_row +
        (x >> 7);

    return
        ((size_t)block_index << 16) +
        offset;
}


static void ui_pixel(
    uint32_t *pixels,
    unsigned x,
    unsigned y,
    uint32_t color)
{
    if (x >= UI_WIDTH ||
        y >= UI_HEIGHT)
        return;

    uint8_t *bytes =
        (uint8_t *)pixels;

    *(uint32_t *)(
        bytes +
        ui_tiled_offset(x, y)) =
            color;
}


static void ui_rect(
    uint32_t *pixels,
    unsigned x,
    unsigned y,
    unsigned w,
    unsigned h,
    uint32_t color)
{
    if (x >= UI_WIDTH ||
        y >= UI_HEIGHT)
        return;

    if (w > UI_WIDTH - x)
        w = UI_WIDTH - x;

    if (h > UI_HEIGHT - y)
        h = UI_HEIGHT - y;

    for (unsigned yy = 0;
         yy < h;
         ++yy) {

        for (unsigned xx = 0;
             xx < w;
             ++xx) {

            ui_pixel(
                pixels,
                x + xx,
                y + yy,
                color);
        }
    }
}


static void ui_circle(
    uint32_t *pixels,
    int cx,
    int cy,
    int radius,
    uint32_t color)
{
    int rr = radius * radius;

    for (int y = -radius;
         y <= radius;
         ++y) {

        for (int x = -radius;
             x <= radius;
             ++x) {

            if (x * x + y * y <= rr) {

                int px = cx + x;
                int py = cy + y;

                if (px >= 0 &&
                    py >= 0 &&
                    px < (int)UI_WIDTH &&
                    py < (int)UI_HEIGHT) {

                    ui_pixel(
                        pixels,
                        (unsigned)px,
                        (unsigned)py,
                        color);
                }
            }
        }
    }
}


static void ui_play_triangle(
    uint32_t *pixels,
    unsigned cx,
    unsigned cy,
    unsigned size,
    uint32_t color)
{
    for (unsigned row = 0;
         row < size;
         ++row) {

        unsigned half =
            row <= size / 2
                ? row
                : size - row;

        unsigned x =
            cx - size / 4;

        unsigned y =
            cy - size / 2 + row;

        ui_rect(
            pixels,
            x,
            y,
            half + 1,
            1,
            color);
    }
}


/*
 * Fuente 5x7 mínima.
 */
static const uint8_t *ui_glyph(
    char c)
{
#define G(name,a,b,c,d,e,f,g) \
    static const uint8_t name[7] = {a,b,c,d,e,f,g}

    G(sp,0,0,0,0,0,0,0);

    G(A,14,17,17,31,17,17,17);
    G(B,30,17,17,30,17,17,30);
    G(C,14,17,16,16,16,17,14);
    G(D,30,17,17,17,17,17,30);
    G(E,31,16,16,30,16,16,31);
    G(F,31,16,16,30,16,16,16);
    G(G,14,17,16,23,17,17,14);
    G(H,17,17,17,31,17,17,17);
    G(I,31,4,4,4,4,4,31);
    G(J,7,2,2,2,18,18,12);
    G(K,17,18,20,24,20,18,17);
    G(L,16,16,16,16,16,16,31);
    G(M,17,27,21,21,17,17,17);
    G(N,17,25,21,19,17,17,17);
    G(O,14,17,17,17,17,17,14);
    G(P,30,17,17,30,16,16,16);
    G(Q,14,17,17,17,21,18,13);
    G(R,30,17,17,30,20,18,17);
    G(S,15,16,16,14,1,1,30);
    G(T,31,4,4,4,4,4,4);
    G(U,17,17,17,17,17,17,14);
    G(V,17,17,17,17,17,10,4);
    G(W,17,17,17,21,21,21,10);
    G(X,17,17,10,4,10,17,17);
    G(Y,17,17,10,4,4,4,4);
    G(Z,31,1,2,4,8,16,31);

    G(n0,14,17,19,21,25,17,14);
    G(n1,4,12,4,4,4,4,14);
    G(n2,14,17,1,2,4,8,31);
    G(n3,30,1,1,14,1,1,30);
    G(n4,2,6,10,18,31,2,2);
    G(n5,31,16,16,30,1,1,30);
    G(n6,14,16,16,30,17,17,14);
    G(n7,31,1,2,4,8,8,8);
    G(n8,14,17,17,14,17,17,14);
    G(n9,14,17,17,15,1,1,14);

    G(colon,0,12,12,0,12,12,0);
    G(dash,0,0,0,14,0,0,0);
    G(slash,1,1,2,4,8,16,16);

#undef G

    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');

    switch (c) {
        case 'A': return A;
        case 'B': return B;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'F': return F;
        case 'G': return G;
        case 'H': return H;
        case 'I': return I;
        case 'J': return J;
        case 'K': return K;
        case 'L': return L;
        case 'M': return M;
        case 'N': return N;
        case 'O': return O;
        case 'P': return P;
        case 'Q': return Q;
        case 'R': return R;
        case 'S': return S;
        case 'T': return T;
        case 'U': return U;
        case 'V': return V;
        case 'W': return W;
        case 'X': return X;
        case 'Y': return Y;
        case 'Z': return Z;

        case '0': return n0;
        case '1': return n1;
        case '2': return n2;
        case '3': return n3;
        case '4': return n4;
        case '5': return n5;
        case '6': return n6;
        case '7': return n7;
        case '8': return n8;
        case '9': return n9;

        case ':': return colon;
        case '-': return dash;
        case '/': return slash;

        default:
            return sp;
    }
}


static void ui_text(
    uint32_t *pixels,
    unsigned x,
    unsigned y,
    const char *text,
    unsigned scale,
    uint32_t color)
{
    while (*text) {

        const uint8_t *rows =
            ui_glyph(*text++);

        for (unsigned row = 0;
             row < 7;
             ++row) {

            for (unsigned col = 0;
                 col < 5;
                 ++col) {

                if (rows[row] &
                    (1u << (4 - col))) {

                    ui_rect(
                        pixels,
                        x + col * scale,
                        y + row * scale,
                        scale,
                        scale,
                        color);
                }
            }
        }

        x += 6 * scale;
    }
}


static void ui_flush(
    void *address,
    size_t length)
{
    uint8_t *p =
        (uint8_t *)address;

    uint8_t *end =
        p + length;

    for (; p < end; p += 64)
        __asm__ volatile(
            "clflush (%0)"
            :
            : "r"(p)
            : "memory");

    __asm__ volatile(
        "mfence"
        :
        :
        : "memory");
}


/*
 * Convierte temporalmente UTF-8 a un subconjunto ASCII que
 * puede representar nuestra fuente 5x7.
 *
 * La metadata original NO se modifica.
 */
static void ui_metadata_text(
    char *dst,
    size_t dst_size,
    const char *src,
    size_t max_chars)
{
    if (!dst || dst_size == 0)
        return;

    dst[0] = '\0';

    if (!src)
        return;

    size_t out = 0;
    size_t chars = 0;

    while (*src &&
           out + 1 < dst_size &&
           chars < max_chars) {

        unsigned char c =
            (unsigned char)*src++;

        /*
         * ASCII.
         */
        if (c < 0x80) {

            if (c >= 'a' && c <= 'z')
                c =
                    (unsigned char)(
                        c - 'a' + 'A');

            /*
             * Evitar controles.
             */
            if (c < 0x20)
                c = ' ';

            dst[out++] = (char)c;
            ++chars;
            continue;
        }

        /*
         * Latin-1 habitual codificado como UTF-8 C3 xx.
         * Nos basta para muchos títulos occidentales mientras
         * sigamos usando la fuente bitmap mínima.
         */
        if (c == 0xc3 &&
            *src) {

            unsigned char d =
                (unsigned char)*src++;

            char mapped = '?';

            switch (d) {

            /* A */
            case 0x80: case 0x81:
            case 0x82: case 0x83:
            case 0x84: case 0x85:
            case 0xa0: case 0xa1:
            case 0xa2: case 0xa3:
            case 0xa4: case 0xa5:
                mapped = 'A';
                break;

            /* C */
            case 0x87: case 0xa7:
                mapped = 'C';
                break;

            /* E */
            case 0x88: case 0x89:
            case 0x8a: case 0x8b:
            case 0xa8: case 0xa9:
            case 0xaa: case 0xab:
                mapped = 'E';
                break;

            /* I */
            case 0x8c: case 0x8d:
            case 0x8e: case 0x8f:
            case 0xac: case 0xad:
            case 0xae: case 0xaf:
                mapped = 'I';
                break;

            /* N */
            case 0x91: case 0xb1:
                mapped = 'N';
                break;

            /* O */
            case 0x92: case 0x93:
            case 0x94: case 0x95:
            case 0x96:
            case 0xb2: case 0xb3:
            case 0xb4: case 0xb5:
            case 0xb6:
                mapped = 'O';
                break;

            /* U */
            case 0x99: case 0x9a:
            case 0x9b: case 0x9c:
            case 0xb9: case 0xba:
            case 0xbb: case 0xbc:
                mapped = 'U';
                break;

            /* Y */
            case 0x9d: case 0xbd:
            case 0xbf:
                mapped = 'Y';
                break;
            }

            dst[out++] = mapped;
            ++chars;
            continue;
        }

        /*
         * Saltar bytes de continuación del resto de UTF-8.
         * Dejamos un '?' visible por carácter no soportado.
         */
        while (((unsigned char)*src & 0xc0) == 0x80)
            ++src;

        dst[out++] = '?';
        ++chars;
    }

    dst[out] = '\0';
}


static void ui_format_time(
    char *dst,
    size_t dst_size,
    uint32_t seconds)
{
    snprintf(
        dst,
        dst_size,
        "%02u:%02u",
        (unsigned)(seconds / 60),
        (unsigned)(seconds % 60));
}



/* Ubuntu para interfaz; Bebas Neue exclusivamente para la marca. */

extern const unsigned char cd5_font_ubuntu_regular[];
extern const size_t cd5_font_ubuntu_regular_len;

extern const unsigned char cd5_font_ubuntu_bold[];
extern const size_t cd5_font_ubuntu_bold_len;

extern const unsigned char cd5_font_bebas[];
extern const size_t cd5_font_bebas_len;


enum {
    CD5_FONT_UBUNTU = 0,
    CD5_FONT_UBUNTU_BOLD,
    CD5_FONT_BEBAS
};


static stbtt_fontinfo g_font_ubuntu;
static stbtt_fontinfo g_font_ubuntu_bold;
static stbtt_fontinfo g_font_bebas;
static int g_fonts_ready = 0;


static void ui_fonts_init(void)
{
    if (g_fonts_ready)
        return;

    int a =
        stbtt_InitFont(
            &g_font_ubuntu,
            cd5_font_ubuntu_regular,
            stbtt_GetFontOffsetForIndex(
                cd5_font_ubuntu_regular,
                0));

    int b =
        stbtt_InitFont(
            &g_font_ubuntu_bold,
            cd5_font_ubuntu_bold,
            stbtt_GetFontOffsetForIndex(
                cd5_font_ubuntu_bold,
                0));

    int c =
        stbtt_InitFont(
            &g_font_bebas,
            cd5_font_bebas,
            stbtt_GetFontOffsetForIndex(
                cd5_font_bebas,
                0));

    g_fonts_ready =
        a && b && c;
}


static uint32_t ui_get_pixel(
    uint32_t *pixels,
    unsigned x,
    unsigned y)
{
    if (x >= UI_WIDTH ||
        y >= UI_HEIGHT)
        return 0;

    uint8_t *bytes =
        (uint8_t *)pixels;

    return
        *(uint32_t *)(
            bytes +
            ui_tiled_offset(x, y));
}


static void ui_blend_pixel(
    uint32_t *pixels,
    int x,
    int y,
    uint32_t color,
    unsigned alpha)
{
    if (x < 0 ||
        y < 0 ||
        x >= (int)UI_WIDTH ||
        y >= (int)UI_HEIGHT ||
        alpha == 0)
        return;

    if (alpha >= 255) {
        ui_pixel(
            pixels,
            (unsigned)x,
            (unsigned)y,
            color);
        return;
    }

    uint32_t dst =
        ui_get_pixel(
            pixels,
            (unsigned)x,
            (unsigned)y);

    unsigned sr = (color >> 16) & 0xff;
    unsigned sg = (color >> 8) & 0xff;
    unsigned sb = color & 0xff;

    unsigned dr = (dst >> 16) & 0xff;
    unsigned dg = (dst >> 8) & 0xff;
    unsigned db = dst & 0xff;

    unsigned inv = 255 - alpha;

    unsigned r =
        (sr * alpha + dr * inv) / 255;

    unsigned g =
        (sg * alpha + dg * inv) / 255;

    unsigned b =
        (sb * alpha + db * inv) / 255;

    ui_pixel(
        pixels,
        (unsigned)x,
        (unsigned)y,
        UINT32_C(0xff000000) |
        (r << 16) |
        (g << 8) |
        b);
}


static void ui_round_rect(
    uint32_t *pixels,
    unsigned x,
    unsigned y,
    unsigned w,
    unsigned h,
    unsigned radius,
    uint32_t color)
{
    if (!w || !h)
        return;

    if (radius * 2 > w)
        radius = w / 2;

    if (radius * 2 > h)
        radius = h / 2;

    ui_rect(
        pixels,
        x + radius,
        y,
        w - radius * 2,
        h,
        color);

    ui_rect(
        pixels,
        x,
        y + radius,
        w,
        h - radius * 2,
        color);

    ui_circle(
        pixels,
        (int)(x + radius),
        (int)(y + radius),
        (int)radius,
        color);

    ui_circle(
        pixels,
        (int)(x + w - radius - 1),
        (int)(y + radius),
        (int)radius,
        color);

    ui_circle(
        pixels,
        (int)(x + radius),
        (int)(y + h - radius - 1),
        (int)radius,
        color);

    ui_circle(
        pixels,
        (int)(x + w - radius - 1),
        (int)(y + h - radius - 1),
        (int)radius,
        color);
}


static uint32_t ui_utf8_next(
    const char **text)
{
    const unsigned char *s =
        (const unsigned char *)*text;

    if (!s[0])
        return 0;

    uint32_t cp;

    if (s[0] < 0x80) {

        cp = s[0];
        *text += 1;

    } else if ((s[0] & 0xe0) == 0xc0 &&
               s[1]) {

        cp =
            ((uint32_t)(s[0] & 0x1f) << 6) |
            (uint32_t)(s[1] & 0x3f);

        *text += 2;

    } else if ((s[0] & 0xf0) == 0xe0 &&
               s[1] &&
               s[2]) {

        cp =
            ((uint32_t)(s[0] & 0x0f) << 12) |
            ((uint32_t)(s[1] & 0x3f) << 6) |
            (uint32_t)(s[2] & 0x3f);

        *text += 3;

    } else if ((s[0] & 0xf8) == 0xf0 &&
               s[1] &&
               s[2] &&
               s[3]) {

        cp =
            ((uint32_t)(s[0] & 0x07) << 18) |
            ((uint32_t)(s[1] & 0x3f) << 12) |
            ((uint32_t)(s[2] & 0x3f) << 6) |
            (uint32_t)(s[3] & 0x3f);

        *text += 4;

    } else {

        cp = '?';
        *text += 1;
    }

    return cp;
}


static stbtt_fontinfo *ui_font(
    int id)
{
    if (id == CD5_FONT_BEBAS)
        return &g_font_bebas;

    if (id == CD5_FONT_UBUNTU_BOLD)
        return &g_font_ubuntu_bold;

    return &g_font_ubuntu;
}


static void ui_ttf_text(
    uint32_t *pixels,
    int font_id,
    int x,
    int y,
    float pixel_height,
    const char *text,
    uint32_t color,
    int max_width)
{
    if (!text || !text[0])
        return;

    ui_fonts_init();

    if (!g_fonts_ready)
        return;

    stbtt_fontinfo *font =
        ui_font(font_id);

    float scale =
        stbtt_ScaleForPixelHeight(
            font,
            pixel_height);

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;

    stbtt_GetFontVMetrics(
        font,
        &ascent,
        &descent,
        &line_gap);

    int baseline =
        y + (int)(ascent * scale);

    float pen =
        (float)x;

    uint32_t previous = 0;

    const char *cursor = text;

    while (*cursor) {

        uint32_t cp =
            ui_utf8_next(
                &cursor);

        if (!cp)
            break;

        if (previous) {

            pen +=
                stbtt_GetCodepointKernAdvance(
                    font,
                    (int)previous,
                    (int)cp) *
                scale;
        }

        int advance = 0;
        int lsb = 0;

        stbtt_GetCodepointHMetrics(
            font,
            (int)cp,
            &advance,
            &lsb);

        float advance_px =
            advance * scale;

        if (max_width > 0 &&
            pen + advance_px >
                (float)(x + max_width))
            break;

        int x0, y0, x1, y1;

        stbtt_GetCodepointBitmapBox(
            font,
            (int)cp,
            scale,
            scale,
            &x0,
            &y0,
            &x1,
            &y1);

        int gw = x1 - x0;
        int gh = y1 - y0;

        if (gw > 0 &&
            gh > 0 &&
            gw <= 128 &&
            gh <= 128) {

            unsigned char bitmap[128 * 128];

            memset(
                bitmap,
                0,
                sizeof(bitmap));

            stbtt_MakeCodepointBitmap(
                font,
                bitmap,
                gw,
                gh,
                gw,
                scale,
                scale,
                (int)cp);

            int gx =
                (int)pen + x0;

            int gy =
                baseline + y0;

            for (int yy = 0;
                 yy < gh;
                 ++yy) {

                for (int xx = 0;
                     xx < gw;
                     ++xx) {

                    unsigned a =
                        bitmap[
                            yy * gw + xx];

                    if (a) {

                        ui_blend_pixel(
                            pixels,
                            gx + xx,
                            gy + yy,
                            color,
                            a);
                    }
                }
            }
        }

        pen += advance_px;
        previous = cp;
    }
}


static void ui_cover_art(
    uint32_t *pixels,
    unsigned x,
    unsigned y,
    unsigned size)
{
    ui_round_rect(
        pixels, x - 8, y - 8,
        size + 16, size + 16,
        24, UINT32_C(0xff211b29)
    );

    pthread_mutex_lock(&g_cover_mutex);

    if (!g_cover_render_valid) {
        ui_round_rect(
            pixels, x, y, size, size,
            18, UINT32_C(0xff18141e)
        );
    } else {
        for (unsigned dy = 0; dy < size; ++dy) {
            unsigned sy = (unsigned)(
                (uint64_t)dy * CD5_COVER_RENDER_SIZE / size
            );

            for (unsigned dx = 0; dx < size; ++dx) {
                unsigned sx = (unsigned)(
                    (uint64_t)dx * CD5_COVER_RENDER_SIZE / size
                );

                ui_pixel(
                    pixels, x + dx, y + dy,
                    g_cover_render[
                        sy * CD5_COVER_RENDER_SIZE + sx
                    ]
                );
            }
        }
    }

    pthread_mutex_unlock(&g_cover_mutex);
}



static float ui_ttf_measure_width(
    int font_id,
    float pixel_height,
    const char *text)
{
    if (!text ||
        !text[0])
        return 0.0f;

    ui_fonts_init();

    if (!g_fonts_ready)
        return 0.0f;

    stbtt_fontinfo *font =
        ui_font(font_id);

    float scale =
        stbtt_ScaleForPixelHeight(
            font,
            pixel_height);

    float width = 0.0f;
    uint32_t previous = 0;

    const char *cursor = text;

    while (*cursor) {

        uint32_t cp =
            ui_utf8_next(
                &cursor);

        if (!cp)
            break;

        if (previous) {

            width +=
                stbtt_GetCodepointKernAdvance(
                    font,
                    (int)previous,
                    (int)cp) *
                scale;
        }

        int advance = 0;
        int lsb = 0;

        stbtt_GetCodepointHMetrics(
            font,
            (int)cp,
            &advance,
            &lsb);

        width +=
            advance * scale;

        previous = cp;
    }

    return width;
}


static int cd5_lyrics_active_line(const cd5_lyrics_t *lyrics,
    uint32_t elapsed_ms)
{
    if (!lyrics->available ||
        !lyrics->synced ||
        lyrics->count <= 0)
        return -1;

    int active = -1;

    for (int i = 0;
         i < lyrics->count;
         ++i) {

        if (lyrics->lines[i].time_ms <=
            elapsed_ms) {

            active = i;

        } else {

            break;
        }
    }

    return active;
}


static float cd5_lyrics_line_progress(const cd5_lyrics_t *lyrics,
    int active,
    uint32_t elapsed_ms)
{
    if (active < 0 ||
        active >= lyrics->count)
        return 0.0f;

    uint32_t start =
        lyrics->lines[
            active].time_ms;

    uint32_t end =
        start + 4500u;

    if (active + 1 <
        lyrics->count) {

        end =
            lyrics->lines[
                active + 1].time_ms;
    }

    if (end <= start)
        return 1.0f;

    if (elapsed_ms <= start)
        return 0.0f;

    if (elapsed_ms >= end)
        return 1.0f;

    return
        (float)(
            elapsed_ms - start) /
        (float)(
            end - start);
}


static float cd5_lyrics_scroll_phase(const cd5_lyrics_t *lyrics,
    int active,
    uint32_t elapsed_ms)
{
    if (active < 0 ||
        active + 1 >=
            lyrics->count)
        return 0.0f;

    uint32_t next =
        lyrics->lines[
            active + 1].time_ms;

    const uint32_t transition =
        420u;

    if (elapsed_ms +
            transition <=
        next)
        return 0.0f;

    if (elapsed_ms >= next)
        return 1.0f;

    float t =
        (float)(
            elapsed_ms -
            (next - transition)) /
        (float)transition;

    if (t < 0.0f)
        t = 0.0f;

    if (t > 1.0f)
        t = 1.0f;

    /*
     * smoothstep
     */
    return
        t * t *
        (3.0f - 2.0f * t);
}


static void ui_draw_frame(
    uint32_t *pixels)
{
    cd5_ui_snapshot snapshot;
    snapshot_disc(&snapshot);
    const uint32_t bg =
        UINT32_C(0xff0d0a13);

    const uint32_t panel =
        UINT32_C(0xff17121f);

    const uint32_t selected =
        UINT32_C(0xff251a31);

    const uint32_t purple =
        UINT32_C(0xff974fe0);

    const uint32_t purple_light =
        UINT32_C(0xffc28cff);

    const uint32_t purple_dark =
        UINT32_C(0xff6f2fb5);

    const uint32_t white =
        UINT32_C(0xfff6f2f8);

    const uint32_t muted =
        UINT32_C(0xffa69cad);

    const uint32_t dim =
        UINT32_C(0xff9a8fa3);

    const uint32_t lyric_past =
        UINT32_C(0xff82758d);

    const uint32_t lyric_active_base =
        UINT32_C(0xffa978ca);

    const uint32_t lyric_active =
        UINT32_C(0xfff0dcff);

    const uint32_t line =
        UINT32_C(0xff31273a);


    ui_rect(
        pixels,
        0, 0,
        UI_WIDTH,
        UI_HEIGHT,
        bg);


    /* Cabecera. */
    ui_ttf_text(
        pixels,
        CD5_FONT_BEBAS,
        64, 35,
        60.0f,
        "CD5",
        purple_light,
        150);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        158, 57,
        17.0f,
        CD5_TAGLINE,
        muted,
        460);

    /*
     * CD5 - version y creditos
     * Visibles tambien sin disco insertado.
     */
    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        1650, 56,
        17.0f,
        "v" CD5_VERSION,
        muted,
        220);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        64, 971,
        16.0f,
        "Made by " CD5_AUTHOR,
        UINT32_C(0xffb77af0),
        360);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        64, 1001,
        14.0f,
        CD5_GITHUB,
        UINT32_C(0xff82758d),
        620);


    if (snapshot.count > 0 && (snapshot.metadata_state != 1 || snapshot.cover_state != 1)) {
        const char *info = snapshot.metadata_state == 2 ? "No metadata found." :
            snapshot.metadata_state == -2 ? "Invalid metadata response." :
            snapshot.metadata_state != 1 ? "Metadata unavailable." :
            snapshot.cover_state == 2 ? "No cover found." :
            "Cover unavailable.";
        ui_ttf_text(pixels, CD5_FONT_UBUNTU, 850, 1001, 14.0f, info, muted, 950);
    }

    int state =
        g_ui_state;

    int count =
        snapshot.count;

    int index =
        g_ui_current_track;


    /* Estado sin disco reproducible. */
    if (state == UI_STATE_NO_DISC ||
        count <= 0) {

        ui_circle(
            pixels,
            960, 475,
            115,
            UINT32_C(0xff201629));

        ui_circle(
            pixels,
            960, 475,
            38,
            bg);

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU_BOLD,
            760, 650,
            34.0f,
            state == UI_STATE_LOADING ? "Reading disc" :
            state == UI_STATE_DISC_ERROR ? "Disc not supported" :
            state == UI_STATE_NO_DRIVE ? "Waiting for reader / disc" : "Insert an Audio CD",
            white,
            450);

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            760, 705,
            18.0f,
            state == UI_STATE_LOADING ? "Identifying release..." :
            state == UI_STATE_DISC_ERROR ? "X retries. Square closes CD5." :
            "Connect a reader and Audio CD. Square closes CD5.",
            muted,
            500);

        return;
    }


    if (index < 0 ||
        index >= count)
        index = 0;


    const audio_track_t *track =
        &snapshot.tracks[index];

    uint32_t start =
        track->start_lba;

    uint32_t end =
        track->end_lba;

    uint32_t now =
        g_ui_current_lba;

    if (state == UI_STATE_PLAYING) {
        uint64_t anchor = g_ui_lba_anchor_ms;
        uint64_t current = cd5_monotonic_ms();

        if (anchor && current > anchor) {
            uint64_t delta = current - anchor;

            if (delta > 300u)
                delta = 300u;

            now += (uint32_t)(delta * 75u / 1000u);
        }
    }

    if (now < start)
        now = start;

    if (now > end)
        now = end;

    uint32_t elapsed_ms =
        (uint32_t)(
            ((uint64_t)(
                now - start) *
             1000u) /
            75u);

    uint32_t elapsed =
        elapsed_ms /
        1000u;

    uint32_t duration =
        (end - start) /
        75u;


    char fallback_track[64];

    snprintf(
        fallback_track,
        sizeof(fallback_track),
        "Track %02u",
        (unsigned)track->number);

    const char *track_title =
        fallback_track;

    const char *album =
        "Audio CD";

    const char *artist =
        "";

    if (snapshot.metadata_state == 1) {

        if (index <
                snapshot.metadata.track_count &&
            snapshot.metadata.tracks[index][0]) {

            track_title =
                snapshot.metadata.tracks[index];
        }

        if (snapshot.metadata.album[0])
            album =
                snapshot.metadata.album;

        if (snapshot.metadata.artist[0])
            artist =
                snapshot.metadata.artist;
    }


    /* Columna izquierda: portada y reproductor. */

    const unsigned left_x =
        64;

    const unsigned cover_size =
        330;

    ui_cover_art(
        pixels,
        left_x,
        145,
        cover_size);


    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU_BOLD,
        left_x,
        510,
        29.0f,
        track_title,
        white,
        390);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU_BOLD,
        left_x,
        553,
        20.0f,
        artist,
        purple_light,
        390);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        left_x,
        586,
        17.0f,
        album,
        muted,
        390);


    char track_info[64];

    snprintf(
        track_info,
        sizeof(track_info),
        "Track %d of %d",
        index + 1,
        count);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        left_x,
        621,
        15.0f,
        track_info,
        dim,
        170);


    /*
     * Progress
     */
    const unsigned bar_x =
        left_x;

    const unsigned bar_y =
        674;

    const unsigned bar_w =
        360;

    ui_round_rect(
        pixels,
        bar_x,
        bar_y,
        bar_w,
        5,
        2,
        line);

    unsigned filled = 0;

    if (end > start) {

        filled =
            (unsigned)(
                ((uint64_t)(
                    now - start) *
                 bar_w) /
                (end - start));
    }

    if (filled > bar_w)
        filled = bar_w;

    if (filled) {

        ui_round_rect(
            pixels,
            bar_x,
            bar_y,
            filled,
            5,
            2,
            purple);
    }

    ui_circle(
        pixels,
        (int)(
            bar_x + filled),
        (int)(
            bar_y + 2),
        5,
        purple_light);


    char t1[32];
    char t2[32];

    ui_format_time(
        t1,
        sizeof(t1),
        elapsed);

    ui_format_time(
        t2,
        sizeof(t2),
        duration);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        bar_x,
        696,
        14.0f,
        t1,
        dim,
        60);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        bar_x + bar_w - 45,
        696,
        14.0f,
        t2,
        dim,
        60);


    /*
     * Controls
     */
    const int cy = 775;

    /*
     * Prev
     */
    ui_rect(
        pixels,
        110, cy - 17,
        5, 34,
        muted);

    for (unsigned i = 0;
         i < 30;
         ++i) {

        ui_rect(
            pixels,
            115 + i,
            cy - i / 2,
            1,
            i + 1,
            muted);
    }


    /*
     * Stop
     */
    ui_round_rect(
        pixels,
        179, cy - 17,
        34, 34,
        6,
        state == UI_STATE_STOPPED
            ? purple
            : muted);


    /*
     * Play/Pause
     */
    ui_circle(
        pixels,
        274, cy,
        43,
        purple);

    if (state == UI_STATE_PAUSED ||
        state == UI_STATE_STOPPED) {

        ui_play_triangle(
            pixels,
            281, cy,
            47,
            white);

    } else {

        ui_round_rect(
            pixels,
            259, cy - 18,
            10, 36,
            3,
            white);

        ui_round_rect(
            pixels,
            278, cy - 18,
            10, 36,
            3,
            white);
    }


    /*
     * Next
     */
    ui_rect(
        pixels,
        354, cy - 17,
        5, 34,
        muted);

    for (unsigned i = 0;
         i < 30;
         ++i) {

        ui_rect(
            pixels,
            324 + i,
            cy - 15 + i / 2,
            1,
            30 - i,
            muted);
    }


    /*
     * Hints compactos
     */
    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        left_x,
        856,
        13.0f,
        "X  Play/Pause    Square  Stop",
        dim,
        350);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        left_x,
        882,
        13.0f,
        "Left/Right  Track",
        dim,
        250);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        left_x,
        908,
        13.0f,
        "OPTIONS  Reload disc; play from track 1",
        dim,
        390);


    /* Columna central: letras. */

    const int lyrics_x =
        490;

    const int lyrics_w =
        790;

    const int lyrics_center_y =
        520;

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU_BOLD,
        lyrics_x,
        147,
        22.0f,
        "Lyrics",
        white,
        220);


    cd5_lyrics_t lyrics_snapshot;
    int lyrics_state;

    pthread_mutex_lock(&g_lyrics_mutex);

    lyrics_state = g_lyrics_state;
    memcpy(
        &lyrics_snapshot,
        &g_lyrics,
        sizeof(lyrics_snapshot)
    );

    pthread_mutex_unlock(&g_lyrics_mutex);


    if (lyrics_state == 1) {

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            lyrics_x,
            485,
            24.0f,
            "Loading lyrics...",
            dim,
            lyrics_w);

    } else if (lyrics_state == 3) {

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU_BOLD,
            lyrics_x,
            475,
            28.0f,
            "Instrumental",
            muted,
            lyrics_w);

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            lyrics_x,
            520,
            17.0f,
            "No lyrics for this track.",
            dim,
            lyrics_w);

    } else if (lyrics_state < 0 ||
               !lyrics_snapshot.available) {

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            lyrics_x,
            485,
            22.0f,
            lyrics_state == -2
                ? "Lyrics service unavailable"
                : "No lyrics available",
            dim,
            lyrics_w);

    } else if (!lyrics_snapshot.synced) {

        /*
         * Letras sin timestamps:
         * render estático centrado.
         */
        int visible =
            lyrics_snapshot.count;

        if (visible > 12)
            visible = 12;

        int y =
            250;

        for (int i = 0;
             i < visible;
             ++i) {

            ui_ttf_text(
                pixels,
                CD5_FONT_UBUNTU,
                lyrics_x,
                y,
                18.0f,
                lyrics_snapshot.lines[i].text,
                i == 0
                    ? muted
                    : dim,
                lyrics_w);

            y += 48;
        }

    } else {

        int active =
            cd5_lyrics_active_line(
                &lyrics_snapshot,
                elapsed_ms);

        if (active < 0)
            active = 0;

        float progress =
            cd5_lyrics_line_progress(
                &lyrics_snapshot,
                active,
                elapsed_ms);

        float scroll =
            cd5_lyrics_scroll_phase(
                &lyrics_snapshot,
                active,
                elapsed_ms);

        const int row_h = 66;

        int scroll_px =
            (int)(
                scroll *
                row_h);


        /*
         * 3 líneas anteriores + activa + 4 siguientes.
         */
        for (int rel = -3;
             rel <= 4;
             ++rel) {

            int li =
                active + rel;

            if (li < 0 ||
                li >=
                    lyrics_snapshot.count)
                continue;

            int y =
                lyrics_center_y +
                rel * row_h -
                scroll_px;

            const char *text =
                lyrics_snapshot.lines[
                    li].text;

            if (!text[0])
                continue;


            if (rel == 0) {

                /*
                 * Base de la frase activa.
                 */
                float font_size =
                    29.0f;

                float full_width =
                    ui_ttf_measure_width(
                        CD5_FONT_UBUNTU_BOLD,
                        font_size,
                        text);

                if (full_width >
                    lyrics_w)
                    font_size =
                        25.0f;

                full_width =
                    ui_ttf_measure_width(
                        CD5_FONT_UBUNTU_BOLD,
                        font_size,
                        text);


                ui_ttf_text(
                    pixels,
                    CD5_FONT_UBUNTU_BOLD,
                    lyrics_x,
                    y,
                    font_size,
                    text,
                    lyric_active_base,
                    lyrics_w);


                /*
                 * Karaoke:
                 *
                 * Pintamos otra vez la misma frase,
                 * pero limitando el ancho al progreso
                 * temporal hacia la siguiente línea.
                 *
                 * Las palabras/caracteres pasan de
                 * morado oscuro a claro de izquierda
                 * a derecha.
                 */
                int highlight_width =
                    (int)(
                        full_width *
                        progress);

                if (highlight_width >
                    lyrics_w)
                    highlight_width =
                        lyrics_w;

                if (highlight_width > 0) {

                    ui_ttf_text(
                        pixels,
                        CD5_FONT_UBUNTU_BOLD,
                        lyrics_x,
                        y,
                        font_size,
                        text,
                        lyric_active,
                        highlight_width);
                }

            } else {

                uint32_t c =
                    rel < 0
                        ? lyric_past
                        : dim;

                ui_ttf_text(
                    pixels,
                    CD5_FONT_UBUNTU,
                    lyrics_x,
                    y,
                    20.0f,
                    text,
                    c,
                    lyrics_w);
            }
        }
    }

    /* Columna derecha: pistas. */

    const unsigned list_x =
        1335;

    const unsigned list_y =
        135;

    const unsigned list_w =
        525;

    const unsigned list_h =
        830;

    ui_round_rect(
        pixels,
        list_x,
        list_y,
        list_w,
        list_h,
        22,
        panel);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU_BOLD,
        list_x + 30,
        list_y + 28,
        22.0f,
        "Tracks",
        white,
        180);

    ui_ttf_text(
        pixels,
        CD5_FONT_UBUNTU,
        list_x + 30,
        list_y + 62,
        14.0f,
        album,
        dim,
        430);


    int visible = 12;
    int first =
        index - 5;

    if (first < 0)
        first = 0;

    if (first + visible >
        count)
        first =
            count - visible;

    if (first < 0)
        first = 0;

    int last =
        first + visible;

    if (last > count)
        last = count;

    unsigned row_y =
        list_y + 108;


    for (int i = first;
         i < last;
         ++i) {

        const audio_track_t *t =
            &snapshot.tracks[i];

        uint32_t dur =
            (t->end_lba -
             t->start_lba) /
            75u;

        char number[16];
        char duration_text[32];
        char fallback[64];

        snprintf(
            number,
            sizeof(number),
            "%02d",
            i + 1);

        snprintf(
            fallback,
            sizeof(fallback),
            "Track %02u",
            (unsigned)t->number);

        const char *name =
            fallback;

        if (snapshot.metadata_state == 1 &&
            i < snapshot.metadata.track_count &&
            snapshot.metadata.tracks[i][0]) {

            name =
                snapshot.metadata.tracks[i];
        }

        ui_format_time(
            duration_text,
            sizeof(duration_text),
            dur);


        if (i == index) {

            ui_round_rect(
                pixels,
                list_x + 14,
                row_y - 10,
                list_w - 28,
                54,
                12,
                selected);

            ui_round_rect(
                pixels,
                list_x + 14,
                row_y - 10,
                4,
                54,
                2,
                purple);
        }


        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            list_x + 34,
            row_y,
            14.0f,
            number,
            i == index
                ? purple_light
                : dim,
            30);

        ui_ttf_text(
            pixels,
            i == index
                ? CD5_FONT_UBUNTU_BOLD
                : CD5_FONT_UBUNTU,
            list_x + 78,
            row_y,
            15.0f,
            name,
            i == index
                ? white
                : muted,
            330);

        ui_ttf_text(
            pixels,
            CD5_FONT_UBUNTU,
            list_x + 445,
            row_y,
            13.0f,
            duration_text,
            dim,
            55);

        row_y += 58;
    }


    /*
     * Divisores discretos entre las tres áreas.
     */
    ui_rect(
        pixels,
        447, 145,
        1, 795,
        line);

    ui_rect(
        pixels,
        1305, 145,
        1, 795,
        line);
}


static void *ui_thread_main(
    void *unused)
{
    (void)unused;

    int video =
        sceVideoOutOpen(
            0xff,
            1,
            0,
            NULL);

    if (video < 0) {
        notify("CDDA UI: VideoOutOpen FAILED");
        return NULL;
    }

    size_t pool_size =
        sceKernelGetDirectMemorySize();

    if (pool_size <
        UI_MEMORY_BYTES) {

        notify("CDDA UI: insufficient direct memory");
        return NULL;
    }

    int64_t physical = 0;

    int result =
        sceKernelAllocateDirectMemory(
            0,
            (int64_t)pool_size,
            UI_MEMORY_BYTES,
            UI_ALIGNMENT,
            UI_MEMORY_TYPE,
            &physical);

    if (result < 0) {
        notify("CDDA UI: direct memory allocation FAILED");
        return NULL;
    }

    void *mapped =
        (void *)UINT64_C(0x720000000);

    result =
        sceKernelMapDirectMemory(
            &mapped,
            UI_MEMORY_BYTES,
            UI_MAP_PROT,
            0,
            physical,
            UI_ALIGNMENT);

    if (result < 0) {
        notify("CDDA UI: direct memory map FAILED");
        return NULL;
    }

    uint8_t *frame0 =
        (uint8_t *)mapped;

    uint8_t *frame1 =
        frame0 +
        UI_FRAME_BYTES;

    ui_video_buffer_t buffers[2];

    memset(
        buffers,
        0,
        sizeof(buffers));

    buffers[0].data =
        frame0;

    buffers[1].data =
        frame1;

    ui_video_attribute_t attr;

    memset(
        &attr,
        0,
        sizeof(attr));

    (void)sceVideoOutSetFlipRate(
        video,
        0);

    sceVideoOutSetBufferAttribute2(
        &attr,
        UI_PIXEL_FORMAT,
        0,
        UI_WIDTH,
        UI_HEIGHT,
        0,
        0,
        0);

    result =
        sceVideoOutRegisterBuffers2(
            video,
            0,
            0,
            buffers,
            2,
            &attr,
            0,
            NULL);

    if (result < 0) {
        notify("CDDA UI: RegisterBuffers FAILED");
        return NULL;
    }


    int back = 0;

    /*
     * Cada flip lleva un identificador único.
     * El estado de VideoOut indica cuál se ha mostrado.
     */
    uint64_t submitted_serial = 0;

    for (;;) {

        /*
         * Con dos buffers esperamos la presentación
         * del último flip antes de dibujar el siguiente.
         *
         * Así el buffer anterior deja de estar en uso
         * por VideoOut antes de reutilizarlo.
         */
        if (submitted_serial != 0) {

            int presented = 0;

            for (int attempt = 0;
                 attempt < 120;
                 ++attempt) {

                uint64_t status[16] = {0};

                int result =
                    sceVideoOutGetFlipStatus(
                        video,
                        status);

                if (result == 0 &&
                    status[3] >= submitted_serial) {

                    presented = 1;
                    break;
                }

                /*
                 * Esperar al siguiente refresco.
                 * No ocupar CPU con polling continuo.
                 */
                if (sceVideoOutWaitVblank(video) < 0)
                    usleep(10000);
            }

            if (!presented) {

                /*
                 * Nunca sobrescribir un buffer
                 * si no sabemos que quedó libre.
                 */
                usleep(10000);
                continue;
            }
        }

        uint32_t *pixels =
            (uint32_t *)(
                back ? frame1 : frame0);

        ui_draw_frame(pixels);

        /*
         * 15 x 9 bloques de 64 KiB.
         */
        const size_t used =
            15UL * 9UL * 0x10000UL;

        ui_flush(pixels, used);

        uint64_t next_serial =
            submitted_serial + 1;

        int flip_result =
            sceVideoOutSubmitFlip(
                video,
                back,
                1,
                (int64_t)next_serial);

        if (flip_result < 0) {

            /*
             * No avanzar serial ni buffer.
             * Reintentar sin alterar el estado
             * de sincronización.
             */
            usleep(10000);
            continue;
        }

        submitted_serial = next_serial;
        back ^= 1;
    }

    return NULL;
}


static int ui_start(void)
{
    pthread_t thread;
    pthread_attr_t attr;

    if (pthread_attr_init(
            &attr) != 0) {

        notify("CDDA UI: pthread_attr_init FAILED");
        return 0;
    }

    (void)pthread_attr_setstacksize(
        &attr,
        512 * 1024);

    int r =
        pthread_create(
            &thread,
            &attr,
            ui_thread_main,
            NULL);

    (void)pthread_attr_destroy(
        &attr);

    if (r != 0) {
        notify("CDDA UI: pthread_create FAILED");
        return 0;
    }

    (void)pthread_detach(
        thread);

    return 1;
}


static int disc_is_ready(int pass_fd)
{
    uint8_t tur[6] = {0};

    return cam_command(
        pass_fd,
        tur,
        sizeof(tur),
        NULL,
        0,
        1000,
        0,
        NULL);
}


static int g_pad_handle = -1;
static uint32_t g_pad_previous = 0;

static _Atomic int g_stopped = 0;

enum {
    PLAYER_ACTION_NONE = 0,
    PLAYER_ACTION_PREV = -1,
    PLAYER_ACTION_NEXT = 1,
    PLAYER_ACTION_STOP = 2,
    PLAYER_ACTION_PLAY = 3,
    PLAYER_ACTION_DISC_CHANGED = 4,
    PLAYER_ACTION_AUDIO_ERROR = 5,
    PLAYER_ACTION_RELOAD = 6,
    PLAYER_ACTION_EXIT = 7
};

static void init_pad(void)
{
    int user_id = -1;

    /*
     * En una aplicación nativa UserService debe inicializarse
     * antes de resolver el usuario conectado.
     *
     * Puede devolver "already initialized"; no lo tratamos
     * como fatal y comprobamos GetInitialUser directamente.
     */
    (void)sceUserServiceInitialize(NULL);

    int ur = sceUserServiceGetInitialUser(&user_id);

    if (ur < 0) {
        notify("CD5: UserService FAILED - controls disabled");
        return;
    }

    int pr = scePadInit();

    /*
     * scePadInit puede devolver un estado de "ya inicializado".
     * Lo importante es comprobar si scePadOpen funciona.
     */
    (void)pr;

    g_pad_handle = scePadOpen(
        user_id,
        0,
        0,
        NULL);

    if (g_pad_handle < 0) {
        notify("CDDA: PadOpen FAILED - controls disabled");
        g_pad_handle = -1;
        return;
    }

}

static int poll_pad(
    const audio_track_t *track,
    int track_index,
    int track_count)
{
    if (g_pad_handle < 0)
        return PLAYER_ACTION_NONE;

    pad_data_t data;
    memset(&data, 0, sizeof(data));

    int ret = scePadReadState(
        g_pad_handle,
        &data);

    if (ret < 0)
        return PLAYER_ACTION_NONE;

    uint32_t buttons = data.buttons;

    /*
     * Solo eventos de pulsación, no repetimos mientras
     * el botón permanezca mantenido.
     */
    uint32_t pressed =
        buttons & ~g_pad_previous;

    g_pad_previous = buttons;

    if (pressed & PAD_BUTTON_OPTIONS) return PLAYER_ACTION_RELOAD;
    if (!track) {
        if (pressed & PAD_BUTTON_SQUARE) return PLAYER_ACTION_EXIT;
        return (pressed & PAD_BUTTON_CROSS) ? PLAYER_ACTION_RELOAD : PLAYER_ACTION_NONE;
    }

    if (pressed & PAD_BUTTON_SQUARE) {
        g_paused = 0;
        g_stopped = 1;
        g_ui_state = UI_STATE_STOPPED;
        return PLAYER_ACTION_STOP;
    }

    if (pressed & PAD_BUTTON_CROSS) {

        if (g_stopped) {
            g_stopped = 0;
            g_paused = 0;
            g_ui_state = UI_STATE_PLAYING;
            return PLAYER_ACTION_PLAY;
        }

        g_paused = !g_paused;

        g_ui_state =
            g_paused
                ? UI_STATE_PAUSED
                : UI_STATE_PLAYING;
    }

    /* OPTIONS explicitly reloads disc information and retries network work. */

    if (pressed & PAD_BUTTON_LEFT)
        return PLAYER_ACTION_PREV;

    if (pressed & PAD_BUTTON_RIGHT)
        return PLAYER_ACTION_NEXT;

    return PLAYER_ACTION_NONE;
}

/* A failed TUR keeps the existing media-change path. A ready drive with
 * a failed read stops until the user retries or selects another track.
 * Do not classify sense data or conceal unreadable sectors with silence. */
static int read_failure_action(int pass_fd)
{
    cd5_io_result failed = g_io;
    if (failed.kind == CD5_IO_MEDIA || failed.kind == CD5_IO_GONE || !disc_is_ready(pass_fd))
        return PLAYER_ACTION_DISC_CHANGED;

    char message[180];
    snprintf(message, sizeof(message),
             "CD5: CD read failed (CAM %u, sense %02x/%02x/%02x). X retries; left/right select a track.",
             failed.cam_status, failed.key, failed.asc, failed.ascq);
    notify(message);
    return PLAYER_ACTION_STOP;
}

/*
 * Envía un grain FLOAT_STEREO.
 *
 * Mientras estamos en pausa mandamos silencio para mantener
 * AudioOut vivo y seguimos leyendo el DualSense.
 *
 * Devuelve:
 *   A PLAYER_ACTION_* value, including a distinct AudioOut failure.
 */
static int submit_audio_grain(
    int audio_handle,
    int pass_fd,
    float *out,
    const audio_track_t *track,
    int track_index,
    int track_count)
{
    static float silence[AUDIO_GRAIN * CHANNELS]
        __attribute__((aligned(64)));

    int action = poll_pad(
        track,
        track_index,
        track_count);

    if (action != PLAYER_ACTION_NONE)
        return action;

    int pause_probe = 0;

    while (g_paused) {

        int ret =
            sceAudioOutOutput(
                audio_handle,
                silence);

        if (ret < 0)
            return PLAYER_ACTION_AUDIO_ERROR;

        action = poll_pad(
            track,
            track_index,
            track_count);

        if (action != PLAYER_ACTION_NONE)
            return action;

        /*
         * No martillamos el lector en cada grain.
         * Aproximadamente cada 100 ms comprobamos el medio.
         */
        if (++pause_probe >= 20) {
            pause_probe = 0;

            if (!disc_is_ready(pass_fd))
                return PLAYER_ACTION_DISC_CHANGED;
        }
    }

    int ret =
        sceAudioOutOutput(
            audio_handle,
            out);

    if (ret < 0)
        return PLAYER_ACTION_AUDIO_ERROR;

    return PLAYER_ACTION_NONE;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /*
     * El runtime nativo de PS5 no debe depender de que
     * PTHREAD_MUTEX_INITIALIZER tenga la misma representación
     * que en el host.
     */
    if (pthread_mutex_init(&g_snapshot_mutex, NULL) != 0 ||
        pthread_mutex_init(&g_cover_mutex, NULL) != 0 ||
        pthread_mutex_init(&g_lyrics_mutex, NULL) != 0) {

        notify("CD5: player mutex init FAILED");

        return EXIT_FAILURE;
    }

    (void)sceSystemServiceHideSplashScreen();


    /* AudioOut param 4: FLOAT_STEREO a 48000 Hz.
     * El CD entrega S16LE a 44100 Hz; convertimos y remuestreamos. */

    int audio_ret = sceAudioOutInit();

    if (audio_ret != 0) {
        notify("CDDA: AudioOutInit FAILED");

        return EXIT_FAILURE;
    }

    int audio_handle = sceAudioOutOpen(
        255,
        0,
        0,
        AUDIO_GRAIN,
        AUDIO_RATE,
        4);

    if (audio_handle < 0) {
        notify("CDDA: AudioOutOpen FAILED");

        return EXIT_FAILURE;
    }


    init_pad();

    /* Pedimos al Payload Manager residente que ejecute el helper por loopback. */

    g_ui_uid_before = (int)getuid();
    g_ui_euid_before = (int)geteuid();

    /*
     * IMPORTANTE:
     * Inicializamos SceNet/SceSsl/SceHttp2 antes de elevar el EUID.
     * Así el contexto TLS se crea todavía bajo las credenciales
     * normales de la aplicación.
     */
    int cd5_network_ready =
        (cd5_net_init() == 0);

    if (!trigger_local_unjail())
        notify("CDDA: automatic unjail request failed");

    /* Descubrimiento del lector y cambios de disco. */

    int pass_fd = -1;

    audio_track_t tracks[MAX_AUDIO_TRACKS];
    cd5_toc_t loaded_toc;

    int track_count = 0;
    int current_track = 0;
    int lyrics_requested_track = -1;

    /*
     * Arrancar VideoOut antes de esperar al disco.
     * Así CD5 puede mostrar NO_DISC y LOADING.
     */
    g_ui_state = UI_STATE_NO_DISC;

    if (!ui_start())
        notify("CDDA: UI unavailable - audio continues");

reload_disc:

    /*
     * Clear the old disc before closing and rediscovering its device.
     * The UI remains active while waiting for a reader or medium.
     */
    g_paused = 0;
    g_stopped = 0;

    lyrics_requested_track = -1;

    publish_disc(NULL, 0);
    g_ui_current_track = 0;
    g_ui_current_lba = 0;
    g_ui_lba_anchor_ms = 0;
    g_ui_state = UI_STATE_NO_DISC;

    cd5_metadata_clear(&g_metadata);
    g_metadata_state = 0;

    cd5_cover_render_clear();
    cd5_cover_clear(&g_cover);

    cd5_lyrics_cancel_and_clear();

    if (pass_fd >= 0) close(pass_fd);
    pass_fd = -1;
    for (;;) {
        pass_fd = open_usb_pass();
        if (pass_fd >= 0 && disc_is_ready(pass_fd)) break;
        if (pass_fd >= 0) close(pass_fd);
        pass_fd = -1;
        g_ui_state = UI_STATE_NO_DRIVE;
        if (poll_pad(NULL, 0, 0) == PLAYER_ACTION_EXIT) return EXIT_SUCCESS;
        usleep(1000000);
    }
    g_ui_uid_after = (int)getuid();
    g_ui_euid_after = (int)geteuid();

    g_ui_state = UI_STATE_LOADING;

    /*
     * Garantizar que el estado LOADING sea perceptible.
     * Sin esto la lectura de TOC puede terminar antes de
     * que VideoOut llegue a presentar un frame.
     */
    usleep(350000);

    memset(tracks, 0, sizeof(tracks));

    track_count = read_disc_toc(pass_fd, &loaded_toc) ? loaded_toc.count : 0;
    if (track_count > 0) memcpy(tracks, loaded_toc.audio, sizeof(tracks));

    /*
     * Puede haber un pequeño periodo de Unit Attention justo
     * después de insertar el medio, o puede no ser un Audio CD.
     * No matamos la aplicación: volvemos a intentarlo.
     */
    if (track_count <= 0) {
        g_ui_state = UI_STATE_DISC_ERROR;
        /* A malformed/data disc is not an automatic reload loop. X or
         * OPTIONS retries; removal returns to discovery. */
        while (disc_is_ready(pass_fd)) {
            int a = poll_pad(NULL, 0, 0);
            g_ui_state = UI_STATE_DISC_ERROR;
            if (a == PLAYER_ACTION_EXIT) { close(pass_fd); return EXIT_SUCCESS; }
            if (a == PLAYER_ACTION_RELOAD) break;
            usleep(250000);
        }
        goto reload_disc;
    }

    /* Identificación MusicBrainz en RAM, sin caché ni archivos temporales. */
    {
        cd5_disc_toc_t disc_toc = loaded_toc.disc;
        char disc_id[29];
        char toc_query[1024];
        char url[2048];

        g_ui_network_state = 0;
        g_ui_network_http = -1;
        g_ui_network_bytes = 0;

        if (cd5_discid_generate(
                &disc_toc,
                disc_id) &&
            cd5_discid_make_toc_query(
                &disc_toc,
                toc_query,
                sizeof(toc_query))) {

            snprintf(
                url,
                sizeof(url),
                "https://musicbrainz.org/ws/2/discid/%s"
                "?toc=%s"
                "&inc=recordings+artist-credits"
                "&cdstubs=no"
                "&fmt=json",
                disc_id,
                toc_query);

            g_ui_network_url_len =
                (uint32_t)strlen(url);

            if (!cd5_network_ready) cd5_network_ready = (cd5_net_init() == 0);
            if (!cd5_network_ready) {

                g_ui_network_state = -1;
                g_ui_network_http = -100;

            } else {

                char *body = NULL;
                size_t body_size = 0;

                int status =
                    cd5_http_get(
                        url,
                        &body,
                        &body_size);

                g_metadata_state = status == 404 ? 2 : -1;
                g_ui_network_http = status;
                g_ui_network_bytes =
                    (uint32_t)body_size;

                g_ui_network_state =
                    (status == 200)
                        ? 1
                        : -1;

                if (status == 200 &&
                    body &&
                    body_size > 0) {

                    int metadata_result = cd5_metadata_parse_musicbrainz(
                            body,
                            body_size,
                            disc_toc.last_track - disc_toc.first_track + 1,
                            &g_metadata);
                    if (metadata_result == 1) {
                        /* Compact metadata to the playable track list. */
                        for (int i = 0; i < track_count; ++i) {
                            int source = tracks[i].number - disc_toc.first_track;
                            if (source < g_metadata.track_count)
                                memmove(g_metadata.tracks[i], g_metadata.tracks[source], CD5_METADATA_TEXT_MAX);
                            else g_metadata.tracks[i][0] = '\0';
                        }
                        g_metadata.track_count = track_count;

                        g_metadata_state = 1;

                        /*
                         * Intentar portada real.
                         * Si no existe o falla la red,
                         * icono CD5 embebido.
                         */
                        if (!cd5_cover_fetch_release(
                                g_metadata.release_id,
                                &g_cover)) {

                            cd5_cover_load_fallback(
                                &g_cover);
                        }

                    } else {

                        g_metadata_state = metadata_result == 0 ? 2 : -2;

                        cd5_cover_load_fallback(
                            &g_cover);
                    }
                }

                free(body);
            }

        } else {

            g_ui_network_state = -1;
            g_ui_network_http = -101;
        }
    }

    cd5_toc_t current_toc;
    if (!disc_is_ready(pass_fd) || !read_disc_toc(pass_fd, &current_toc) ||
        memcmp(&current_toc, &loaded_toc, sizeof(current_toc)) != 0)
        goto reload_disc;

    if (!g_cover.valid)
        cd5_cover_load_fallback(&g_cover);

    cd5_cover_render_build();

    /*
     * Publicar TOC estable para la UI.
     */
    publish_disc(tracks, track_count);

    g_ui_current_track = 0;

    g_ui_current_lba =
        tracks[0].start_lba;

    current_track = 0;
    g_ui_state = UI_STATE_PLAYING;

    /* Reproducción. */

    static uint8_t readbuf[READ_BYTES];

    static float out[AUDIO_GRAIN * CHANNELS]
        __attribute__((aligned(64)));

    for (;;) {

        if (current_track < 0)
            current_track = 0;

        if (current_track >= track_count)
            current_track = 0;

        audio_track_t *track =
            &tracks[current_track];

        /*
         * Solicitar lyrics solo una vez al entrar en cada pista.
         * La petición HTTP corre en otro hilo.
         */
        if (lyrics_requested_track !=
            current_track) {

            uint32_t sectors =
                track->end_lba -
                track->start_lba;

            int duration_seconds =
                (int)(
                    (sectors + 37u) /
                    75u);

            cd5_lyrics_request(
                current_track,
                duration_seconds);

            lyrics_requested_track =
                current_track;
        }

        g_ui_current_track =
            current_track;

        g_ui_current_lba =
            track->start_lba;

        uint32_t lba =
            track->start_lba;

        uint32_t out_frames = 0;

        /* 44100/48000 = 147/160: next_out_num usa unidades de 1/160 de frame CD.
         * La posición entera evita acumular error flotante de reloj. */

        int have_previous = 0;

        float previous_l = 0.0f;
        float previous_r = 0.0f;

        uint64_t input_index = 0;
        uint64_t next_out_num = 0;

        int track_action =
            PLAYER_ACTION_NONE;

        while (lba < track->end_lba) {

            g_ui_current_lba =
                lba;

            g_ui_lba_anchor_ms = cd5_monotonic_ms();

            uint32_t sectors =
                READ_SECTORS;

            if (lba + sectors > track->end_lba)
                sectors =
                    track->end_lba - lba;

            if (!read_cdda_sectors(
                    pass_fd,
                    lba,
                    sectors,
                    readbuf)) {

                track_action = read_failure_action(pass_fd);

                break;
            }

            for (uint32_t sidx = 0;
                 sidx < sectors;
                 ++sidx) {

                uint8_t *sector =
                    &readbuf[
                        sidx *
                        CD_SECTOR_BYTES];

                for (uint32_t frame = 0;
                     frame < CD_FRAMES_SECTOR;
                     ++frame) {

                    uint32_t off =
                        frame * 4;

                    int16_t left_s16 =
                        read_le16(
                            &sector[off + 0]);

                    int16_t right_s16 =
                        read_le16(
                            &sector[off + 2]);

                    float current_l =
                        (float)left_s16 /
                        32768.0f;

                    float current_r =
                        (float)right_s16 /
                        32768.0f;

                    /*
                     * Primer sample del stream.
                     */
                    if (!have_previous) {

                        previous_l =
                            current_l;

                        previous_r =
                            current_r;

                        have_previous = 1;
                        input_index = 0;

                        out[
                            out_frames * 2 + 0] =
                            current_l;

                        out[
                            out_frames * 2 + 1] =
                            current_r;

                        ++out_frames;

                        /*
                         * Próximo punto de salida:
                         * 147/160 frames de entrada.
                         */
                        next_out_num = 147;

                        if (out_frames ==
                            AUDIO_GRAIN) {

                            int a =
                                submit_audio_grain(
                                    audio_handle,
                                    pass_fd,
                                    out,
                                    track,
                                    current_track,
                                    track_count);

                            out_frames = 0;

                            if (a !=
                                PLAYER_ACTION_NONE) {
                                track_action = a;
                                goto track_finished;
                            }
                        }

                        continue;
                    }

                    ++input_index;

                    /*
                     * Intervalo actual:
                     *
                     * previous = input_index - 1
                     * current  = input_index
                     *
                     * Coordenadas expresadas en /160.
                     */
                    uint64_t interval_start =
                        (input_index - 1) * 160ULL;

                    uint64_t interval_end =
                        input_index * 160ULL;

                    while (next_out_num <=
                           interval_end) {

                        float frac =
                            (float)(
                                next_out_num -
                                interval_start) /
                            160.0f;

                        float sample_l =
                            previous_l +
                            (current_l -
                             previous_l) *
                            frac;

                        float sample_r =
                            previous_r +
                            (current_r -
                             previous_r) *
                            frac;

                        out[
                            out_frames * 2 + 0] =
                            sample_l;

                        out[
                            out_frames * 2 + 1] =
                            sample_r;

                        ++out_frames;

                        next_out_num += 147ULL;

                        if (out_frames ==
                            AUDIO_GRAIN) {

                            int a =
                                submit_audio_grain(
                                    audio_handle,
                                    pass_fd,
                                    out,
                                    track,
                                    current_track,
                                    track_count);

                            out_frames = 0;

                            if (a !=
                                PLAYER_ACTION_NONE) {

                                track_action = a;

                                goto track_finished;
                            }
                        }
                    }

                    previous_l =
                        current_l;

                    previous_r =
                        current_r;
                }
            }

            lba += sectors;
        }

track_finished:

        /* Cambio o extracción de disco. */
        if (track_action == PLAYER_ACTION_RELOAD || track_action ==
            PLAYER_ACTION_DISC_CHANGED) {

            g_paused = 0;
            g_stopped = 0;
            g_ui_state = UI_STATE_NO_DISC;

            goto reload_disc;
        }

        /* STOP mantiene AudioOut con silencio; X reproduce desde el inicio.
         * Izquierda/derecha seleccionan otra pista sin reanudar. */
        if (track_action ==
            PLAYER_ACTION_STOP) {

            static float stop_silence[
                AUDIO_GRAIN * CHANNELS]
                __attribute__((aligned(64)));

            int stop_probe = 0;

            g_stopped = 1;
            g_paused = 0;
            g_ui_state = UI_STATE_STOPPED;

            g_ui_current_lba =
                tracks[current_track].start_lba;

            for (;;) {

                int ret =
                    sceAudioOutOutput(
                        audio_handle,
                        stop_silence);

                if (ret < 0) {
                    track_action = PLAYER_ACTION_AUDIO_ERROR;
                    break;
                }

                int a =
                    poll_pad(
                        &tracks[current_track],
                        current_track,
                        track_count);

                if (a == PLAYER_ACTION_RELOAD) {
                    track_action = a;
                    break;
                }
                if (a == PLAYER_ACTION_PLAY) {
                    g_stopped = 0;
                    g_paused = 0;
                    track_action =
                        PLAYER_ACTION_NONE;
                    break;
                }

                if (a == PLAYER_ACTION_PREV) {

                    --current_track;

                    if (current_track < 0)
                        current_track =
                            track_count - 1;

                    g_ui_current_track =
                        current_track;

                    g_ui_current_lba =
                        tracks[
                            current_track
                        ].start_lba;

                    continue;
                }

                if (a == PLAYER_ACTION_NEXT) {

                    ++current_track;

                    if (current_track >=
                        track_count)
                        current_track = 0;

                    g_ui_current_track =
                        current_track;

                    g_ui_current_lba =
                        tracks[
                            current_track
                        ].start_lba;

                    continue;
                }

                /*
                 * Un segundo cuadrado simplemente mantiene STOP.
                 */
                if (a == PLAYER_ACTION_STOP)
                    continue;

                /*
                 * Comprobar el medio aproximadamente
                 * cada 100 ms.
                 */
                if (++stop_probe >= 20) {

                    stop_probe = 0;

                    if (!disc_is_ready(
                            pass_fd)) {

                        track_action =
                            PLAYER_ACTION_DISC_CHANGED;
                        break;
                    }
                }
            }

            if (track_action == PLAYER_ACTION_RELOAD || track_action ==
                PLAYER_ACTION_DISC_CHANGED) {

                g_stopped = 0;
                goto reload_disc;
            }

            if (track_action == PLAYER_ACTION_AUDIO_ERROR) {
                notify("CD5: Audio output failed. Closing player; please relaunch.");
                close(pass_fd);
                return EXIT_FAILURE;
            }

            /*
             * PLAY desde STOP:
             * el bucle exterior vuelve a construir el resampler
             * desde track->start_lba.
             */
            g_stopped = 0;
            g_paused = 0;
            g_ui_state = UI_STATE_PLAYING;

            continue;
        }

        /*
         * Pista anterior / siguiente solicitada por el usuario.
         */
        if (track_action ==
            PLAYER_ACTION_PREV) {

            --current_track;

            if (current_track < 0)
                current_track =
                    track_count - 1;

            continue;
        }

        if (track_action ==
            PLAYER_ACTION_NEXT) {

            ++current_track;

            if (current_track >=
                track_count)
                current_track = 0;

            continue;
        }

        /*
         * AudioOut no disponible: terminar en vez de bloquear el proceso.
         */
        if (track_action == PLAYER_ACTION_AUDIO_ERROR) {
            notify("CD5: Audio output failed. Closing player; please relaunch.");
            close(pass_fd);
            return EXIT_FAILURE;
        }

        /*
         * Fin natural de pista:
         * avanzar automáticamente.
         */
        ++current_track;

        if (current_track >= track_count)
            current_track = 0;
    }
}
