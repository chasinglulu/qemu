/*
 * DW UART emulation
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
 *
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
#include "hw/char/dw_uart.h"
#include "hw/qdev-properties.h"

#define TO_REG(addr)    ((addr) / sizeof(uint32_t))

static void dw_uart_instance_init(Object *obj)
{
	DWUARTState *s = DW_UART(obj);

	object_initialize_child(OBJECT(s), "designware-uart",
							&s->uart, TYPE_SERIAL_MM);
	qdev_alias_all_properties(DEVICE(&s->uart), obj);
	qdev_alias_all_properties(DEVICE(&s->uart.serial), obj);
}

static void dw_uart_reset(DeviceState *dev)
{
	DWUARTState *s = DW_UART(dev);

	memset(s->regs, 0, DW_UART_REG_SIZE);

	device_cold_reset(DEVICE(&s->uart));
}

static uint64_t dw_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
	DWUARTState *s = opaque;
	uint32_t val;

	val = s->regs[TO_REG(addr)];

	return (uint64_t)val;
}

static void dw_uart_write(void *opaque, hwaddr addr, uint64_t val,
							unsigned int size)
{
	DWUARTState *s = opaque;
	uint32_t val32 = (uint32_t)val;

	s->regs[TO_REG(addr)] = val32;
}

static const MemoryRegionOps dw_uart_ops = {
	.read = dw_uart_read,
	.write = dw_uart_write,
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

static void dw_uart_realize(DeviceState *dev, Error **errp)
{
	DWUARTState *s = DW_UART(dev);
	SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
	SysBusDevice *sbd_uart = SYS_BUS_DEVICE(&s->uart);
	DeviceState *sdev = DEVICE(&s->uart);

	sdev->id = g_strdup_printf("snps-uart%d", s->index);
	memory_region_init(&s->container, OBJECT(s),
						"synopsys.uart-container", 0x100);
	sysbus_init_mmio(sbd, &s->container);

	memory_region_init_io(&s->iomem, OBJECT(s), &dw_uart_ops,
							s, TYPE_DW_UART, DW_UART_REG_SIZE);
	memory_region_add_subregion(&s->container, 0x20, &s->iomem);

	sysbus_realize(sbd_uart, errp);
	memory_region_add_subregion(&s->container, 0,
								sysbus_mmio_get_region(sbd_uart, 0));

	/* propagate irq */
	sysbus_pass_irq(sbd, sbd_uart);
}

static const VMStateDescription vmstate_dw_uart = {
	.name = TYPE_DW_UART,
	.version_id = 1,
	.fields = (VMStateField[]) {
		VMSTATE_UINT32_ARRAY(regs, DWUARTState, DW_UART_NUM_REGS),
		VMSTATE_END_OF_LIST(),
	},
};

static Property dw_uart_properties[] = {
	DEFINE_PROP_UINT8("index", DWUARTState, index, 0),
	DEFINE_PROP_END_OF_LIST(),
};

static void dw_uart_class_init(ObjectClass *classp, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(classp);

	dc->desc = "Synopsys DesignWare UART Controller";
	dc->realize = dw_uart_realize;
	dc->reset = dw_uart_reset;
	dc->vmsd = &vmstate_dw_uart;
	device_class_set_props(dc, dw_uart_properties);
}

static const TypeInfo dw_uart_info = {
	.name          = TYPE_DW_UART,
	.parent        = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(DWUARTState),
	.instance_init = dw_uart_instance_init,
	.class_init    = dw_uart_class_init,
};

static void dw_uart_register_types(void)
{
	type_register_static(&dw_uart_info);
}

type_init(dw_uart_register_types)