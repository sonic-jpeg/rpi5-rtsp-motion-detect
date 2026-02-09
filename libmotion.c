#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <arm_neon.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#define WIDTH 1280
#define HEIGHT 720
#define PIXELS (WIDTH * HEIGHT)

#define FRAME_HISTORY 10
#define ACTIVE_THRESHOLD 0.002

#define START_FRAMES 15
#define PRESTOP_LOW_MIN 10
#define PRESTOP_LOW_MAX 19
#define PRESTOP_LOW_FULL 20
#define PRESTOP_HIGH_MAX 20
#define CANCEL_PRESTOP_FRAMES 15
#define FULL_STOP_DELAY 4.0

typedef struct {
    uint8_t *prev_alpha;
    uint8_t *curr_alpha;

    double delta_history[FRAME_HISTORY];
    int hist_len;
    int hist_i;

    /* motion state */
    int motion_active;

    /* pre-stop tracking */
    int hi_run;
    int lo_run;
    int mid_hi_run;
    int mid_lo_seen;
    double prestop_ts;
    uint8_t *frame_buf;
    int8_t fd;
} motion_t;

/* ---------- wall clock ---------- */
static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static inline double delta_no_neon(const uint8_t *a, const uint8_t *b) {
    uint64_t sum = 0;

    for (size_t i = 0; i < PIXELS; i++) {
        sum += (uint64_t)abs((int)a[i] - (int)b[i]);
    }

    return (double)sum / (PIXELS * 255.0);
}

/* ---------- NEON delta ---------- */
static inline double delta_neon(const uint8_t *a, const uint8_t *b) {
    uint32x4_t acc = vdupq_n_u32(0);

    for (size_t i = 0; i < PIXELS; i += 16) {
        uint8x16_t va = vld1q_u8(a + i);
        uint8x16_t vb = vld1q_u8(b + i);
        uint8x16_t vd = vabdq_u8(va, vb);

        uint16x8_t lo = vmovl_u8(vget_low_u8(vd));
        uint16x8_t hi = vmovl_u8(vget_high_u8(vd));

        acc = vpadalq_u16(acc, lo);
        acc = vpadalq_u16(acc, hi);
    }

    uint64_t sum =
        (uint64_t)vgetq_lane_u32(acc, 0) +
        (uint64_t)vgetq_lane_u32(acc, 1) +
        (uint64_t)vgetq_lane_u32(acc, 2) +
        (uint64_t)vgetq_lane_u32(acc, 3);

    return (double)sum / (PIXELS * 255.0);
}

/* ---------- lifecycle ---------- */
motion_t *motion_new(int8_t fd) {
    motion_t *m = calloc(1, sizeof(motion_t));
    m->fd = fd;
    m->prev_alpha = aligned_alloc(16, PIXELS);
    m->curr_alpha = aligned_alloc(16, PIXELS);
    m->frame_buf = malloc(PIXELS);
    memset(m->prev_alpha, 0, PIXELS);
    return m;
}

void motion_free(motion_t *m) {
    if (!m) return;
    free(m->prev_alpha);
    free(m->curr_alpha);
    free(m->frame_buf);
    free(m);
}

/* ---------- feed motion frame ---------- */
/*
   returns:
     1  -> START
     0  -> KEEP
    -1  -> STOP
*/
int motion_feed(motion_t *m, const uint8_t *a) {
    memcpy(m->curr_alpha, a, PIXELS);
    double delta = delta_neon(m->curr_alpha, m->prev_alpha);
    memcpy(m->prev_alpha, m->curr_alpha, PIXELS);

    /* update delta history */
    m->delta_history[m->hist_i++ % FRAME_HISTORY] = delta;
    if (m->hist_len < FRAME_HISTORY) m->hist_len++;

    double avg = 0.0;
    for (int i = 0; i < m->hist_len; i++) avg += m->delta_history[i];
    avg /= m->hist_len;

//  printf("Frame %d delta: %.6f motion_active=%d\n",
//      m->hist_i, avg, m->motion_active);

    /* ---------- NOT ACTIVE (start logic) ---------- */
    if (!m->motion_active) {
        if (avg > ACTIVE_THRESHOLD) m->hi_run++;
        else m->hi_run = 0;

        if (m->hi_run >= START_FRAMES) {
            m->motion_active = 1;
            m->hi_run = m->lo_run = m->mid_hi_run = m->mid_lo_seen = 0;
            m->prestop_ts = 0.0;
            return 1; /* START */
        }
        return 0; /* KEEP */
    }

    /* ---------- RECORDING ---------- */
    if (m->prestop_ts == 0.0) {
        if (avg <= ACTIVE_THRESHOLD) {
            m->lo_run++;

            if (m->lo_run >= PRESTOP_LOW_MIN && m->lo_run <= PRESTOP_LOW_MAX)
                m->mid_lo_seen = 1;

            if (m->lo_run >= PRESTOP_LOW_FULL) {
                m->prestop_ts = now_sec();
            }
        } else {
            if (m->mid_lo_seen) {
                m->mid_hi_run++;
                if (m->mid_hi_run > PRESTOP_HIGH_MAX) {
                    m->mid_hi_run = 0;
                    m->mid_lo_seen = 0;
                }
            }
            m->lo_run = 0;
        }

        if (m->mid_lo_seen && m->mid_hi_run > 0 && avg <= ACTIVE_THRESHOLD) {
            m->lo_run++;
            if (m->lo_run >= PRESTOP_LOW_MIN) {
                m->prestop_ts = now_sec();
            }
        }

        return 0; /* KEEP */
    }

    /* ---------- PRESTOP (cancel) ---------- */
    if (avg > ACTIVE_THRESHOLD) {
        m->hi_run++;
        if (m->hi_run >= CANCEL_PRESTOP_FRAMES) {
            /* cancel prestop */
            m->prestop_ts = 0.0;
            m->hi_run = m->lo_run = m->mid_hi_run = m->mid_lo_seen = 0;
        }
        return 0; /* KEEP */
    }

    /* ---------- FULL STOP ---------- */
    if ((now_sec() - m->prestop_ts) >= FULL_STOP_DELAY) {
        m->motion_active = 0;
        m->prestop_ts = 0.0;
        m->hi_run = m->lo_run = m->mid_hi_run = m->mid_lo_seen = 0;
        return -1; /* STOP */
    }

    return 0; /* KEEP */
}

int motion_feed_next_frame(motion_t *m) {
    size_t received = 0;
    while (received < PIXELS) {
        ssize_t n = read(m->fd, m->frame_buf + received, PIXELS - received);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return 0; // keep on error
        } else if (n == 0) {
            return 0; // EOF -> keep
        }
        received += n;
    }

    return motion_feed(m, m->frame_buf);
}
