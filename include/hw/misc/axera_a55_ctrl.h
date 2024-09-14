/*
 * System-on-Chip PMU register definition
 *
 * Copyright 2023 xinlu.wang
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef AXERA_A55_CTRL_H
#define AXERA_A55_CTRL_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_LUA_CORE_CTRL "lua.a55_core_ctrl"
typedef struct LUACoreCtrlState LUACoreCtrlState;
DECLARE_INSTANCE_CHECKER(LUACoreCtrlState, LUA_CORE_CTRL, TYPE_LUA_CORE_CTRL)

#define LUA_CORE_CTRL_SIZE         0x1000
#define LUA_CORE_CTRL_REG_SIZE     0x110
#define LUA_CORE_CTRL_NUM_REGS     (LUA_CORE_CTRL_REG_SIZE / sizeof(uint32_t))

struct LUACoreCtrlState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;
	uint32_t regs[LUA_CORE_CTRL_NUM_REGS];
	RegisterInfo regs_info[LUA_CORE_CTRL_NUM_REGS];

	uint32_t num_cpu;
	uint8_t target_el;
	uint8_t start_powered_off;
};

#endif