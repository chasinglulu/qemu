/*
 * QEMU model of the USB DWC3 device controller emulation.
 *
 * Copyright (C) 2024 Charleye<wangkart@aliyun.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/register.h"
#include "hw/usb/dev-dwc3.h"
#include "sysemu/dma.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/hid.h>
#include <linux/usb/ch9.h>

uint32_t dwc3_device_get_ep_cmd(DWC3DeviceState *s, int ep)
{
	return extract32(s->regs[DWC3_DEPCMD(ep)], 0, 4);
}

uint32_t dwc3_device_get_generic_cmd(DWC3DeviceState *s)
{
	return extract32(s->regs[DWC3_DGCMD], 0, 8);
}

void dwc3_device_init(DWC3DeviceState *s)
{
	memset(s, 0, sizeof(DWC3DeviceState));

	s->raw_gadget_fd = -1;
	s->epnum = -1;
	s->stop_thread = false;

	qemu_mutex_init(&s->mutex);
	qemu_cond_init(&s->rg_thread_cond);
	qemu_cond_init(&s->rg_event_notifier);
	qemu_cond_init(&s->rg_int_mask);
}

void dwc3_device_setup_dma(DWC3DeviceState *s)
{
	if (s->dma_mr) {
		s->as = g_malloc0(sizeof(AddressSpace));
		address_space_init(s->as, s->dma_mr, "dwc3-device-dma");
	} else {
		s->as = &address_space_memory;
	}
}

void dwc3_device_setup_regs(DWC3DeviceState *s, uint32_t *regs)
{
	s->regs = regs;
}

void dwc3_device_finalize(DWC3DeviceState *s)
{
	s->stop_thread = true;
	qemu_thread_join(&s->ep0_loop_thread);

	if (s->raw_gadget_fd > 0)
		usb_raw_close(s->raw_gadget_fd);
	s->raw_gadget_fd = -1;
	s->epnum = -1;

	qemu_cond_destroy(&s->rg_thread_cond);
	qemu_cond_destroy(&s->rg_event_notifier);
	qemu_cond_destroy(&s->rg_int_mask);
	qemu_mutex_destroy(&s->mutex);
}