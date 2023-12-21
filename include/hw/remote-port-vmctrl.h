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

#ifndef REMOTE_PORT_VMCTRL_H
#define REMOTE_PORT_VMCTRL_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"

#include "hw/remote-port.h"
#include "hw/remote-port-device.h"

#define TYPE_REMOTE_PORT_VMCTRL "remote-port-vmctrl"
#define REMOTE_PORT_VMCTRL(obj) \
	OBJECT_CHECK(struct RemotePortVMCtrl, (obj), TYPE_REMOTE_PORT_VMCTRL)

typedef struct RemotePortVMCtrl {
	SysBusDevice parent;

	MemoryRegion iomem;
	struct RemotePort *rp;
	struct rp_peer_state *peer;
} RemotePortVMCtrl;
#endif