/*
 * Copyright (c) 2023, Xinlu Wang
 *
 * FlexCAN block emulation code
 *
 * Author: Xinlu Wang <wangkart@aliyun.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/net/flexcan.h"
#include "migration/vmstate.h"

static void flexcan_reset(DeviceState *dev)
{
    FlexCANState *s = FLEXCAN(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static uint64_t flexcan_read(void *opaque, hwaddr offset,
                               unsigned size)
{
    FlexCANState *s = opaque;
    return s->regs[offset / sizeof(uint32_t)];
}

static void flexcan_write(void *opaque, hwaddr offset,
                            uint64_t value, unsigned size)
{
    FlexCANState *s = opaque;
    s->regs[offset / sizeof(uint32_t)] = value;
}

static const struct MemoryRegionOps flexcan_ops = {
    .read = flexcan_read,
    .write = flexcan_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl ={
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void flexcan_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    FlexCANState *s = FLEXCAN(obj);

    memory_region_init_io(&s->iomem,
                          obj,
                          &flexcan_ops,
                          s,
                          TYPE_FLEXCAN ".iomem",
                          sizeof(s->regs));
    sysbus_init_mmio(sd, &s->iomem);
}

static const VMStateDescription vmstate_flexcan = {
    .name = TYPE_FLEXCAN,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, FlexCANState, FLEXCAN_NUM),
        VMSTATE_END_OF_LIST()
    },
};

static void flexcan_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = flexcan_reset;
    dc->vmsd  = &vmstate_flexcan;
    dc->desc  = "FlexCAN Controller";
}

static const TypeInfo flexcan_info ={
    .name          = TYPE_FLEXCAN,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FlexCANState),
    .instance_init = flexcan_init,
    .class_init    = flexcan_class_init,
};

static void flexcan_register_type(void)
{
    type_register_static(&flexcan_info);
}
type_init(flexcan_register_type)
