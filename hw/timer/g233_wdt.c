#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define TYPE_G233_WDT "g233.wdt"
OBJECT_DECLARE_SIMPLE_TYPE(G233WDTState, G233_WDT)

#define G233_WDT_SIZE 0x1000

#define WDT_CTRL_OFF 0x00
#define WDT_LOAD_OFF 0x04
#define WDT_VAL_OFF  0x08
#define WDT_KEY_OFF  0x0C
#define WDT_SR_OFF   0x10

#define WDT_CTRL_EN    (1u << 0)
#define WDT_CTRL_INTEN (1u << 1)

#define WDT_KEY_FEED 0x5A5A5A5Au
#define WDT_KEY_LOCK 0x1ACCE551u

#define WDT_SR_TIMEOUT (1u << 0)

/* One counter tick per microsecond of virtual time. */
#define WDT_NS_PER_TICK 1000

struct G233WDTState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t load;
    uint32_t ctrl;
    uint32_t sr;
    bool locked;
    bool expired;
    int64_t ref_ns;
    uint32_t counter_ref;
    uint32_t frozen_val;
};

static void g233_wdt_update_irq(G233WDTState *s)
{
    qemu_set_irq(s->irq, (s->sr & WDT_SR_TIMEOUT) &&
                          (s->ctrl & WDT_CTRL_INTEN));
}

static uint32_t g233_wdt_compute_val(G233WDTState *s, int64_t now_ns)
{
    if (!(s->ctrl & WDT_CTRL_EN)) {
        return s->frozen_val;
    }
    if (s->expired) {
        return 0;
    }

    uint64_t elapsed = (uint64_t)(now_ns - s->ref_ns);
    uint64_t dec = elapsed / WDT_NS_PER_TICK;
    if (dec >= s->counter_ref) {
        s->expired = true;
        s->counter_ref = 0;
        s->sr |= WDT_SR_TIMEOUT;
        g233_wdt_update_irq(s);
        return 0;
    }
    return (uint32_t)(s->counter_ref - dec);
}

static void g233_wdt_restart_count(G233WDTState *s, int64_t now_ns)
{
    s->expired = false;
    s->ref_ns = now_ns;
    s->counter_ref = s->load;
}

static uint64_t g233_wdt_read(void *opaque, hwaddr addr, unsigned size)
{
    G233WDTState *s = opaque;
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case WDT_CTRL_OFF:
        return s->ctrl;
    case WDT_LOAD_OFF:
        return s->load;
    case WDT_VAL_OFF:
        return g233_wdt_compute_val(s, now_ns);
    case WDT_SR_OFF:
        (void)g233_wdt_compute_val(s, now_ns);
        return s->sr;
    default:
        return 0;
    }
}

static void g233_wdt_write(void *opaque, hwaddr addr, uint64_t value,
                           unsigned size)
{
    G233WDTState *s = opaque;
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t v = (uint32_t)value;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case WDT_CTRL_OFF:
        (void)g233_wdt_compute_val(s, now_ns);
        if (s->locked) {
            v |= WDT_CTRL_EN;
        }
        {
            bool was_en = !!(s->ctrl & WDT_CTRL_EN);
            bool now_en = !!(v & WDT_CTRL_EN);

            if (was_en && !now_en) {
                s->frozen_val = g233_wdt_compute_val(s, now_ns);
            }
            s->ctrl = v & (WDT_CTRL_EN | WDT_CTRL_INTEN);
            if (!was_en && now_en) {
                g233_wdt_restart_count(s, now_ns);
            }
        }
        g233_wdt_update_irq(s);
        break;
    case WDT_LOAD_OFF:
        s->load = v;
        break;
    case WDT_VAL_OFF:
        break;
    case WDT_KEY_OFF:
        if (v == WDT_KEY_FEED) {
            (void)g233_wdt_compute_val(s, now_ns);
            if (s->ctrl & WDT_CTRL_EN) {
                g233_wdt_restart_count(s, now_ns);
            }
        } else if (v == WDT_KEY_LOCK) {
            s->locked = true;
        }
        break;
    case WDT_SR_OFF:
        s->sr &= ~(v & WDT_SR_TIMEOUT);
        g233_wdt_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *s = G233_WDT(dev);

    s->load = 0;
    s->ctrl = 0;
    s->sr = 0;
    s->locked = false;
    s->expired = false;
    s->ref_ns = 0;
    s->counter_ref = 0;
    s->frozen_val = 0;
    g233_wdt_update_irq(s);
}

static void g233_wdt_init(Object *obj)
{
    G233WDTState *s = G233_WDT(obj);

    memory_region_init_io(&s->mmio, obj, &g233_wdt_ops, s, TYPE_G233_WDT,
                          G233_WDT_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, g233_wdt_reset);
}

static const TypeInfo g233_wdt_info = {
    .name = TYPE_G233_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)
