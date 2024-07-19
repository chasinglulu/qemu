/*
 * Synopsys DesignWare APB UART emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/char/dwc-apb-uart.h"
#include "hw/qdev-properties.h"

#define TO_REG(addr)    ((addr) / sizeof(uint32_t))

static void dwc_uart_instance_init(Object *obj)
{
	DWCUARTState *s = DWC_UART(obj);

	object_initialize_child(OBJECT(s), "dwc-apb-uart-core",
							&s->uart, TYPE_SERIAL_MM);
	qdev_alias_all_properties(DEVICE(&s->uart), obj);
	qdev_alias_all_properties(DEVICE(&s->uart.serial), obj);
}

static void dwc_uart_reset(DeviceState *dev)
{
	DWCUARTState *s = DWC_UART(dev);

	memset(s->regs, 0, DWC_UART_REG_SIZE);

	device_cold_reset(DEVICE(&s->uart));
}

static uint64_t dwc_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
	DWCUARTState *s = opaque;
	uint32_t val;

	val = s->regs[TO_REG(addr)];

	return (uint64_t)val;
}

static void dwc_uart_write(void *opaque, hwaddr addr, uint64_t val,
							unsigned int size)
{
	DWCUARTState *s = opaque;
	uint32_t val32 = (uint32_t)val;

	s->regs[TO_REG(addr)] = val32;
}

static const MemoryRegionOps dwc_uart_ops = {
	.read = dwc_uart_read,
	.write = dwc_uart_write,
	.endianness = DEVICE_NATIVE_ENDIAN,
	.impl = {
		.min_access_size = 4,
		.max_access_size = 4,
	},
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4,
	}
};

static void dwc_uart_realize(DeviceState *dev, Error **errp)
{
	DWCUARTState *s = DWC_UART(dev);
	SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
	SysBusDevice *sbd_uart = SYS_BUS_DEVICE(&s->uart);
	DeviceState *sdev = DEVICE(&s->uart);

	sdev->id = g_strdup_printf("snps-uart%d", s->index);
	memory_region_init(&s->container, OBJECT(s),
						"synopsys.uart-container", 0x100);
	sysbus_init_mmio(sbd, &s->container);

	memory_region_init_io(&s->iomem, OBJECT(s), &dwc_uart_ops,
							s, TYPE_DWC_UART, DWC_UART_REG_SIZE);
	memory_region_add_subregion(&s->container, 0x20, &s->iomem);

	sysbus_realize(sbd_uart, errp);
	memory_region_add_subregion(&s->container, 0,
								sysbus_mmio_get_region(sbd_uart, 0));

	/* propagate irq */
	sysbus_pass_irq(sbd, sbd_uart);
}

static const VMStateDescription vmstate_dwc_uart = {
	.name = TYPE_DWC_UART,
	.version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_UINT32_ARRAY(regs, DWCUARTState, DWC_UART_NUM_REGS),
		VMSTATE_END_OF_LIST(),
	},
};

static Property dwc_uart_properties[] = {
	DEFINE_PROP_UINT8("index", DWCUARTState, index, 0),
	DEFINE_PROP_END_OF_LIST(),
};

static void dwc_uart_class_init(ObjectClass *classp, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(classp);

	dc->desc = "Synopsys DesignWare APB UART Controller";
	dc->realize = dwc_uart_realize;
	dc->reset = dwc_uart_reset;
	dc->vmsd = &vmstate_dwc_uart;
	device_class_set_props(dc, dwc_uart_properties);
}

static const TypeInfo dwc_uart_info = {
	.name          = TYPE_DWC_UART,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(DWCUARTState),
	.instance_init = dwc_uart_instance_init,
	.class_init    = dwc_uart_class_init,
};

static void dwc_uart_register_types(void)
{
	type_register_static(&dwc_uart_info);
}

type_init(dwc_uart_register_types)