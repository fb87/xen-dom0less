// vim: tabstop=4 shiftwidth=4 expandtab colorcolumn=80 autoindent

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct buffer {
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
};

static int drm_fd;
static uint32_t conn_id, crtc_id;
static drmModeModeInfo mode;
static struct buffer bufs[2];
static int front = 0;

static void die(const char *msg)
{
    perror(msg);
    exit(1);
}

/* ---------- Display setup ---------- */

static void find_display()
{
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) die("drmModeGetResources");

    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *conn =
            drmModeGetConnector(drm_fd, res->connectors[i]);
        if (!conn) continue;

        if (conn->connection == DRM_MODE_CONNECTED &&
                conn->count_modes > 0) {

            conn_id = conn->connector_id;
            mode = conn->modes[0];

            drmModeEncoder *enc = NULL;
            if (conn->encoder_id)
                enc = drmModeGetEncoder(drm_fd, conn->encoder_id);

            if (!enc) {
                drmModeRes *r = drmModeGetResources(drm_fd);
                for (int j = 0; j < r->count_encoders; j++) {
                    enc = drmModeGetEncoder(drm_fd, r->encoders[j]);
                    if (enc) break;
                }
                drmModeFreeResources(r);
            }

            if (!enc) die("No encoder");
            crtc_id = enc->crtc_id;

            drmModeFreeEncoder(enc);
            drmModeFreeConnector(conn);
            drmModeFreeResources(res);
            return;
        }

        drmModeFreeConnector(conn);
    }

    drmModeFreeResources(res);
    die("No connected display");
}

/* ---------- Dumb buffer ---------- */

static void create_buffer(struct buffer *buf, int w, int h)
{
    struct drm_mode_create_dumb creq = {0};
    creq.width = w;
    creq.height = h;
    creq.bpp = 32;

    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0)
        die("CREATE_DUMB");

    buf->handle = creq.handle;
    buf->pitch = creq.pitch;
    buf->size = creq.size;

    if (drmModeAddFB(drm_fd, w, h, 24, 32,
                buf->pitch, buf->handle, &buf->fb_id))
        die("AddFB");

    struct drm_mode_map_dumb mreq = {0};
    mreq.handle = buf->handle;

    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq))
        die("MAP_DUMB");

    buf->map = mmap(NULL, buf->size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED, drm_fd, mreq.offset);

    if (buf->map == MAP_FAILED)
        die("mmap");

    memset(buf->map, 0, buf->size);
}

/* ---------- Drawing helpers ---------- */

static inline void putpixel(struct buffer *buf, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 ||
            x >= mode.hdisplay || y >= mode.vdisplay)
        return;

    uint32_t *p = (uint32_t *)(buf->map + y * buf->pitch);
    p[x] = color;
}

static void clear(struct buffer *buf, uint32_t color)
{
    for (int y = 0; y < mode.vdisplay; y++) {
        uint32_t *row = (uint32_t *)(buf->map + y * buf->pitch);
        for (int x = 0; x < mode.hdisplay; x++)
            row[x] = color;
    }
}

static void line(struct buffer *buf,
        int x0, int y0,
        int x1, int y1,
        uint32_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        putpixel(buf, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void circle(struct buffer *buf,
        int cx, int cy, int r,
        uint32_t color)
{
    for (int a = 0; a < 360; a++) {
        float rad = a * M_PI / 180.0f;
        int x = cx + cosf(rad) * r;
        int y = cy + sinf(rad) * r;
        putpixel(buf, x, y, color);
    }
}

/* ---------- Meter ---------- */

static void draw_meter(struct buffer *buf, float value)
{
    int cx = mode.hdisplay / 2;
    int cy = mode.vdisplay / 2;
    int r  = mode.vdisplay / 3;

    clear(buf, 0x000000);

    /* outer circle */
    circle(buf, cx, cy, r, 0x00FFFFFF);

    /* tick marks */
    for (int i = 0; i <= 10; i++) {
        float angle = (-140 + i * 28) * M_PI / 180.0f;
        int x0 = cx + cosf(angle) * (r - 10);
        int y0 = cy + sinf(angle) * (r - 10);
        int x1 = cx + cosf(angle) * r;
        int y1 = cy + sinf(angle) * r;
        line(buf, x0, y0, x1, y1, 0x00FFFFFF);
    }

    /* needle */
    float angle = (-140 + value * 280) * M_PI / 180.0f;
    int nx = cx + cosf(angle) * (r - 20);
    int ny = cy + sinf(angle) * (r - 20);
    line(buf, cx, cy, nx, ny, 0x00FF0000);
}

/* ---------- Page flip ---------- */

static volatile int waiting = 0;

static void flip_handler(int fd,
        unsigned int frame,
        unsigned int sec,
        unsigned int usec,
        void *data)
{
    waiting = 0;
}

static void wait_flip()
{
    drmEventContext ev = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .page_flip_handler = flip_handler,
    };

    while (waiting) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(drm_fd, &fds);
        select(drm_fd + 1, &fds, NULL, NULL, NULL);
        drmHandleEvent(drm_fd, &ev);
    }
}

/* ---------- Main ---------- */

int main()
{
    drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd < 0) die("open drm");

    find_display();

    int w = mode.hdisplay;
    int h = mode.vdisplay;

    create_buffer(&bufs[0], w, h);
    create_buffer(&bufs[1], w, h);

    if (drmModeSetCrtc(drm_fd, crtc_id,
                bufs[0].fb_id,
                0, 0,
                &conn_id, 1, &mode))
        die("SetCrtc");

    /* Slow smooth motion */
    float t = 0.0f;

    while (1) {
        int back = front ^ 1;

        float value = (sinf(t) + 1.0f) * 0.5f;
        draw_meter(&bufs[back], value);

        t += 0.02f;   /* smaller = slower */

        waiting = 1;
        if (drmModePageFlip(drm_fd, crtc_id,
                    bufs[back].fb_id,
                    DRM_MODE_PAGE_FLIP_EVENT,
                    NULL))
            die("PageFlip");

        wait_flip();
        front = back;
    }

    return 0;
}

