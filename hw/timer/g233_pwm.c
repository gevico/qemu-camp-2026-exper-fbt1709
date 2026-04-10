#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define TYPE_G233_PWM "g233.pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

#define G233_PWM_CHANS 4
#define G233_PWM_SIZE  0x1000

#define PWM_GLB_OFF        0x00
#define PWM_CH_BASE(ch)    (0x10 + (ch) * 0x10)
#define PWM_CH_CTRL(ch)    (PWM_CH_BASE(ch) + 0x00)
#define PWM_CH_PERIOD(ch)  (PWM_CH_BASE(ch) + 0x04)
#define PWM_CH_DUTY(ch)    (PWM_CH_BASE(ch) + 0x08)
#define PWM_CH_CNT(ch)     (PWM_CH_BASE(ch) + 0x0C)

#define PWM_CTRL_EN  (1u << 0)
#define PWM_CTRL_POL (1u << 1)

typedef struct {
    uint32_t ctrl;
    uint32_t period;
    uint32_t duty;
    uint32_t latched_cnt;
    int64_t start_ns;
    bool done;
    bool done_armed;
} G233PWMChan;

struct G233PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    G233PWMChan ch[G233_PWM_CHANS];
};

static uint32_t g233_pwm_chan_cnt(G233PWMChan *c, int64_t now_ns)
{
    if (c->ctrl & PWM_CTRL_EN) {
        uint64_t delta = (now_ns > c->start_ns) ? (uint64_t)(now_ns - c->start_ns) : 0;
        uint64_t cnt64 = (uint64_t)c->latched_cnt + delta;
        if (c->done_armed && c->period != 0 && cnt64 >= c->period) {
            c->done = true;
            c->done_armed = false;
        }
        return (uint32_t)cnt64;
    }
    return c->latched_cnt;
}

static uint32_t g233_pwm_glb(G233PWMState *s, int64_t now_ns)
{
    uint32_t glb = 0;
    for (int i = 0; i < G233_PWM_CHANS; i++) {
        /* Update DONE by elapsed virtual time even if CNT wasn't read. */
        (void)g233_pwm_chan_cnt(&s->ch[i], now_ns);
        if (s->ch[i].ctrl & PWM_CTRL_EN) {
            glb |= (1u << i);
        }
        if (s->ch[i].done) {
            glb |= (1u << (4 + i));
        }
    }
    return glb;
}

static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned size)
{
    G233PWMState *s = opaque;
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int ch;
    hwaddr off;

    if (size != 4) {
        return 0;
    }
    if (addr == PWM_GLB_OFF) {
        return g233_pwm_glb(s, now_ns);
    }

    if (addr >= PWM_CH_BASE(0) && addr < PWM_CH_BASE(G233_PWM_CHANS)) {
        ch = (addr - PWM_CH_BASE(0)) / 0x10;
        off = (addr - PWM_CH_BASE(0)) % 0x10;
        switch (off) {
        case 0x00:
            return s->ch[ch].ctrl;
        case 0x04:
            return s->ch[ch].period;
        case 0x08:
            return s->ch[ch].duty;
        case 0x0C:
            return g233_pwm_chan_cnt(&s->ch[ch], now_ns);
        }
    }
    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    G233PWMState *s = opaque;
    uint32_t v = (uint32_t)value;
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int ch;
    hwaddr off;

    if (size != 4) {
        return;
    }
    if (addr == PWM_GLB_OFF) {
        for (int i = 0; i < G233_PWM_CHANS; i++) {
            if (v & (1u << (4 + i))) {
                s->ch[i].done = false; /* W1C DONE */
                s->ch[i].done_armed = false;
            }
        }
        return;
    }

    if (addr >= PWM_CH_BASE(0) && addr < PWM_CH_BASE(G233_PWM_CHANS)) {
        ch = (addr - PWM_CH_BASE(0)) / 0x10;
        off = (addr - PWM_CH_BASE(0)) % 0x10;
        G233PWMChan *c = &s->ch[ch];
        switch (off) {
        case 0x00: {
            bool was_en = !!(c->ctrl & PWM_CTRL_EN);
            bool now_en = !!(v & PWM_CTRL_EN);
            c->ctrl = v & (PWM_CTRL_EN | PWM_CTRL_POL);
            if (!was_en && now_en) {
                c->latched_cnt = 0;
                c->start_ns = now_ns;
                c->done = false;
                c->done_armed = true;
            } else if (was_en && !now_en) {
                c->latched_cnt = g233_pwm_chan_cnt(c, now_ns);
            }
            return;
        }
        case 0x04:
            c->period = v;
            return;
        case 0x08:
            c->duty = v;
            return;
        case 0x0C:
            /* read-only */
            return;
        }
    }
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_pwm_reset(DeviceState *dev)
{
    G233PWMState *s = G233_PWM(dev);
    memset(s->ch, 0, sizeof(s->ch));
}

static void g233_pwm_init(Object *obj)
{
    G233PWMState *s = G233_PWM(obj);
    memory_region_init_io(&s->mmio, obj, &g233_pwm_ops, s, TYPE_G233_PWM, G233_PWM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_pwm_reset);
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)
