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
#include <sys/socket.h>
#include <netinet/in.h>


/* ---- Pad / UserService minimal ABI ---- */

extern int sceUserServiceGetInitialUser(int *user_id);

extern int scePadInit(void);
extern int scePadOpen(int user_id, int type, int index, void *param);
extern int scePadReadState(int handle, void *data);

#define PAD_BUTTON_OPTIONS   0x0008u
#define PAD_BUTTON_RIGHT     0x0020u
#define PAD_BUTTON_LEFT      0x0080u
#define PAD_BUTTON_CROSS     0x4000u

/*
 * Buffer deliberately oversized.
 * buttons is at offset 0 in the known Pad ABI.
 * We only consume buttons here.
 */
typedef struct {
    uint32_t buttons;
    uint8_t rest[508];
} pad_data_t;

#include <stddef.h>
#include <string.h>
#include <unistd.h>

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

    if (ioctl(fd, CAMIOCOMMAND, &c) != 0)
        return 0;

    if ((c.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP)
        return 0;

    uint32_t resid = c.csio.resid;
    uint32_t got = resid <= len ? len - resid : 0;

    if (actual)
        *actual = got;

    if (len && resid && !allow_short)
        return 0;

    return 1;
}

static int open_usb_pass(void)
{
    int fd = open("/dev/pass1", O_RDWR);

    if (fd >= 0)
        return fd;

    fd = open("/dev/pass1", O_RDONLY);

    if (fd >= 0)
        return fd;

    return -1;
}

#define MAX_AUDIO_TRACKS 99

typedef struct {
    uint8_t number;
    uint32_t start_lba;
    uint32_t end_lba;
} audio_track_t;

static int read_audio_tracks(
    int pass_fd,
    audio_track_t *tracks,
    int max_tracks)
{
    uint8_t toc_cdb[10] = {
        0x43, 0, 0, 0, 0,
        0, 0, 0x03, 0x24, 0
    };

    uint8_t toc[804];
    memset(toc, 0, sizeof(toc));

    uint32_t got = 0;

    if (!cam_command(
            pass_fd,
            toc_cdb,
            sizeof(toc_cdb),
            toc,
            sizeof(toc),
            10000,
            1,
            &got))
        return -1;

    if (got < 12)
        return -1;

    uint32_t toc_total = (uint32_t)be16(toc) + 2;

    if (toc_total > got)
        toc_total = got;

    int count = 0;
    int pending_audio = -1;

    for (uint32_t off = 4;
         off + 8 <= toc_total;
         off += 8) {

        uint8_t control = toc[off + 1] & 0x0f;
        uint8_t track = toc[off + 2];
        uint32_t lba = be32(&toc[off + 4]);

        /*
         * Cualquier descriptor posterior cierra la pista
         * de audio anterior, incluido el lead-out 0xAA.
         */
        if (pending_audio >= 0) {
            tracks[pending_audio].end_lba = lba;
            pending_audio = -1;
        }

        if (track == 0xAA)
            continue;

        /*
         * control bit 2:
         *   0 = audio
         *   1 = data
         */
        if ((control & 0x04) == 0) {
            if (count >= max_tracks)
                break;

            tracks[count].number = track;
            tracks[count].start_lba = lba;
            tracks[count].end_lba = 0;

            pending_audio = count;
            ++count;
        }
    }

    /*
     * Todas las pistas válidas deben haber quedado cerradas
     * por el descriptor siguiente o por el lead-out.
     */
    int valid = 0;

    for (int i = 0; i < count; ++i) {
        if (tracks[i].end_lba > tracks[i].start_lba) {
            if (valid != i)
                tracks[valid] = tracks[i];

            ++valid;
        }
    }

    return valid;
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

    if (!cam_command(
            pass_fd,
            read_cdb,
            sizeof(read_cdb),
            buffer,
            bytes,
            20000,
            0,
            &received))
        return 0;

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

    notify("CDDA: autounjail requested");

    return 1;
}

static int g_pad_handle = -1;
static uint32_t g_pad_previous = 0;
static int g_paused = 0;

enum {
    PLAYER_ACTION_NONE = 0,
    PLAYER_ACTION_PREV = -1,
    PLAYER_ACTION_NEXT = 1
};

static void notify_track(
    const audio_track_t *track,
    int index,
    int total)
{
    char msg[160];

    snprintf(
        msg,
        sizeof(msg),
        "CDDA: Track %u  (%d/%d)%s",
        (unsigned)track->number,
        index + 1,
        total,
        g_paused ? "  PAUSED" : "");

    notify(msg);
}

