// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC A55 CPU Controller emulation
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
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "target/arm/arm-powerctl.h"
#include "target/arm/cpu.h"
#include "hw/register.h"
#include "hw/misc/axera_a55_ctrl.h"
#include "trace.h"

// #define LUA_CORE_CTRL_ERR_DEBUG 1
#ifndef LUA_CORE_CTRL_ERR_DEBUG
#define LUA_CORE_CTRL_ERR_DEBUG 0
#endif

REG32(CA55_CFG, 0x00)
	FIELD(CA55_CFG, EE_CORE0, 0, 1)
	FIELD(CA55_CFG, EE_CORE1, 1, 1)
	FIELD(CA55_CFG, EE_CORE2, 2, 1)
	FIELD(CA55_CFG, EE_CORE3, 3, 1)
	FIELD(CA55_CFG, TE_CORE0, 8, 1)
	FIELD(CA55_CFG, TE_CORE1, 9, 1)
	FIELD(CA55_CFG, TE_CORE2, 10, 1)
	FIELD(CA55_CFG, TE_CORE3, 11, 1)
	FIELD(CA55_CFG, MASK_CFG, 12, 1)
REG32(CA55_PCHN_CTRL_CORE0, 0x30)
REG32(CA55_PCHN_CTRL_CORE1, 0x34)
REG32(CA55_PCHN_CTRL_CORE2, 0x38)
REG32(CA55_PCHN_CTRL_DSU0, 0x3C)
REG32(CA55_PCHN_CTRL_DSU1, 0x40)
REG32(CA55_PCHN_CTRL_DSU2, 0x44)
REG32(CA55_PCHN_INT_STS, 0x48)
REG32(CA55_PCHN_LPC_STS, 0x4C)
REG32(CA55_QCHN_LPC_CFG, 0x50)
REG32(CA55_QCHN_LPC_STS, 0x54)
REG32(CA55_QCHN_LPC_TIMEOUT, 0x58)
REG32(FAB_CPU_CTL, 0x5C)
REG32(FAB_CPU_IRQ_CTL, 0x60)
REG32(FAB_CPU_IRQ_STS, 0x64)
REG32(CA55_CFG_INT_DISABLE, 0x68)
REG32(SLEEP_CTRL, 0x6C)
REG32(CORE0_PC_H, 0x80)
REG32(CORE0_PC_L, 0x84)
REG32(CORE1_PC_H, 0x88)
REG32(CORE1_PC_L, 0x8C)
REG32(CORE2_PC_H, 0x90)
REG32(CORE2_PC_L, 0x94)
REG32(CORE3_PC_H, 0x98)
REG32(CORE3_PC_L, 0x9C)
REG32(A55_BUSY_STS, 0xA0)
REG32(CA55_CORE_SW_RST, 0xE0)
	FIELD(CA55_CORE_SW_RST, CORE0, 0, 1)
	FIELD(CA55_CORE_SW_RST, CORE1, 1, 1)
	FIELD(CA55_CORE_SW_RST, CORE2, 2, 1)
	FIELD(CA55_CORE_SW_RST, CORE3, 3, 1)
REG32(CA55_INIT, 0xE4)
REG32(CA55_RVBARADDR0_L, 0xE8)
REG32(CA55_RVBARADDR0_H, 0xEC)
REG32(CA55_RVBARADDR1_L, 0xF0)
REG32(CA55_RVBARADDR1_H, 0xF4)
REG32(CA55_RVBARADDR2_L, 0xF8)
REG32(CA55_RVBARADDR2_H, 0xFC)
REG32(CA55_RVBARADDR3_L, 0x100)
REG32(CA55_RVBARADDR3_H, 0x104)

static void lua_core_ctrl_swrst(RegisterInfo *reg, uint64_t val64)
{
	LUACoreCtrlState *s = LUA_CORE_CTRL(reg->opaque);
	uint32_t cpu_idx;
	ARMCPU *target_cpu = NULL;
	bool power_on = false;
	uint64_t entry = 0;
	int i, ret;

	for (i = 0; i < s->num_cpu; i++) {
		cpu_idx = i << 8;
		if (val64 & BIT(i))
			power_on = false;
		else
			power_on = true;

		target_cpu = ARM_CPU(arm_get_cpu_by_id(cpu_idx));
		g_assert(target_cpu != NULL);

		if (target_cpu->power_state == PSCI_ON && power_on)
			continue;
		if (target_cpu->power_state == PSCI_OFF && !power_on)
			continue;

		switch (BIT(i)) {
		case R_CA55_CORE_SW_RST_CORE0_MASK:
			entry = s->regs[R_CA55_RVBARADDR0_H];
			entry <<= 32;
			entry |= s->regs[R_CA55_RVBARADDR0_L];
			break;
		case R_CA55_CORE_SW_RST_CORE1_MASK:
			entry = s->regs[R_CA55_RVBARADDR1_H];
			entry <<= 32;
			entry |= s->regs[R_CA55_RVBARADDR1_L];
			break;
		case R_CA55_CORE_SW_RST_CORE2_MASK:
			entry = s->regs[R_CA55_RVBARADDR2_H];
			entry <<= 32;
			entry |= s->regs[R_CA55_RVBARADDR2_L];
			break;
		case R_CA55_CORE_SW_RST_CORE3_MASK:
			entry = s->regs[R_CA55_RVBARADDR3_H];
			entry <<= 32;
			entry |= s->regs[R_CA55_RVBARADDR3_L];
			break;
		}

		if (power_on) {
			trace_lua_core_ctrl_poweron(cpu_idx, entry, s->target_el);
			ret = arm_set_cpu_on(cpu_idx, entry, 0,
								s->target_el, true);
			if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS)
				error_report("%s: failed to bring up CPU %d: err %d",
							__func__, cpu_idx, ret);
		} else {
			trace_lua_core_ctrl_poweroff(cpu_idx);
			ret = arm_set_cpu_off(cpu_idx);
			if (ret != QEMU_ARM_POWERCTL_RET_SUCCESS)
				error_report("%s: failed to power off CPU %d: err %d",
							__func__, cpu_idx, ret);
		}
	}
}

