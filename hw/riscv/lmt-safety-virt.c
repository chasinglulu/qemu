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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/sysbus.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/riscv/lmt-safety.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#define TYPE_LAMBERT_SAFETY_VIRT_MACHINE MACHINE_TYPE_NAME("lmt-safety-virt")
OBJECT_DECLARE_SIMPLE_TYPE(LambertSafetyVirt, LAMBERT_SAFETY_VIRT_MACHINE)

struct LambertSafetyVirt {
	MachineState parent_obj;
	Notifier machine_done;

	LambertSafety safety;

	void *fdt;
	int fdt_size;
	uint32_t clock_phandle;

	struct {
		const char *memdev;
	} cfg;
};

static void lmt_safety_virt_virt_mach_done(Notifier *notifier, void *data)
{
	// LambertSafetyVirt *vms = container_of(notifier, LambertSafetyVirt,
	// 										machine_done);
	// MachineState *ms = MACHINE(vms);
	// RISCVCPU *cpu = RISCV_CPU(first_cpu);
}

static void lmt_safety_virt_mach_init(MachineState *machine)
{
	LambertSafetyVirt *vms = LAMBERT_SAFETY_VIRT_MACHINE(machine);
	//MachineClass *mc = MACHINE_GET_CLASS(machine);

	object_initialize_child(OBJECT(machine), "lmt-safety", &vms->safety,
							TYPE_LMT_SAFETY);

	object_property_set_str(OBJECT(&vms->safety), "cpu-type",
									machine->cpu_type, &error_abort);
	object_property_set_link(OBJECT(&vms->safety), "lmt-safety.mem",
							OBJECT(machine->ram), &error_abort);
	object_property_set_uint(OBJECT(&vms->safety), "num-harts", machine->smp.cpus, &error_abort);

	if(vms->cfg.memdev)
		object_property_set_str(OBJECT(&vms->safety), "memdev",
								vms->cfg.memdev, &error_abort);

	sysbus_realize_and_unref(SYS_BUS_DEVICE(&vms->safety), &error_fatal);

	vms->machine_done.notify = lmt_safety_virt_virt_mach_done;
	qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static void lmt_safety_virt_mach_instance_init(Object *obj)
{
}

static void lmt_safety_virt_mach_class_init(ObjectClass *oc, void *data)
{
	MachineClass *mc = MACHINE_CLASS(oc);

	mc->desc = "Lambert Safety Island Virtual Platform";
	mc->init = lmt_safety_virt_mach_init;
	mc->min_cpus = LMT_SAFETY_NR_RISCVS;
	mc->max_cpus = LMT_SAFETY_NR_RISCVS;
	mc->minimum_page_bits = 12;
	mc->default_cpus = LMT_SAFETY_NR_RISCVS;
	mc->default_cpu_type = TYPE_RISCV_CPU_THEAD_E907;
	mc->no_cdrom = 1;
	mc->no_sdcard = 1;
	mc->no_floppy = 1;
	mc->block_default_type = IF_NONE;
	mc->default_ram_id = "lmt-safety.ddr";

	object_class_property_add_str(oc, "memdev", NULL,
					NULL);
}

static const TypeInfo lmt_safety_virt_mach_info = {
	.name       = TYPE_LAMBERT_SAFETY_VIRT_MACHINE,
	.parent     = TYPE_MACHINE,
	.class_init = lmt_safety_virt_mach_class_init,
	.instance_init = lmt_safety_virt_mach_instance_init,
	.instance_size = sizeof(LambertSafetyVirt),
};

static void lmt_safety_virt_machine_init(void)
{
	type_register_static(&lmt_safety_virt_mach_info);
}

type_init(lmt_safety_virt_machine_init)