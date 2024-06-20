/*
 * QEMU model of the USB DWC3 host controller emulation.
 *
 * Copyright (c) 2020 Xilinx Inc.
 *
 * Written by Vikram Garhwal<fnu.vikram@xilinx.com>
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
#ifndef UCD_DWC3_H
#define UCD_DWC3_H

#include "qemu/thread.h"
#include "qemu/thread-posix.h"

#include <linux/types.h>
// #include <linux/usb/ch9.h>

#define UDC_NAME_LENGTH_MAX 128

struct usb_raw_init {
	__u8 driver_name[UDC_NAME_LENGTH_MAX];
	__u8 device_name[UDC_NAME_LENGTH_MAX];
	__u8 speed;
};

struct usb_raw_event {
	__u32		type;
	__u32		length;
	__u8		data[];
};

struct usb_raw_ep_io {
	__u16		ep;
	__u16		flags;
	__u32		length;
	__u8		data[];
};

enum usb_raw_event_type {
	USB_RAW_EVENT_INVALID = 0,
	USB_RAW_EVENT_CONNECT = 1,
	USB_RAW_EVENT_CONTROL = 2,
	USB_RAW_EVENT_SUSPEND = 3,
	USB_RAW_EVENT_RESUME = 4,
	USB_RAW_EVENT_RESET = 5,
	USB_RAW_EVENT_DISCONNECT = 6,
};

struct dwc3_device {
	uint32_t *regs;
	MemoryRegion **dma_mr;

	uint32_t epnum;
	int fd;
	struct usb_raw_event *evt;
	struct usb_raw_ep_io *ep_io;

	QemuMutex mutex;
	QemuCond cond;
};

int dwc3_gadget_raw_open(void);
void dwc3_gadget_raw_close(int fd);
void dwc3_gadget_raw_init(int fd, uint8_t speed, const char *driver, const char *device);
void dwc3_gadget_raw_run(int fd);
int dwc3_gadget_raw_ep0_read(int fd, struct usb_raw_ep_io *io);
int dwc3_gadget_raw_ep0_write(int fd, struct usb_raw_ep_io *io);
void *dwc3_gadget_ep0_loop_thread(void *arg);
void usb_raw_ep0_stall(int fd);
int usb_raw_ep0_write(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep0_read(int fd, struct usb_raw_ep_io *io);

#endif