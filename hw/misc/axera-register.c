// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC register emulation
 *
 * Copyright (C) 2025 chasinglulu <wangkart@aliyun.com>
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
#include "qapi/error.h"
#include "trace.h"

struct LUARegisterState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;
	char *name;

	/* register default value*/
	uint32_t reg_val;

	uint32_t rstval; /* reset value */
	bool resettable;
};

typedef struct LUARegisterState LUARegisterState;
#define TYPE_LUA_REGISTER "laguna.register"
DECLARE_INSTANCE_CHECKER(LUARegisterState, LUA_REGISTER, TYPE_LUA_REGISTER)

static Property lua_regitser_properties[] = {
	DEFINE_PROP_STRING("name", LUARegisterState, name),
	DEFINE_PROP_UINT32("default", LUARegisterState, rstval, 0),
	DEFINE_PROP_BOOL("resettable", LUARegisterState, resettable, true),
	DEFINE_PROP_END_OF_LIST(),
};

static uint64_t lua_register_read(void *opaque, hwaddr offset, unsigned int size)
{
	LUARegisterState *s = LUA_REGISTER(opaque);

	trace_lua_register_read(SYS_BUS_DEVICE(s)->mmio[0].addr, s->rstval);

	return s->reg_val;
}

static void lua_register_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
	LUARegisterState *s = LUA_REGISTER(opaque);

	trace_lua_register_write(SYS_BUS_DEVICE(s)->mmio[0].addr, (uint32_t)value);

	s->reg_val = value;
}

static const MemoryRegionOps lua_register_ops = {
	.read = lua_register_read,
	.write = lua_register_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.impl.min_access_size = 4,
	.impl.max_access_size = 4,
};

static void lua_register_reset(DeviceState *dev)
{
	LUARegisterState *s = LUA_REGISTER(dev);

	if (s->resettable) {
		s->reg_val = s->rstval;
		trace_lua_register_write(SYS_BUS_DEVICE(s)->mmio[0].addr, s->rstval);
	}
}

static void lua_regitser_realize(DeviceState *dev, Error **errp)
{
	LUARegisterState *s = LUA_REGISTER(dev);

	if (s->name == NULL) {
		error_setg(errp, "property 'name' not specified");
		return;
	}

	s->reg_val = s->rstval;

	memory_region_init_io(&s->mmio, OBJECT(s), &lua_register_ops,
	              s, s->name, sizeof(uint32_t));
	sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);
}

static void lua_regitser_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_regitser_properties);
	dc->realize = lua_regitser_realize;
	dc->reset = lua_register_reset;
	dc->desc = "Laguna Register With Default Value";
}

static void lua_regitser_init(Object *obj)
{
}

static const TypeInfo lua_regitser_info = {
	.name = TYPE_LUA_REGISTER,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUARegisterState),
	.instance_init = lua_regitser_init,
	.class_init = lua_regitser_class_init
};

static void lua_regitser_register_types(void)
{
	type_register_static(&lua_regitser_info);
}
type_init(lua_regitser_register_types)