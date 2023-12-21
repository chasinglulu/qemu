/*
 * Virtual Machine Controller emulation
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
 */


#include "qemu/osdep.h"
#include "qemu/help-texts.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/hw.h"
#include "migration/vmstate.h"
#include "qemu/cutils.h"
#include "qemu/log.h"
#include "hw/remote-port-vmctrl.h"

static void rp_vm_ctrl_reset(DeviceState *dev)
{
}

static void rm_vm_ctrl_write(void *opaque, hwaddr offset, uint64_t val,
								unsigned size)
{
	RemotePortVMCtrl *s = REMOTE_PORT_VMCTRL(opaque);
	struct rp_pkt_vm_ctrl pkt;
	size_t len;

	assert(s->rp);

	switch (offset) {
	case 0x00:
		qemu_log_mask(LOG_STRACE, "start vm\n");
		len = rp_encode_vm_ctrl(s->rp->current_id++, 0, &pkt, RP_VM_CTRL_START, 0);
		rp_write(s->rp, &pkt, len);
		break;
	case 0x04:
		qemu_log_mask(LOG_STRACE, "set pc addr: 0x%lx\n", val);
		len = rp_encode_vm_ctrl(s->rp->current_id++, 0, &pkt, RP_VM_CTRL_SET_PC, val);
		rp_write(s->rp, &pkt, len);
		break;
	default:
		assert(0);
		break;
	}
}

static const MemoryRegionOps rp_vmctrl_ops = {
	.write = rm_vm_ctrl_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_vm_ctrl_realize(DeviceState *dev, Error **errp)
{
	struct RemotePortVMCtrl *s = REMOTE_PORT_VMCTRL(dev);

	memory_region_init_io(&s->iomem, OBJECT(s), &rp_vmctrl_ops, s,
							"vm-ctrl", 0x10);
	sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void rp_vm_ctrl_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = rp_vm_ctrl_realize;
	dc->reset = rp_vm_ctrl_reset;
}

static void rp_vm_ctrl_init(Object *obj)
{
	RemotePortVMCtrl *s = REMOTE_PORT_VMCTRL(obj);
	object_property_add_link(obj, "rp-adaptor0", "remote-port",
								(Object **)&s->rp,
								object_property_allow_set_link,
								OBJ_PROP_LINK_STRONG);
}

static const TypeInfo rp_vm_ctrl_info = {
	.name          = TYPE_REMOTE_PORT_VMCTRL,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(struct RemotePortVMCtrl),
	.instance_init = rp_vm_ctrl_init,
	.class_init    = rp_vm_ctrl_class_init,
};

static void rp_vm_ctrl_register_types(void)
{
	type_register_static(&rp_vm_ctrl_info);
}

type_init(rp_vm_ctrl_register_types)
