/*
 * G233 SPI controller (MMIO) — SSI master stub
 *
 * Copyright (c) 2025
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/fifo8.h"
#include "qemu/module.h"
#include "qemu/log.h"

#define SPI_CR1   0x00
#define SPI_CR2   0x04
#define SPI_SR    0x08
#define SPI_DR    0x0c

#define SPI_CR1_DEFAULT_VALUE    0x000000e5u
#define SPI_CR2_DEFAULT_VALUE    0x00000000u
#define SPI_DR_DEFAULT_VALUE     0x00000000u

#define SPI_CR1_SPE     (1u << 0)
#define SPI_CR1_MSTR    (1u << 2)
#define SPI_CR1_ERRIE   (1u << 5)
#define SPI_CR1_RXNEIE  (1u << 6)
#define SPI_CR1_TXEIE   (1u << 7)

#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_OVERRUN  (1u << 4)

#define SPI_CS_LINES_NR   2

#define SPI_FIFO_SIZE   64

#define TYPE_G233_SPI "g233.spi"
OBJECT_DECLARE_SIMPLE_TYPE(G233SPIState, G233_SPI)

struct G233SPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    SSIBus *bus;

    /* sysbus IRQ 0: SPI -> PLIC; 1..N: chip-select lines to SSIPeripherals */
    qemu_irq irq;
    qemu_irq irq_cs_lines[SPI_CS_LINES_NR];

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint32_t cr1;
    uint32_t cr2;
    /* Sticky OVERRUN (bit 4); TXE/RXNE are computed */
    uint32_t sr;
    uint32_t dr;

    bool rx_not_empty;
};

static bool g233_spi_enabled(G233SPIState *s)
{
    return (s->cr1 & SPI_CR1_SPE) && (s->cr1 & SPI_CR1_MSTR);
}

static bool g233_spi_txe_ready(G233SPIState *s)
{
    /* Single-stage shift: whenever master is enabled we accept another byte */
    return g233_spi_enabled(s);
}

static uint32_t g233_spi_sr_get(G233SPIState *s)
{
    uint32_t v = s->sr & SPI_SR_OVERRUN;

    if (s->rx_not_empty) {
        v |= SPI_SR_RXNE;
    }
    if (g233_spi_txe_ready(s)) {
        v |= SPI_SR_TXE;
    }
    return v;
}

static void g233_spi_update_irq(G233SPIState *s)
{
    uint32_t sr = g233_spi_sr_get(s);
    bool level = false;

    if ((s->cr1 & SPI_CR1_TXEIE) && (sr & SPI_SR_TXE)) {
        level = true;
    }
    if ((s->cr1 & SPI_CR1_RXNEIE) && (sr & SPI_SR_RXNE)) {
        level = true;
    }
    if ((s->cr1 & SPI_CR1_ERRIE) && (sr & SPI_SR_OVERRUN)) {
        level = true;
    }
    qemu_set_irq(s->irq, level);
}

static void g233_spi_update_cs(G233SPIState *s)
{
    int sel = (int)(s->cr2 & 0x3);
    int i;

    for (i = 0; i < SPI_CS_LINES_NR; i++) {
        /* Flash uses SSI_CS_LOW: line low (level 0) when selected */
        qemu_set_irq(s->irq_cs_lines[i], (sel == i) ? 0 : 1);
    }
}

static uint64_t g233_spi_read(void *opaque, hwaddr offset, unsigned size)
{
    G233SPIState *s = G233_SPI(opaque);

    switch (offset) {
    case SPI_CR1:
        return s->cr1;
    case SPI_CR2:
        return s->cr2;
    case SPI_SR:
        return g233_spi_sr_get(s);
    case SPI_DR:
        if (s->rx_not_empty) {
            s->rx_not_empty = false;
            g233_spi_update_irq(s);
        }
        return s->dr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx,
                      __func__, offset);
        return 0;
    }
}

static void g233_spi_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    G233SPIState *s = G233_SPI(opaque);

    switch (offset) {
    case SPI_CR1:
        s->cr1 = (uint32_t)value;
        g233_spi_update_irq(s);
        break;
    case SPI_CR2:
        s->cr2 = (uint32_t)value;
        g233_spi_update_cs(s);
        g233_spi_update_irq(s);
        break;
    case SPI_SR:
        /* W1C: clear OVERRUN */
        s->sr &= ~((uint32_t)value & SPI_SR_OVERRUN);
        g233_spi_update_irq(s);
        break;
    case SPI_DR:
        if (g233_spi_enabled(s)) {
            if (s->rx_not_empty) {
                s->sr |= SPI_SR_OVERRUN;
            }
            s->dr = ssi_transfer(s->bus, (uint32_t)((uint8_t)value));
            s->rx_not_empty = true;
        } else {
            s->dr = (uint32_t)value;
        }
        g233_spi_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx,
                      __func__, offset);
        break;
    }
}

static const MemoryRegionOps g233_spi_mmio_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *s = G233_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    memory_region_init_io(&s->mmio, OBJECT(s), &g233_spi_mmio_ops,
                          s, TYPE_G233_SPI, 0x18);
    sysbus_init_mmio(sbd, &s->mmio);

    sysbus_init_irq(sbd, &s->irq);
    for (i = 0; i < SPI_CS_LINES_NR; i++) {
        sysbus_init_irq(sbd, &s->irq_cs_lines[i]);
    }

    s->bus = ssi_create_bus(dev, "spi");
    fifo8_create(&s->tx_fifo, SPI_FIFO_SIZE);
    fifo8_create(&s->rx_fifo, SPI_FIFO_SIZE);
}

static void g233_spi_reset(DeviceState *d)
{
    G233SPIState *s = G233_SPI(d);

    s->cr1 = SPI_CR1_DEFAULT_VALUE;
    s->cr2 = SPI_CR2_DEFAULT_VALUE;
    s->sr = 0;
    s->dr = SPI_DR_DEFAULT_VALUE;
    s->rx_not_empty = false;
    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->tx_fifo);
    g233_spi_update_cs(s);
    g233_spi_update_irq(s);
}

static const VMStateDescription vmstate_g233_spi = {
    .name = TYPE_G233_SPI,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_FIFO8(tx_fifo, G233SPIState),
        VMSTATE_FIFO8(rx_fifo, G233SPIState),
        VMSTATE_UINT32(cr1, G233SPIState),
        VMSTATE_UINT32(cr2, G233SPIState),
        VMSTATE_UINT32(sr, G233SPIState),
        VMSTATE_UINT32(dr, G233SPIState),
        VMSTATE_BOOL(rx_not_empty, G233SPIState),
        VMSTATE_END_OF_LIST()
    }
};

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = g233_spi_realize;
    device_class_set_legacy_reset(dc, g233_spi_reset);
    dc->vmsd = &vmstate_g233_spi;
    dc->desc = "G233 SPI controller";
}

static const TypeInfo g233_spi_info = {
    .name = TYPE_G233_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .class_init = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)
