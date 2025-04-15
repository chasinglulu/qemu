// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC Download interface register emulation
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

struct LUADownloadIFState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;

	/* download interface value */
	uint32_t downif;
};
typedef struct LUADownloadIFState LUADownloadIFState;
#define TYPE_LUA_DOWNIF "laguna.downif"
DECLARE_INSTANCE_CHECKER(LUADownloadIFState, LUA_DOWNIF, TYPE_LUA_DOWNIF)

static Property lua_download_if_properties[] = {
	DEFINE_PROP_UINT32("downif", LUADownloadIFState, downif, 0),
	DEFINE_PROP_END_OF_LIST(),
};

static void lua_download_if_realize(DeviceState *dev, Error **errp)
{
}

static void lua_download_if_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_download_if_properties);
	// dc->vmsd = &vmstate_lua_download-if;
	dc->realize = lua_download_if_realize;
	// dc->reset = lua_download_if_reset;
	dc->desc = "Laguna Download Interface";
}

static uint64_t lua_download_if_read(void *opaque, hwaddr offset, unsigned int size)
{
	LUADownloadIFState *s = LUA_DOWNIF(opaque);

	return s->downif;
}

static void lua_download_if_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
	LUADownloadIFState *s = LUA_DOWNIF(opaque);

	s->downif = value;
}

static const MemoryRegionOps lua_download_if_ops = {
	.read = lua_download_if_read,
	.write = lua_download_if_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.impl.min_access_size = 4,
	.impl.max_access_size = 4,
};

static void lua_download_if_init(Object *obj)
{
	LUADownloadIFState *s = LUA_DOWNIF(obj);

	memory_region_init_io(&s->mmio, obj, &lua_download_if_ops,
	              s, TYPE_LUA_DOWNIF, sizeof(uint32_t));
	sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static const TypeInfo lua_download_if_info = {
	.name = TYPE_LUA_DOWNIF,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUADownloadIFState),
	.instance_init = lua_download_if_init,
	.class_init = lua_download_if_class_init
};

static void lua_download_if_register_types(void)
{
	type_register_static(&lua_download_if_info);
}
type_init(lua_download_if_register_types)