static const RegisterAccessInfo lua_core_ctrl_regs_info[] = {
	{ .name = "CA55_CORE_SW_RST", .addr = A_CA55_CORE_SW_RST,
		.ro = 0xFFFFFFF0,
		.unimp = 0xFFFFFFFF,
		.reset = 0xF,
		.post_write = lua_core_ctrl_swrst,
	}, { .name = "CA55_RVBARADDR0_L", .addr = A_CA55_RVBARADDR0_L,
		.unimp = 0xFFFFFFFF,
		.reset = 0x14000000,
	}, { .name = "CA55_RVBARADDR0_H", .addr = A_CA55_RVBARADDR0_H,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "CA55_RVBARADDR1_L", .addr = A_CA55_RVBARADDR1_L,
		.unimp = 0xFFFFFFFF,
		.reset = 0x14000000,
	}, { .name = "CA55_RVBARADDR1_H", .addr = A_CA55_RVBARADDR1_H,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "CA55_RVBARADDR2_L", .addr = A_CA55_RVBARADDR2_L,
		.unimp = 0xFFFFFFFF,
		.reset = 0x14000000,
	}, { .name = "CA55_RVBARADDR2_H", .addr = A_CA55_RVBARADDR2_H,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "CA55_RVBARADDR3_L", .addr = A_CA55_RVBARADDR3_L,
		.unimp = 0xFFFFFFFF,
		.reset = 0x14000000,
	}, { .name = "CA55_RVBARADDR3_H", .addr = A_CA55_RVBARADDR3_H,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	},
};

static const MemoryRegionOps lua_core_ctrl_ops = {
	.read = register_read_memory,
	.write = register_write_memory,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
};

static void lua_core_ctrl_reset(DeviceState *dev)
{
	LUACoreCtrlState *s = LUA_CORE_CTRL(dev);
	int i;

	if (s->start_powered_off)
		register_write(&s->regs_info[R_CA55_CORE_SW_RST], s->start_powered_off,
		                    ~0, NULL, false);

	for (i = 0; i < ARRAY_SIZE(s->regs_info); i++) {
		switch (i) {
		case R_CA55_CORE_SW_RST:
			break;
		default:
			register_reset(&s->regs_info[i]);
		};
	}
}

static Property lua_core_ctrl_properties[] = {
	DEFINE_PROP_UINT32("num-cpu", LUACoreCtrlState, num_cpu, 4),
	DEFINE_PROP_UINT8("el", LUACoreCtrlState, target_el, 3),
	DEFINE_PROP_UINT8("start-powered-off", LUACoreCtrlState, start_powered_off, 0xF),
	DEFINE_PROP_END_OF_LIST(),
};

static void lua_core_ctrl_realize(DeviceState *dev, Error **errp)
{
	LUACoreCtrlState *s = LUA_CORE_CTRL(dev);

	sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void lua_core_ctrl_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_core_ctrl_properties);
	// dc->vmsd = &vmstate_lua_core_ctrl;
	dc->realize = lua_core_ctrl_realize;
	dc->reset = lua_core_ctrl_reset;
	dc->desc = "Laguna A55 Core Controller";
}

static void lua_core_ctrl_init(Object *obj)
{
	LUACoreCtrlState *s = LUA_CORE_CTRL(obj);
	RegisterInfoArray *reg_array;

	memory_region_init(&s->mmio, obj, TYPE_LUA_CORE_CTRL,
							LUA_CORE_CTRL_SIZE);
	reg_array = register_init_block32(DEVICE(obj), lua_core_ctrl_regs_info,
					ARRAY_SIZE(lua_core_ctrl_regs_info),
					s->regs_info, s->regs,
					&lua_core_ctrl_ops,
					LUA_CORE_CTRL_ERR_DEBUG,
					LUA_CORE_CTRL_REG_SIZE);

	memory_region_add_subregion(&s->mmio, 0x00,
					&reg_array->mem);
}

static const TypeInfo lua_core_ctrl_info = {
	.name = TYPE_LUA_CORE_CTRL,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUACoreCtrlState),
	.instance_init = lua_core_ctrl_init,
	.class_init = lua_core_ctrl_class_init
};

static void lua_core_ctrl_register_types(void)
{
	type_register_static(&lua_core_ctrl_info);
}
type_init(lua_core_ctrl_register_types)
