// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC Bootstrap Controller emulation
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
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

struct LUABootstrapState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;

	uint32_t bootstrap;
};
typedef struct LUABootstrapState LUABootstrapState;
#define TYPE_LUA_BOOTSTRAP "laguna.bootstrap"
DECLARE_INSTANCE_CHECKER(LUABootstrapState, LUA_BOOTSTRAP, TYPE_LUA_BOOTSTRAP)

static Property lua_bootstrap_properties[] = {
	DEFINE_PROP_UINT32("bootstrap", LUABootstrapState, bootstrap, 0),
	DEFINE_PROP_END_OF_LIST(),
};

static void lua_bootstrap_realize(DeviceState *dev, Error **errp)
{
}

static void lua_bootstrap_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_bootstrap_properties);
	// dc->vmsd = &vmstate_lua_bootstrap;
	dc->realize = lua_bootstrap_realize;
	// dc->reset = lua_bootstrap_reset;
	dc->desc = "Laguna Bootstrap Controller";
}

static uint64_t lua_bootstarp_read(void *opaque, hwaddr offset, unsigned int size)
{
	LUABootstrapState *s = LUA_BOOTSTRAP(opaque);

	return s->bootstrap;
}

static void lua_bootstarp_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
	LUABootstrapState *s = LUA_BOOTSTRAP(opaque);

	s->bootstrap = value;
}

static const MemoryRegionOps lua_bootstarp_ops = {
	.read = lua_bootstarp_read,
	.write = lua_bootstarp_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.impl.min_access_size = 4,
	.impl.max_access_size = 4,
};

static void lua_bootstrap_init(Object *obj)
{
	LUABootstrapState *s = LUA_BOOTSTRAP(obj);

	memory_region_init_io(&s->mmio, obj, &lua_bootstarp_ops,
	              s, TYPE_LUA_BOOTSTRAP, sizeof(uint32_t));
	sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static const TypeInfo lua_bootstrap_info = {
	.name = TYPE_LUA_BOOTSTRAP,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUABootstrapState),
	.instance_init = lua_bootstrap_init,
	.class_init = lua_bootstrap_class_init
};

static void lua_bootstrap_register_types(void)
{
	type_register_static(&lua_bootstrap_info);
}
type_init(lua_bootstrap_register_types)