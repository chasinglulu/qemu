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
#define DWAPB_I2C_DEBUG                 0
#endif

#if DWAPB_I2C_DEBUG
#define DPRINT(fmt, args...)              \
    do { fprintf(stderr, "QEMU I2C: "fmt, ## args); } while (0)

static const char *dwapb_i2c_get_regname(unsigned offset)
{
    switch (offset) {
    case I2CCON_ADDR:
        return "I2CCON";
    case I2CSTAT_ADDR:
        return "I2CSTAT";
    case I2CADD_ADDR:
        return "I2CADD";
    case I2CDS_ADDR:
        return "I2CDS";
    case I2CLC_ADDR:
        return "I2CLC";
    default:
        return "[?]";
    }
}
#else
#define DPRINT(fmt, args...)              do { } while (0)
#endif

static inline void dwapb_i2c_raise_interrupt(DWAPBI2CState *s)
{
    if (s->i2ccon & I2CCON_INTRS_EN) {
        s->i2ccon |= I2CCON_INT_PEND;
        qemu_irq_raise(s->irq);
    }
}

static void dwapb_i2c_data_receive(void *opaque)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;

    s->i2cstat &= ~I2CSTAT_LAST_BIT;
    s->scl_free = false;
    s->i2cds = i2c_recv(s->bus);
    dwapb_i2c_raise_interrupt(s);
}

static void dwapb_i2c_data_send(void *opaque)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;

    s->i2cstat &= ~I2CSTAT_LAST_BIT;
    s->scl_free = false;
    if (i2c_send(s->bus, s->i2cds) < 0 && (s->i2ccon & I2CCON_ACK_GEN)) {
        s->i2cstat |= I2CSTAT_LAST_BIT;
    }
    dwapb_i2c_raise_interrupt(s);
}

static uint64_t dwapb_i2c_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    DWAPBI2CState *s = (DWAPBI2CState *)opaque;
    uint8_t value;

    switch (offset) {
    case I2CCON_ADDR:
        value = s->i2ccon;
        break;
    case I2CSTAT_ADDR:
        value = s->i2cstat;
        break;
    case I2CADD_ADDR:
        value = s->i2cadd;
        break;
    case I2CDS_ADDR:
        value = s->i2cds;
        s->scl_free = true;
        if (DWAPB_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Rx &&
               (s->i2cstat & I2CSTAT_START_BUSY) &&
               !(s->i2ccon & I2CCON_INT_PEND)) {
            dwapb_i2c_data_receive(s);
        }
        break;
    case I2CLC_ADDR:
        value = s->i2clc;
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
    case I2CCON_ADDR:
        s->i2ccon = (v & ~I2CCON_INT_PEND) | (s->i2ccon & I2CCON_INT_PEND);
        if ((s->i2ccon & I2CCON_INT_PEND) && !(v & I2CCON_INT_PEND)) {
            s->i2ccon &= ~I2CCON_INT_PEND;
            qemu_irq_lower(s->irq);
            if (!(s->i2ccon & I2CCON_INTRS_EN)) {
                s->i2cstat &= ~I2CSTAT_START_BUSY;
            }

            if (s->i2cstat & I2CSTAT_START_BUSY) {
                if (s->scl_free) {
                    if (DWAPB_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Tx) {
                        dwapb_i2c_data_send(s);
                    } else if (DWAPB_I2C_MODE(s->i2cstat) ==
                            I2CMODE_MASTER_Rx) {
                        dwapb_i2c_data_receive(s);
                    }
                } else {
                    s->i2ccon |= I2CCON_INT_PEND;
                    qemu_irq_raise(s->irq);
                }
            }
        }
        break;
    case I2CSTAT_ADDR:
        s->i2cstat =
                (s->i2cstat & I2CSTAT_START_BUSY) | (v & ~I2CSTAT_START_BUSY);

        if (!(s->i2cstat & I2CSTAT_OUTPUT_EN)) {
            s->i2cstat &= ~I2CSTAT_START_BUSY;
            s->scl_free = true;
            qemu_irq_lower(s->irq);
            break;
        }

        /* Nothing to do if in i2c slave mode */
        if (!I2C_IN_MASTER_MODE(s->i2cstat)) {
            break;
        }

        if (v & I2CSTAT_START_BUSY) {
            s->i2cstat &= ~I2CSTAT_LAST_BIT;
            s->i2cstat |= I2CSTAT_START_BUSY;    /* Line is busy */
            s->scl_free = false;

            /* Generate start bit and send slave address */
            if (i2c_start_transfer(s->bus, s->i2cds >> 1, s->i2cds & 0x1) &&
                    (s->i2ccon & I2CCON_ACK_GEN)) {
                s->i2cstat |= I2CSTAT_LAST_BIT;
            } else if (DWAPB_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Rx) {
                dwapb_i2c_data_receive(s);
            }
            dwapb_i2c_raise_interrupt(s);
        } else {
            i2c_end_transfer(s->bus);
            if (!(s->i2ccon & I2CCON_INT_PEND)) {
                s->i2cstat &= ~I2CSTAT_START_BUSY;
            }
            s->scl_free = true;
        }
        break;
    case I2CADD_ADDR:
        if ((s->i2cstat & I2CSTAT_OUTPUT_EN) == 0) {
            s->i2cadd = v;
        }
        break;
    case I2CDS_ADDR:
        if (s->i2cstat & I2CSTAT_OUTPUT_EN) {
            s->i2cds = v;
            s->scl_free = true;
            if (DWAPB_I2C_MODE(s->i2cstat) == I2CMODE_MASTER_Tx &&
                    (s->i2cstat & I2CSTAT_START_BUSY) &&
                    !(s->i2ccon & I2CCON_INT_PEND)) {
                dwapb_i2c_data_send(s);
            }
        }
        break;
    case I2CLC_ADDR:
        s->i2clc = v;
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
        VMSTATE_UINT8(i2ccon, DWAPBI2CState),
        VMSTATE_UINT8(i2cstat, DWAPBI2CState),
        VMSTATE_UINT8(i2cds, DWAPBI2CState),
        VMSTATE_UINT8(i2cadd, DWAPBI2CState),
        VMSTATE_UINT8(i2clc, DWAPBI2CState),
        VMSTATE_BOOL(scl_free, DWAPBI2CState),
        VMSTATE_END_OF_LIST()
    }
};

static void dwapb_i2c_reset(DeviceState *d)
{
    DWAPBI2CState *s = DWAPB_I2C(d);

    s->i2ccon  = 0x00;
    s->i2cstat = 0x00;
    s->i2cds   = 0xFF;
    s->i2clc   = 0x00;
    s->i2cadd  = 0xFF;
    s->scl_free = true;
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