static void init_pad(void)
{
    int user_id = -1;

    int ur = sceUserServiceGetInitialUser(&user_id);

    if (ur < 0) {
        notify("CDDA: UserService unavailable - Pad disabled");
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

    notify("CDDA: DualSense controls ready");
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

    if (pressed & PAD_BUTTON_CROSS) {
        g_paused = !g_paused;

        notify(g_paused
            ? "CDDA: PAUSED"
            : "CDDA: PLAY");
    }

    if (pressed & PAD_BUTTON_OPTIONS) {
        notify_track(
            track,
            track_index,
            track_count);
    }

    if (pressed & PAD_BUTTON_LEFT)
        return PLAYER_ACTION_PREV;

    if (pressed & PAD_BUTTON_RIGHT)
        return PLAYER_ACTION_NEXT;

    return PLAYER_ACTION_NONE;
}

/*
 * Envía un grain FLOAT_STEREO.
 *
 * Mientras estamos en pausa mandamos silencio para mantener
 * AudioOut vivo y seguimos leyendo el DualSense.
 *
 * Devuelve:
 *   0  = continuar
 *  -1  = pista anterior
 *  +1  = pista siguiente
 */
static int submit_audio_grain(
    int audio_handle,
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

    while (g_paused) {

        int ret =
            sceAudioOutOutput(
                audio_handle,
                silence);

        if (ret < 0)
            return 99;

        action = poll_pad(
            track,
            track_index,
            track_count);

        if (action != PLAYER_ACTION_NONE)
            return action;
    }

    int ret =
        sceAudioOutOutput(
            audio_handle,
            out);

    if (ret < 0)
        return 99;

    return PLAYER_ACTION_NONE;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    (void)sceSystemServiceHideSplashScreen();

    notify("CDDA Player v1 starting");

    /*
     * ------------------------------------------------------------
     * AUDIO OUT
     * ------------------------------------------------------------
     *
     * param = 4:
     * FLOAT_STEREO.
     *
     * El CD entrega S16LE 44100 Hz.
     * Nosotros convertimos a float y remuestreamos a 48000 Hz.
     */

    int audio_ret = sceAudioOutInit();

    if (audio_ret != 0) {
        notify("CDDA: AudioOutInit FAILED");

        for (;;)
            __asm__ volatile("pause");
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

        for (;;)
            __asm__ volatile("pause");
    }

    notify("CDDA: AudioOut FLOAT_STEREO OK");

    /*
     * ------------------------------------------------------------
     * DUALSENSE
     * ------------------------------------------------------------
     */

    init_pad();

    /*
     * ------------------------------------------------------------
     * AUTO-UNJAIL
     * ------------------------------------------------------------
     *
     * Payload Manager ya está residente en la consola.
     * Pedimos por loopback que ejecute el helper autónomo.
     */
    notify("CDDA: requesting automatic unjail");

    if (!trigger_local_unjail())
        notify("CDDA: automatic unjail request failed");

    /*
     * ------------------------------------------------------------
     * ESPERAR ACCESO AL LECTOR
     * ------------------------------------------------------------
     */

    int pass_fd = -1;

    notify("CDDA: waiting for unjail / optical drive");

    while (pass_fd < 0) {
        pass_fd = open_usb_pass();

        if (pass_fd < 0)
            usleep(100000);
    }

    notify("CDDA: optical drive access OK");

    /*
     * TEST UNIT READY
     */

    uint8_t tur[6] = {0};

    if (!cam_command(
            pass_fd,
            tur,
            sizeof(tur),
            NULL,
            0,
            5000,
            0,
            NULL)) {

        notify("CDDA: TEST UNIT READY FAILED");
        close(pass_fd);

        for (;;)
            __asm__ volatile("pause");
    }

    /*
     * ------------------------------------------------------------
     * TOC COMPLETO
     * ------------------------------------------------------------
     */

    audio_track_t tracks[MAX_AUDIO_TRACKS];
    memset(tracks, 0, sizeof(tracks));

    int track_count =
        read_audio_tracks(
            pass_fd,
            tracks,
            MAX_AUDIO_TRACKS);

    if (track_count <= 0) {
        notify("CDDA: no audio tracks found");
        close(pass_fd);

        for (;;)
            __asm__ volatile("pause");
    }

    {
        char msg[128];

        snprintf(
            msg,
            sizeof(msg),
            "CDDA: %d audio track(s) found",
            track_count);

        notify(msg);
    }

    /*
     * ------------------------------------------------------------
     * REPRODUCTOR
     * ------------------------------------------------------------
     */

    static uint8_t readbuf[READ_BYTES];

    static float out[AUDIO_GRAIN * CHANNELS]
        __attribute__((aligned(64)));

    int current_track = 0;

    for (;;) {

        if (current_track < 0)
            current_track = 0;

        if (current_track >= track_count)
            current_track = 0;

        audio_track_t *track =
            &tracks[current_track];

        notify_track(
            track,
            current_track,
            track_count);

        uint32_t lba =
            track->start_lba;

        uint32_t out_frames = 0;

        /*
         * --------------------------------------------------------
         * Resampler racional:
         *
         * 44100 / 48000 = 147 / 160
         *
         * next_out_num representa la posición del siguiente
         * sample de salida en unidades de 1/160 de frame CD.
         *
         * De esta forma no acumulamos error flotante de reloj.
         * --------------------------------------------------------
         */

        int have_previous = 0;

        float previous_l = 0.0f;
        float previous_r = 0.0f;

        uint64_t input_index = 0;
        uint64_t next_out_num = 0;

        int track_action =
            PLAYER_ACTION_NONE;

        while (lba < track->end_lba) {

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

                notify("CDDA: READ CD FAILED");
                track_action = 99;
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
         * Error grave de AudioOut / CD.
         */
        if (track_action == 99) {
            close(pass_fd);

            for (;;)
                __asm__ volatile("pause");
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
