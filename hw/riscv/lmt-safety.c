/*
 * Lambert safety island emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"
#include "hw/riscv/lmt-safety.h"
#include "hw/misc/unimp.h"
#include "sysemu/hostmem.h"
#include "migration/vmstate.h"


static void lmt_safety_realize(DeviceState *dev, Error **errp)
{
	//LambertSafety *s = LMT_SAFETY(dev);
}

static Property lmt_safety_properties[] = {
	DEFINE_PROP_STRING("memdev", LambertSafety, cfg.memdev),
	DEFINE_PROP_END_OF_LIST()
};

static void lmt_safety_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = lmt_safety_realize;
	device_class_set_props(dc, lmt_safety_properties);
}

static void lmt_safety_init(Object *obj)
{
}

static const TypeInfo lmt_safety_info = {
	.name = TYPE_LMT_SAFETY,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LambertSafety),
	.instance_init = lmt_safety_init,
	.class_init = lmt_safety_class_init,
};

static void lmt_safety_register_types(void)
{
	type_register_static(&lmt_safety_info);
}

type_init(lmt_safety_register_types);
