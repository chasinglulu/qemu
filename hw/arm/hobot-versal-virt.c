/*
 * Horizon Robotics Jounery SoC emulation
 *
 * Copyright (C) 2023 Horizon Robotics Co., Ltd
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
#include "hw/arm/fdt.h"
#include "cpu.h"
#include "hw/qdev-properties.h"
#include "hw/arm/versal-sigi.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"

#define TYPE_HOBOT_VERSAL_VIRT_MACHINE MACHINE_TYPE_NAME("hobot-versal-virt")
OBJECT_DECLARE_SIMPLE_TYPE(HobotVersalVirt, HOBOT_VERSAL_VIRT_MACHINE)

struct HobotVersalVirt {
    MachineState parent_obj;
    Notifier machine_done;

    SigiVirt soc;

    void *fdt;
    int fdt_size;
    uint32_t clock_phandle;
    uint32_t gic_phandle;;
    struct arm_boot_info bootinfo;
};

static void create_fdt(HobotVersalVirt *s)
{
    MachineClass *mc = MACHINE_GET_CLASS(s);

    s->fdt = create_device_tree(&s->fdt_size);
    if (!s->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    /* Allocate all phandles.  */
    s->gic_phandle = qemu_fdt_alloc_phandle(s->fdt);
    s->clock_phandle = qemu_fdt_alloc_phandle(s->fdt);

    /* Create /chosen node for load_dtb.  */
    qemu_fdt_add_subnode(s->fdt, "/chosen");

    /* Header */
    qemu_fdt_setprop_cell(s->fdt, "/", "interrupt-parent", s->gic_phandle);
    qemu_fdt_setprop_cell(s->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(s->fdt, "/", "#address-cells", 0x2);
    qemu_fdt_setprop_string(s->fdt, "/", "model", mc->desc);
    qemu_fdt_setprop_string(s->fdt, "/", "compatible", "hobot-versal-virt");
}

static void *hobot_versal_virt_dtb(const struct arm_boot_info *binfo, int *fdt_size)
{
    const HobotVersalVirt *board = container_of(binfo, HobotVersalVirt,
                                                 bootinfo);

    *fdt_size = board->fdt_size;
    return board->fdt;
}

static void hobot_versal_virt_mach_done(Notifier *notifier, void *data)
{
    HobotVersalVirt *vms = container_of(notifier, HobotVersalVirt,
                                         machine_done);
    MachineState *ms = MACHINE(vms);
    ARMCPU *cpu = ARM_CPU(first_cpu);
    struct arm_boot_info *info = &vms->bootinfo;
    AddressSpace *as = arm_boot_address_space(cpu, info);

    if (arm_load_dtb(info->dtb_start, info, info->dtb_limit, as, ms) < 0) {
        exit(1);
    }
}

static void hobot_versal_virt_mach_init(MachineState *machine)
{
    HobotVersalVirt *vms = HOBOT_VERSAL_VIRT_MACHINE(machine);
    //MemoryRegion *sysmem = get_system_memory();
    int psci_conduit = QEMU_PSCI_CONDUIT_SMC;

    object_initialize_child(OBJECT(machine), "sigi-virt", &vms->soc,
                            TYPE_SIGI_VIRT);
    object_property_set_link(OBJECT(&vms->soc), "sigi-virt.ddr",
                            OBJECT(machine->ram), &error_abort);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(&vms->soc), &error_fatal);

    create_fdt(vms);

    vms->bootinfo.ram_size = machine->ram_size;
    vms->bootinfo.board_id = -1;
    vms->bootinfo.loader_start = base_memmap[VIRT_MEM].base;
    vms->bootinfo.get_dtb = hobot_versal_virt_dtb;
    vms->bootinfo.skip_dtb_autoload = true;
    vms->bootinfo.psci_conduit = psci_conduit;
    arm_load_kernel(ARM_CPU(first_cpu), machine, &vms->bootinfo);

    vms->machine_done.notify = hobot_versal_virt_mach_done;
    qemu_add_machine_init_done_notifier(&vms->machine_done);
}

static void hobot_versal_virt_mach_instance_init(Object *obj)
{
}

static void hobot_versal_virt_mach_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Horizon Robotics Jounery Development Board";
    mc->init = hobot_versal_virt_mach_init;
    mc->min_cpus = SIGI_VIRT_NR_ACPUS;
    mc->max_cpus = 16;
    mc->minimum_page_bits = 12;
    mc->default_cpus = SIGI_VIRT_NR_ACPUS;
    mc->no_cdrom = 1;
    mc->no_sdcard = 1;
    mc->default_ram_id = "sigi-virt.ddr";
}

static const TypeInfo hobot_versal_virt_mach_info = {
    .name       = TYPE_HOBOT_VERSAL_VIRT_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = hobot_versal_virt_mach_class_init,
    .instance_init = hobot_versal_virt_mach_instance_init,
    .instance_size = sizeof(HobotVersalVirt),
};

static void hobot_versal_virt_machine_init(void)
{
    type_register_static(&hobot_versal_virt_mach_info );
}

type_init(hobot_versal_virt_machine_init)
