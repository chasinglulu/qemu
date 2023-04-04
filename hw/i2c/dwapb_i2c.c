/*
 *  DW APB System-on-Chip I2C emulation
 *
 *  Copyright 2023 xinlu.wang
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/i2c/i2c.h"
#include "hw/irq.h"
#include "qom/object.h"
#include "hw/i2c/dwapb_i2c.h"

#ifndef DWAPB_I2C_DEBUG
#define DWAPB_I2C_DEBUG                 1
#endif

#if DWAPB_I2C_DEBUG
#define DPRINT(fmt, args...)              \
    do { fprintf(stderr, "QEMU I2C: "fmt, ## args); } while (0)

static const char *dwapb_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case I2C_CON_ADDR:
        return "I2C_CONTROL_REG";
    case I2C_TAR_ADDR:
        return "I2C_TARGET_REG";
    default:
        return "[?]";
    }
}
#else
#define DPRINT(fmt, args...)              do { } while (0)
#endif

/*
static inline void dwapb_i2c_raise_interrupt(DWAPBI2CState *s)
{
    qemu_irq_raise(s->irq);
}

static void dwapb_i2c_data_receive(void *opaque)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;

    dwapb_i2c_raise_interrupt(s);
}

static void dwapb_i2c_data_send(void *opaque)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;

    dwapb_i2c_raise_interrupt(s);
}
*/

static uint64_t dwapb_i2c_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;
    uint8_t value;

    switch (offset) {
    case I2C_CON_ADDR:
        value = s->i2c_con;
        break;
    default:
        value = 0;
        DPRINT("ERROR: Bad read offset 0x%x\n", (unsigned int)offset);
        break;
    }

    DPRINT("read %s [0x%02x] -> 0x%02x\n", dwapb_i2c_get_regname(offset),
            (unsigned int)offset, value);
    return value;
}

static void dwapb_i2c_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;
    uint8_t v = value & 0xff;

    DPRINT("write %s [0x%02x] <- 0x%02x\n", dwapb_i2c_get_regname(offset),
            (unsigned int)offset, v);

    switch (offset) {
    case I2C_CON_ADDR:
        s->i2c_con = v;
        break;
    default:
        DPRINT("ERROR: Bad write offset 0x%x\n", (unsigned int)offset);
        break;
    }
}

static const MemoryRegionOps dwapb_i2c_ops = {
    .read = dwapb_i2c_read,
    .write = dwapb_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription dwapb_i2c_vmstate = {
    .name = "dwapb-i2c",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(i2c_con, DWAPBI2CState),
        VMSTATE_UINT32(i2c_tar, DWAPBI2CState),
        VMSTATE_UINT32(i2c_sar, DWAPBI2CState),
        VMSTATE_UINT32(i2c_hs_maddr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_data_cmd, DWAPBI2CState),
        VMSTATE_UINT32(i2c_intr_stat, DWAPBI2CState),
        VMSTATE_UINT32(i2c_intr_mask, DWAPBI2CState),
        VMSTATE_UINT32(i2c_raw_intr_stat, DWAPBI2CState),
        VMSTATE_UINT32(i2c_rx_tl, DWAPBI2CState),
        VMSTATE_UINT32(i2c_tx_tl, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_rx_under, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_rx_over, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_tx_over, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_rd_req, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_tx_abrt, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_rx_done, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_activity, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_stop_det, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_start_det, DWAPBI2CState),
        VMSTATE_UINT32(i2c_clr_gen_call, DWAPBI2CState),
        VMSTATE_UINT32(i2c_enable, DWAPBI2CState),
        VMSTATE_UINT32(i2c_status, DWAPBI2CState),
        VMSTATE_UINT32(i2c_txflr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_rxflr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_sda_hold, DWAPBI2CState),
        VMSTATE_UINT32(i2c_tx_abrt_source, DWAPBI2CState),
        VMSTATE_UINT32(i2c_slv_data_nack_only, DWAPBI2CState),
        VMSTATE_UINT32(i2c_dma_cr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_dma_tdlr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_dma_rdlr, DWAPBI2CState),
        VMSTATE_UINT32(i2c_sda_setup, DWAPBI2CState),
        VMSTATE_UINT32(i2c_ack_general_call, DWAPBI2CState),
        VMSTATE_UINT32(i2c_enable_status, DWAPBI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void dwapb_i2c_reset(DeviceState *d)
{
    DWAPBI2CState *s = DWAPB_I2C(d);

    s->i2c_con  = 0x00;
 
}

static void dwapb_i2c_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    DWAPBI2CState *s = DWAPB_I2C(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &dwapb_i2c_ops, s,
                          TYPE_DWAPB_I2C, DWAPB_I2C_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    s->bus = i2c_init_bus(dev, "i2c");
}

static void dwapb_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &dwapb_i2c_vmstate;
    dc->reset = dwapb_i2c_reset;
}

static const TypeInfo dwapb_i2c_type_info = {
    .name = TYPE_DWAPB_I2C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBI2CState),
    .instance_init = dwapb_i2c_init,
    .class_init = dwapb_i2c_class_init,
};

static void dwapb_i2c_register_types(void)
{
    type_register_static(&dwapb_i2c_type_info);
}

type_init(dwapb_i2c_register_types)
