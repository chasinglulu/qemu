// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC System Reset Control register emulation
 *
 * Copyright (C) 2025 Charleye <wangkart@aliyun.com>
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "sysemu/sysemu.h"
#include "sysemu/runstate.h"

struct LUASYSResetState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;

	/* system reset control value */
	uint32_t sysreset;
};
typedef struct LUASYSResetState LUASYSResetState;
#define TYPE_LUA_SYSRESET "laguna.sysreset"
DECLARE_INSTANCE_CHECKER(LUASYSResetState, LUA_SYSRESET, TYPE_LUA_SYSRESET)

static Property lua_sysreset_properties[] = {
	DEFINE_PROP_UINT32("sysreset", LUASYSResetState, sysreset, 0),
	DEFINE_PROP_END_OF_LIST(),
};

static void lua_sysreset_realize(DeviceState *dev, Error **errp)
{
}

static void lua_sysreset_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_sysreset_properties);
	dc->realize = lua_sysreset_realize;
	dc->desc = "Laguna System Reset Control";
}

static uint64_t lua_sysreset_read(void *opaque, hwaddr offset, unsigned int size)
{
	LUASYSResetState *s = LUA_SYSRESET(opaque);

	return s->sysreset;
}

static void lua_sysreset_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
	LUASYSResetState *s = LUA_SYSRESET(opaque);

	s->sysreset = value;

	if (value & BIT(7)) {
		/* Trigger system reset */
		qemu_log_mask(LOG_GUEST_ERROR, "Laguna System Reset triggered\n");
		qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
	}
}

static const MemoryRegionOps lua_sysreset_ops = {
	.read = lua_sysreset_read,
	.write = lua_sysreset_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.impl.min_access_size = 4,
	.impl.max_access_size = 4,
};

static void lua_sysreset_init(Object *obj)
{
	LUASYSResetState *s = LUA_SYSRESET(obj);

	memory_region_init_io(&s->mmio, obj, &lua_sysreset_ops,
	              s, TYPE_LUA_SYSRESET, sizeof(uint32_t));
	sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static const TypeInfo lua_sysreset_info = {
	.name = TYPE_LUA_SYSRESET,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUASYSResetState),
	.instance_init = lua_sysreset_init,
	.class_init = lua_sysreset_class_init
};

static void lua_sysreset_register_types(void)
{
	type_register_static(&lua_sysreset_info);
}
type_init(lua_sysreset_register_types)