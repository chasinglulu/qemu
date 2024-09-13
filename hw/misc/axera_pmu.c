// SPDX-License-Identifier: GPL-2.0+
/*
 * Axera Laguna SoC PMU emulation
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
#include "trace.h"

// #define LUA_PMU_ERR_DEBUG 1
#ifndef LUA_PMU_ERR_DEBUG
#define LUA_PMU_ERR_DEBUG 0
#endif

#define TYPE_LUA_PMU "lua-pmu"
typedef struct LUAPMUState LUAPMUState;
DECLARE_INSTANCE_CHECKER(LUAPMUState, LUA_PMU, TYPE_LUA_PMU)

#define LUA_PMU_SIZE         0x1000
#define LUA_PMU_REG_SIZE     0x100
#define LUA_PMU_NUM_REGS     (LUA_PMU_REG_SIZE / sizeof(uint32_t))

struct LUAPMUState {
	SysBusDevice parent_obj;

	MemoryRegion mmio;
	uint32_t regs[LUA_PMU_NUM_REGS];
	RegisterInfo regs_info[LUA_PMU_NUM_REGS];
};

REG32(SLEEP_EN, 0x00)
	FIELD(SLEEP_EN, CPU, 0, 1)
	FIELD(SLEEP_EN, NPU, 1, 1)
	FIELD(SLEEP_EN, ISP, 2, 1)
	FIELD(SLEEP_EN, MM, 3, 1)
	FIELD(SLEEP_EN, VPU, 4, 1)
	FIELD(SLEEP_EN, FLASH, 5, 1)
	FIELD(SLEEP_EN, PERIPH, 6, 1)
	FIELD(SLEEP_EN, DDR, 7, 1)
REG32(WAKEUP, 0x04)
	FIELD(WAKEUP, CPU, 0, 1)
	FIELD(WAKEUP, NPU, 1, 1)
	FIELD(WAKEUP, ISP, 2, 1)
	FIELD(WAKEUP, MM, 3, 1)
	FIELD(WAKEUP, VPU, 4, 1)
	FIELD(WAKEUP, FLASH, 5, 1)
	FIELD(WAKEUP, PERIPH, 6, 1)
	FIELD(WAKEUP, DDR, 7, 1)
REG32(PWR_STATE, 0x08)
	FIELD(PWR_STATE, CPU, 0, 4)
	FIELD(PWR_STATE, NPU, 4, 4)
	FIELD(PWR_STATE, ISP, 8, 4)
	FIELD(PWR_STATE, MM, 12, 4)
	FIELD(PWR_STATE, VPU, 16, 4)
	FIELD(PWR_STATE, FLASH, 20, 4)
	FIELD(PWR_STATE, PERIPH, 24, 4)
	FIELD(PWR_STATE, DDR, 28, 4)
REG32(PWR_WAIT0, 0x0C)
	FIELD(PWR_WAIT0, CPU, 0, 8)
	FIELD(PWR_WAIT0, NPU, 8, 8)
	FIELD(PWR_WAIT0, ISP, 16, 8)
	FIELD(PWR_WAIT0, MM, 24, 8)
REG32(PWR_WAIT1, 0x10)
	FIELD(PWR_WAIT1, VPU, 0, 8)
	FIELD(PWR_WAIT1, FLASH, 8, 8)
	FIELD(PWR_WAIT1, PERIPH, 16, 8)
	FIELD(PWR_WAIT1, DDR, 24, 8)
REG32(PWROFF_BYPASS, 0x14)
	FIELD(PWROFF_BYPASS, CPU, 0, 1)
	FIELD(PWROFF_BYPASS, NPU, 1, 1)
	FIELD(PWROFF_BYPASS, ISP, 2, 1)
	FIELD(PWROFF_BYPASS, MM, 3, 1)
	FIELD(PWROFF_BYPASS, VPU, 4, 1)
	FIELD(PWROFF_BYPASS, FLASH, 5, 1)
	FIELD(PWROFF_BYPASS, PERIPH, 6, 1)
	FIELD(PWROFF_BYPASS, DDR, 7, 1)
REG32(DPHY_PWR_CTRL, 0x18)
	FIELD(DPHY_PWR_CTRL, DPHYTX_PWR_OFF, 0, 1)
	FIELD(DPHY_PWR_CTRL, DPHYRX1_PWR_OFF, 1, 1)
	FIELD(DPHY_PWR_CTRL, DPHYRX0_PWR_OFF, 2, 1)
	FIELD(DPHY_PWR_CTRL, DPHYTX_AON_POWER_READY_N, 3, 1)
	FIELD(DPHY_PWR_CTRL, DPHYRX1_AON_POWER_READY_N, 4, 1)
	FIELD(DPHY_PWR_CTRL, DPHYRX0_AON_POWER_READY_N, 5, 1)
REG32(SLP_FRC_EN, 0x1C)
	FIELD(SLP_FRC_EN, CPU, 0, 1)
	FIELD(SLP_FRC_EN, NPU, 1, 1)
	FIELD(SLP_FRC_EN, ISP, 2, 1)
	FIELD(SLP_FRC_EN, MM, 3, 1)
	FIELD(SLP_FRC_EN, VPU, 4, 1)
	FIELD(SLP_FRC_EN, FLASH, 5, 1)
	FIELD(SLP_FRC_EN, PERIPH, 6, 1)
	FIELD(SLP_FRC_EN, DDR, 7, 1)
REG32(SLP_FRC_SW, 0x20)
	FIELD(SLP_FRC_SW, CPU, 0, 1)
	FIELD(SLP_FRC_SW, NPU, 1, 1)
	FIELD(SLP_FRC_SW, ISP, 2, 1)
	FIELD(SLP_FRC_SW, MM, 3, 1)
	FIELD(SLP_FRC_SW, VPU, 4, 1)
	FIELD(SLP_FRC_SW, FLASH, 5, 1)
	FIELD(SLP_FRC_SW, PERIPH, 6, 1)
	FIELD(SLP_FRC_SW, DDR, 7, 1)
REG32(CLK_FRC_EN, 0x24)
	FIELD(CLK_FRC_EN, CPU, 0, 1)
	FIELD(CLK_FRC_EN, NPU, 1, 1)
	FIELD(CLK_FRC_EN, ISP, 2, 1)
	FIELD(CLK_FRC_EN, MM, 3, 1)
	FIELD(CLK_FRC_EN, VPU, 4, 1)
	FIELD(CLK_FRC_EN, FLASH, 5, 1)
	FIELD(CLK_FRC_EN, PERIPH, 6, 1)
	FIELD(CLK_FRC_EN, DDR, 7, 1)
REG32(CLK_FRC_SW, 0x28)
	FIELD(CLK_FRC_SW, CPU, 0, 1)
	FIELD(CLK_FRC_SW, NPU, 1, 1)
	FIELD(CLK_FRC_SW, ISP, 2, 1)
	FIELD(CLK_FRC_SW, MM, 3, 1)
	FIELD(CLK_FRC_SW, VPU, 4, 1)
	FIELD(CLK_FRC_SW, FLASH, 5, 1)
	FIELD(CLK_FRC_SW, PERIPH, 6, 1)
	FIELD(CLK_FRC_SW, DDR, 7, 1)
REG32(RST_FRC_EN, 0x2C)
	FIELD(RST_FRC_EN, CPU, 0, 1)
	FIELD(RST_FRC_EN, NPU, 1, 1)
	FIELD(RST_FRC_EN, ISP, 2, 1)
	FIELD(RST_FRC_EN, MM, 3, 1)
	FIELD(RST_FRC_EN, VPU, 4, 1)
	FIELD(RST_FRC_EN, FLASH, 5, 1)
	FIELD(RST_FRC_EN, PERIPH, 6, 1)
	FIELD(RST_FRC_EN, DDR, 7, 1)
REG32(RST_FRC_SW, 0x30)
	FIELD(RST_FRC_SW, CPU, 0, 1)
	FIELD(RST_FRC_SW, NPU, 1, 1)
	FIELD(RST_FRC_SW, ISP, 2, 1)
	FIELD(RST_FRC_SW, MM, 3, 1)
	FIELD(RST_FRC_SW, VPU, 4, 1)
	FIELD(RST_FRC_SW, FLASH, 5, 1)
	FIELD(RST_FRC_SW, PERIPH, 6, 1)
	FIELD(RST_FRC_SW, DDR, 7, 1)
REG32(SLP_BYPASS, 0x34)
	FIELD(SLP_BYPASS, CPU, 0, 1)
	FIELD(SLP_BYPASS, NPU, 1, 1)
	FIELD(SLP_BYPASS, ISP, 2, 1)
	FIELD(SLP_BYPASS, MM, 3, 1)
	FIELD(SLP_BYPASS, VPU, 4, 1)
	FIELD(SLP_BYPASS, FLASH, 5, 1)
	FIELD(SLP_BYPASS, PERIPH, 6, 1)
	FIELD(SLP_BYPASS, DDR, 7, 1)
REG32(DEEP_SLEEP_CTRL, 0x38)
	FIELD(DEEP_SLEEP_CTRL, XTAL_SLEEP_BYP, 0, 1)
	FIELD(DEEP_SLEEP_CTRL, PLL_SLEEP_BYP, 1, 1)
	FIELD(DEEP_SLEEP_CTRL, PAD_SLEEP_BYP, 2, 1)
	FIELD(DEEP_SLEEP_CTRL, WAIT_OSC_SET, 3, 2)
REG32(BIST_CTRL, 0x3C)
	FIELD(BIST_CTRL, MBIST_START_NPU, 0, 1)
	FIELD(BIST_CTRL, MBIST_START_ISP, 1, 1)
	FIELD(BIST_CTRL, MBIST_START_MM, 2, 1)
	FIELD(BIST_CTRL, MBIST_START_FLASH, 3, 1)
	FIELD(BIST_CTRL, MBIST_TRY_NUM, 4, 3)
	FIELD(BIST_CTRL, RST_CTRL_MBIST_EN, 7, 1)
	FIELD(BIST_CTRL, SW_FORCE_MBIST, 8, 1)
	FIELD(BIST_CTRL, RST_CTRL_LBIST_EN, 9, 1)
	FIELD(BIST_CTRL, SW_FORCE_LBIST, 10, 1)
REG32(BIST_STATUS, 0x40)
	FIELD(BIST_STATUS, MBIST_CTRL_IDLE, 0, 1)
	FIELD(BIST_STATUS, LBIST_FAIL_CNT, 1, 3)
	FIELD(BIST_STATUS, MBIST_FAIL_CNT, 4, 3)
REG32(PD_STATUS, 0x44)
	FIELD(PD_STATUS, CPU_RST_CTRL_PMU, 0, 1)
	FIELD(PD_STATUS, NPU_RST_CTRL_PMU, 1, 1)
	FIELD(PD_STATUS, ISP_RST_CTRL_PMU, 2, 1)
	FIELD(PD_STATUS, MM_RST_CTRL_PMU, 3, 1)
	FIELD(PD_STATUS, VPU_RST_CTRL_PMU, 4, 1)
	FIELD(PD_STATUS, FLASH_RST_CTRL_PMU, 5, 1)
	FIELD(PD_STATUS, PERIPH_RST_CTRL_PMU, 6, 1)
	FIELD(PD_STATUS, DDR_RST_CTRL_PMU, 7, 1)
	FIELD(PD_STATUS, CPU_CLK_CTRL_PMU, 8, 1)
	FIELD(PD_STATUS, NPU_CLK_CTRL_PMU, 9, 1)
	FIELD(PD_STATUS, ISP_CLK_CTRL_PMU, 10, 1)
	FIELD(PD_STATUS, MM_CLK_CTRL_PMU, 11, 1)
	FIELD(PD_STATUS, VPU_CLK_CTRL_PMU, 12, 1)
	FIELD(PD_STATUS, FLASH_CLK_CTRL_PMU, 13, 1)
	FIELD(PD_STATUS, PERIPH_CLK_CTRL_PMU, 14, 1)
	FIELD(PD_STATUS, DDR_RST_CLK_PMU, 15, 1)
REG32(INT_MASK_PWRON, 0x48)
	FIELD(INT_MASK_PWRON, CPU, 0, 1)
	FIELD(INT_MASK_PWRON, NPU, 1, 1)
	FIELD(INT_MASK_PWRON, ISP, 2, 1)
	FIELD(INT_MASK_PWRON, MM, 3, 1)
	FIELD(INT_MASK_PWRON, VPU, 4, 1)
	FIELD(INT_MASK_PWRON, FLASH, 5, 1)
	FIELD(INT_MASK_PWRON, PERIPH, 6, 1)
	FIELD(INT_MASK_PWRON, DDR, 7, 1)
REG32(INT_MASK_PWROFF, 0x4C)
	FIELD(INT_MASK_PWROFF, CPU, 0, 1)
	FIELD(INT_MASK_PWROFF, NPU, 1, 1)
	FIELD(INT_MASK_PWROFF, ISP, 2, 1)
	FIELD(INT_MASK_PWROFF, MM, 3, 1)
	FIELD(INT_MASK_PWROFF, VPU, 4, 1)
	FIELD(INT_MASK_PWROFF, FLASH, 5, 1)
	FIELD(INT_MASK_PWROFF, PERIPH, 6, 1)
	FIELD(INT_MASK_PWROFF, DDR, 7, 1)
REG32(INT_CLR_PWRON, 0x50)
	FIELD(INT_CLR_PWRON, CPU, 0, 1)
	FIELD(INT_CLR_PWRON, NPU, 1, 1)
	FIELD(INT_CLR_PWRON, ISP, 2, 1)
	FIELD(INT_CLR_PWRON, MM, 3, 1)
	FIELD(INT_CLR_PWRON, VPU, 4, 1)
	FIELD(INT_CLR_PWRON, FLASH, 5, 1)
	FIELD(INT_CLR_PWRON, PERIPH, 6, 1)
	FIELD(INT_CLR_PWRON, DDR, 7, 1)
REG32(INT_CLR_PWROFF, 0x54)
	FIELD(INT_CLR_PWROFF, CPU, 0, 1)
	FIELD(INT_CLR_PWROFF, NPU, 1, 1)
	FIELD(INT_CLR_PWROFF, ISP, 2, 1)
	FIELD(INT_CLR_PWROFF, MM, 3, 1)
	FIELD(INT_CLR_PWROFF, VPU, 4, 1)
	FIELD(INT_CLR_PWROFF, FLASH, 5, 1)
	FIELD(INT_CLR_PWROFF, PERIPH, 6, 1)
	FIELD(INT_CLR_PWROFF, DDR, 7, 1)
REG32(INT_RAW_PWRON, 0x58)
	FIELD(INT_RAW_PWRON, CPU, 0, 1)
	FIELD(INT_RAW_PWRON, NPU, 1, 1)
	FIELD(INT_RAW_PWRON, ISP, 2, 1)
	FIELD(INT_RAW_PWRON, MM, 3, 1)
	FIELD(INT_RAW_PWRON, VPU, 4, 1)
	FIELD(INT_RAW_PWRON, FLASH, 5, 1)
	FIELD(INT_RAW_PWRON, PERIPH, 6, 1)
	FIELD(INT_RAW_PWRON, DDR, 7, 1)
REG32(INT_RAW_PWROFF, 0x5C)
	FIELD(INT_RAW_PWROFF, CPU, 0, 1)
	FIELD(INT_RAW_PWROFF, NPU, 1, 1)
	FIELD(INT_RAW_PWROFF, ISP, 2, 1)
	FIELD(INT_RAW_PWROFF, MM, 3, 1)
	FIELD(INT_RAW_PWROFF, VPU, 4, 1)
	FIELD(INT_RAW_PWROFF, FLASH, 5, 1)
	FIELD(INT_RAW_PWROFF, PERIPH, 6, 1)
	FIELD(INT_RAW_PWROFF, DDR, 7, 1)
REG32(INT_STA_PWRON, 0x60)
	FIELD(INT_STA_PWRON, CPU, 0, 1)
	FIELD(INT_STA_PWRON, NPU, 1, 1)
	FIELD(INT_STA_PWRON, ISP, 2, 1)
	FIELD(INT_STA_PWRON, MM, 3, 1)
	FIELD(INT_STA_PWRON, VPU, 4, 1)
	FIELD(INT_STA_PWRON, FLASH, 5, 1)
	FIELD(INT_STA_PWRON, PERIPH, 6, 1)
	FIELD(INT_STA_PWRON, DDR, 7, 1)
REG32(INT_STA_PWROFF, 0x64)
	FIELD(INT_STA_PWROFF, CPU, 0, 1)
	FIELD(INT_STA_PWROFF, NPU, 1, 1)
	FIELD(INT_STA_PWROFF, ISP, 2, 1)
	FIELD(INT_STA_PWROFF, MM, 3, 1)
	FIELD(INT_STA_PWROFF, VPU, 4, 1)
	FIELD(INT_STA_PWROFF, FLASH, 5, 1)
	FIELD(INT_STA_PWROFF, PERIPH, 6, 1)
	FIELD(INT_STA_PWROFF, DDR, 7, 1)
REG32(INT_MASK_LBIST, 0x68)
	FIELD(INT_MASK_LBIST, VAL, 0, 1)
REG32(INT_CLR_LBIST, 0x6C)
	FIELD(INT_CLR_LBIST, VAL, 0, 1)
REG32(INT_RAW_LBIST, 0x70)
	FIELD(INT_RAW_LBIST, VAL, 0, 1)
REG32(INT_STA_LBIST, 0x74)
	FIELD(INT_STA_LBIST, VAL, 0, 1)
REG32(INT_MASK_MBIST, 0x78)
	FIELD(INT_MASK_MBIST, VAL, 0, 1)
REG32(INT_CLR_MBIST, 0x7C)
	FIELD(INT_CLR_MBIST, VAL, 0, 1)
REG32(INT_RAW_MBIST, 0x80)
	FIELD(INT_RAW_MBIST, VAL, 0, 1)
REG32(INT_STA_MBIST, 0x84)
	FIELD(INT_STA_MBIST, VAL, 0, 1)
REG32(MISC_CTRL, 0x88)
	FIELD(MISC_CTRL, DAP_WAKEUP_CPU_MASK, 0, 1)

static const RegisterAccessInfo lua_pmu_regs_info[] = {
	{ .name = "SLEEP_EN", .addr = A_SLEEP_EN,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "WAKEUP", .addr = A_WAKEUP,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "PWR_STATE", .addr = A_PWR_STATE,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "PWR_WAIT0", .addr = A_PWR_WAIT0,
		.unimp = 0xFFFFFFFF,
		.reset = 0x55555555,
	}, { .name = "PWR_WAIT1", .addr = A_PWR_WAIT1,
		.unimp = 0xFFFFFFFF,
		.reset = 0x55555555,
	}, { .name = "PWROFF_BYPASS", .addr = A_PWROFF_BYPASS,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "DPHY_PWR_CTRL", .addr = A_DPHY_PWR_CTRL,
		.ro = 0xFFFFFFC0,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "SLP_FRC_EN", .addr = A_SLP_FRC_EN,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "SLP_FRC_SW", .addr = A_SLP_FRC_SW,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "CLK_FRC_EN", .addr = A_CLK_FRC_EN,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "CLK_FRC_SW", .addr = A_CLK_FRC_SW,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "RST_FRC_EN", .addr = A_RST_FRC_EN,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "RST_FRC_SW", .addr = A_RST_FRC_SW,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "SLP_BYPASS", .addr = A_SLP_BYPASS,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "DEEP_SLEEP_CTRL", .addr = A_DEEP_SLEEP_CTRL,
		.ro = 0xFFFFFFE0,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "BIST_CTRL", .addr = A_BIST_CTRL,
		.w1c = 0xF,
		.ro = 0xFFFFF800,
		.unimp = 0xFFFFFFFF,
		.reset = 0x270,
	}, { .name = "BIST_STATUS", .addr = A_BIST_STATUS,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "PD_STATUS", .addr = A_PD_STATUS,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_MASK_PWRON", .addr = A_INT_MASK_PWRON,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_MASK_PWROFF", .addr = A_INT_MASK_PWROFF,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_CLR_PWRON", .addr = A_INT_CLR_PWRON,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
		.w1c = 0xFF,
	}, { .name = "INT_CLR_PWROFF", .addr = A_INT_CLR_PWROFF,
		.ro = 0xFFFFFF00,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
		.w1c = 0xFF,
	}, { .name = "INT_RAW_PWRON", .addr = A_INT_RAW_PWRON,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_RAW_PWROFF", .addr = A_INT_RAW_PWROFF,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_STA_PWRON", .addr = A_INT_STA_PWRON,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_STA_PWROFF", .addr = A_INT_STA_PWROFF,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_MASK_LBIST", .addr = A_INT_MASK_LBIST,
		.ro = 0xFFFFFFFE,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_CLR_LBIST", .addr = A_INT_CLR_LBIST,
		.ro = 0xFFFFFFFE,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
		.w1c = 0x1,
	}, { .name = "INT_RAW_LBIST", .addr = A_INT_RAW_LBIST,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_STA_LBIST", .addr = A_INT_STA_LBIST,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_MASK_MBIST", .addr = A_INT_MASK_MBIST,
		.ro = 0xFFFFFFFE,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_CLR_MBIST", .addr = A_INT_CLR_MBIST,
		.ro = 0xFFFFFFFE,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
		.w1c = 0x1,
	}, { .name = "INT_RAW_MBIST", .addr = A_INT_RAW_MBIST,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "INT_STA_MBIST", .addr = A_INT_STA_MBIST,
		.ro = 0xFFFFFFFF,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}, { .name = "MISC_CTRL", .addr = A_MISC_CTRL,
		.ro = 0xFFFFFFFE,
		.unimp = 0xFFFFFFFF,
		.reset = 0x0,
	}
};

static const MemoryRegionOps lua_pmu_ops = {
	.read = register_read_memory,
	.write = register_write_memory,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
};

static void lua_pmu_reset(DeviceState *dev)
{
	LUAPMUState *s = LUA_PMU(dev);

	arm_set_cpu_off(0x0);

	memset(s->regs, 0, sizeof(s->regs));
}

static Property lua_pmu_properties[] = {
	DEFINE_PROP_END_OF_LIST(),
};

static void lua_pmu_realize(DeviceState *dev, Error **errp)
{
	LUAPMUState *s = LUA_PMU(dev);

	memory_region_init_io(&s->mmio, OBJECT(dev), &lua_pmu_ops, s,
			TYPE_LUA_PMU, LUA_PMU_REG_SIZE);

	sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void lua_pmu_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, lua_pmu_properties);
	// dc->vmsd = &vmstate_sigi_pmu;
	dc->realize = lua_pmu_realize;
	dc->reset = lua_pmu_reset;
	dc->desc = "Laguna PMU";
}

static void lua_pmu_init(Object *obj)
{
	LUAPMUState *s = LUA_PMU(obj);
	RegisterInfoArray *reg_array;

	memory_region_init(&s->mmio, obj, TYPE_LUA_PMU, LUA_PMU_SIZE);
	reg_array = register_init_block32(DEVICE(obj), lua_pmu_regs_info,
					ARRAY_SIZE(lua_pmu_regs_info),
					s->regs_info, s->regs,
					&lua_pmu_ops,
					LUA_PMU_ERR_DEBUG,
					LUA_PMU_REG_SIZE);
	memory_region_add_subregion(&s->mmio, 0x00,
					&reg_array->mem);
}

static const TypeInfo lua_pmu_info = {
	.name = TYPE_LUA_PMU,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(LUAPMUState),
	.instance_init = lua_pmu_init,
	.class_init = lua_pmu_class_init
};

static void lua_pmu_register_types(void)
{
	type_register_static(&lua_pmu_info);
}

type_init(lua_pmu_register_types)