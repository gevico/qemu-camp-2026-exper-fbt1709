#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_G233_GPIO "g233.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(G233GPIOState, G233_GPIO)

#define G233_GPIO_SIZE   0x1000

#define GPIO_DIR_OFF     0x00
#define GPIO_OUT_OFF     0x04
#define GPIO_IN_OFF      0x08
#define GPIO_IE_OFF      0x0C
#define GPIO_IS_OFF      0x10
#define GPIO_TRIG_OFF    0x14
#define GPIO_POL_OFF     0x18

struct G233GPIOState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t dir;
    uint32_t out;
    uint32_t in;
    uint32_t ie;
    uint32_t is;
    uint32_t trig;
    uint32_t pol;
    uint32_t last_in;
};

static void g233_gpio_update_irq(G233GPIOState *s)
{
    qemu_set_irq(s->irq, s->is != 0);
}

static void g233_gpio_recompute(G233GPIOState *s)
{
    uint32_t new_in = s->out & s->dir;
    uint32_t edge_mask = ~s->trig;
    uint32_t level_mask = s->trig;
    uint32_t rise = (~s->last_in) & new_in;
    uint32_t fall = s->last_in & (~new_in);
    uint32_t edge_set;
    uint32_t level_active;

    edge_set = ((rise & s->pol) | (fall & ~s->pol)) & edge_mask & s->ie;
    s->is |= edge_set;

    level_active = ((new_in & s->pol) | ((~new_in) & ~s->pol)) & level_mask & s->ie;
    s->is = (s->is & ~level_mask) | level_active;

    s->in = new_in;
    s->last_in = new_in;
    g233_gpio_update_irq(s);
}

static uint64_t g233_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    G233GPIOState *s = opaque;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case GPIO_DIR_OFF:
        return s->dir;
    case GPIO_OUT_OFF:
        return s->out;
    case GPIO_IN_OFF:
        return s->in;
    case GPIO_IE_OFF:
        return s->ie;
    case GPIO_IS_OFF:
        return s->is;
    case GPIO_TRIG_OFF:
        return s->trig;
    case GPIO_POL_OFF:
        return s->pol;
    default:
        return 0;
    }
}

static void g233_gpio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    G233GPIOState *s = opaque;
    uint32_t v = (uint32_t)value;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case GPIO_DIR_OFF:
        s->dir = v;
        g233_gpio_recompute(s);
        break;
    case GPIO_OUT_OFF:
        s->out = v;
        g233_gpio_recompute(s);
        break;
    case GPIO_IE_OFF:
        s->ie = v;
        g233_gpio_recompute(s);
        break;
    case GPIO_IS_OFF:
        s->is &= ~v; /* W1C */
        g233_gpio_recompute(s);
        break;
    case GPIO_TRIG_OFF:
        s->trig = v;
        g233_gpio_recompute(s);
        break;
    case GPIO_POL_OFF:
        s->pol = v;
        g233_gpio_recompute(s);
        break;
    case GPIO_IN_OFF:
        /* read-only */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_gpio_reset(DeviceState *dev)
{
    G233GPIOState *s = G233_GPIO(dev);

    s->dir = 0;
    s->out = 0;
    s->in = 0;
    s->ie = 0;
    s->is = 0;
    s->trig = 0;
    s->pol = 0;
    s->last_in = 0;
    g233_gpio_update_irq(s);
}

static void g233_gpio_init(Object *obj)
{
    G233GPIOState *s = G233_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &g233_gpio_ops, s,
                          TYPE_G233_GPIO, G233_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_gpio_reset);
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)
