/*
 * Raw Gadget backend implemetion
 *
 * Raw Gadget is a Linux kernel module that implements a low-level
 * interface for the Linux USB Gadget subsystem.
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/register.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/register.h"
#include "sysemu/dma.h"
#include "hw/usb/raw-gadget.h"
#include "qemu/thread.h"
#include "qemu/thread-posix.h"
#include "hw/usb/dev-dwc3.h"
#include "qemu/cutils.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/hid.h>
#include <linux/usb/ch9.h>

struct usb_raw_control_event {
	struct usb_raw_event inner;
	struct usb_ctrlrequest ctrl;
};

static void log_control_request(struct usb_ctrlrequest *ctrl) {
	qemu_log("  bRequestType: 0x%x (%s), bRequest: 0x%x, wValue: 0x%x,"
		" wIndex: 0x%x, wLength: %d\n", ctrl->bRequestType,
		(ctrl->bRequestType & USB_DIR_IN) ? "IN" : "OUT",
		ctrl->bRequest, ctrl->wValue, ctrl->wIndex, ctrl->wLength);

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		qemu_log("  type = USB_TYPE_STANDARD\n");
		break;
	case USB_TYPE_CLASS:
		qemu_log("  type = USB_TYPE_CLASS\n");
		break;
	case USB_TYPE_VENDOR:
		qemu_log("  type = USB_TYPE_VENDOR\n");
		break;
	default:
		qemu_log("  type = unknown = %d\n", (int)ctrl->bRequestType);
		break;
	}

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		switch (ctrl->bRequest) {
		case USB_REQ_GET_DESCRIPTOR:
			qemu_log("  req = USB_REQ_GET_DESCRIPTOR\n");
			switch (ctrl->wValue >> 8) {
			case USB_DT_DEVICE:
				qemu_log("  desc = USB_DT_DEVICE\n");
				break;
			case USB_DT_CONFIG:
				qemu_log("  desc = USB_DT_CONFIG\n");
				break;
			case USB_DT_STRING:
				qemu_log("  desc = USB_DT_STRING\n");
				break;
			case USB_DT_INTERFACE:
				qemu_log("  desc = USB_DT_INTERFACE\n");
				break;
			case USB_DT_ENDPOINT:
				qemu_log("  desc = USB_DT_ENDPOINT\n");
				break;
			case USB_DT_DEVICE_QUALIFIER:
				qemu_log("  desc = USB_DT_DEVICE_QUALIFIER\n");
				break;
			case USB_DT_OTHER_SPEED_CONFIG:
				qemu_log("  desc = USB_DT_OTHER_SPEED_CONFIG\n");
				break;
			case USB_DT_INTERFACE_POWER:
				qemu_log("  desc = USB_DT_INTERFACE_POWER\n");
				break;
			case USB_DT_OTG:
				qemu_log("  desc = USB_DT_OTG\n");
				break;
			case USB_DT_DEBUG:
				qemu_log("  desc = USB_DT_DEBUG\n");
				break;
			case USB_DT_INTERFACE_ASSOCIATION:
				qemu_log("  desc = USB_DT_INTERFACE_ASSOCIATION\n");
				break;
			case USB_DT_SECURITY:
				qemu_log("  desc = USB_DT_SECURITY\n");
				break;
			case USB_DT_KEY:
				qemu_log("  desc = USB_DT_KEY\n");
				break;
			case USB_DT_ENCRYPTION_TYPE:
				qemu_log("  desc = USB_DT_ENCRYPTION_TYPE\n");
				break;
			case USB_DT_BOS:
				qemu_log("  desc = USB_DT_BOS\n");
				break;
			case USB_DT_DEVICE_CAPABILITY:
				qemu_log("  desc = USB_DT_DEVICE_CAPABILITY\n");
				break;
			case USB_DT_WIRELESS_ENDPOINT_COMP:
				qemu_log("  desc = USB_DT_WIRELESS_ENDPOINT_COMP\n");
				break;
			case USB_DT_PIPE_USAGE:
				qemu_log("  desc = USB_DT_PIPE_USAGE\n");
				break;
			case USB_DT_SS_ENDPOINT_COMP:
				qemu_log("  desc = USB_DT_SS_ENDPOINT_COMP\n");
				break;
			case HID_DT_HID:
				qemu_log("  descriptor = HID_DT_HID\n");
				return;
			case HID_DT_REPORT:
				qemu_log("  descriptor = HID_DT_REPORT\n");
				return;
			case HID_DT_PHYSICAL:
				qemu_log("  descriptor = HID_DT_PHYSICAL\n");
				return;
			default:
				qemu_log("  desc = unknown = 0x%x\n",
							ctrl->wValue >> 8);
				break;
			}
			break;
		case USB_REQ_SET_ADDRESS:
			qemu_log("  req = USB_REQ_SET_ADDRESS\n");
			break;
		case USB_REQ_SET_CONFIGURATION:
			qemu_log("  req = USB_REQ_SET_CONFIGURATION\n");
			break;
		case USB_REQ_GET_CONFIGURATION:
			qemu_log("  req = USB_REQ_GET_CONFIGURATION\n");
			break;
		case USB_REQ_SET_INTERFACE:
			qemu_log("  req = USB_REQ_SET_INTERFACE\n");
			break;
		case USB_REQ_GET_INTERFACE:
			qemu_log("  req = USB_REQ_GET_INTERFACE\n");
			break;
		case USB_REQ_GET_STATUS:
			qemu_log("  req = USB_REQ_GET_STATUS\n");
			break;
		case USB_REQ_CLEAR_FEATURE:
			qemu_log("  req = USB_REQ_CLEAR_FEATURE\n");
			break;
		case USB_REQ_SET_FEATURE:
			qemu_log("  req = USB_REQ_SET_FEATURE\n");
			break;
		default:
			qemu_log("  req = unknown = 0x%x\n", ctrl->bRequest);
			break;
		}
		break;
	case USB_TYPE_CLASS:
		switch (ctrl->bRequest) {
		case HID_REQ_GET_REPORT:
			qemu_log("  req = HID_REQ_GET_REPORT\n");
			break;
		case HID_REQ_GET_IDLE:
			qemu_log("  req = HID_REQ_GET_IDLE\n");
			break;
		case HID_REQ_GET_PROTOCOL:
			qemu_log("  req = HID_REQ_GET_PROTOCOL\n");
			break;
		case HID_REQ_SET_REPORT:
			qemu_log("  req = HID_REQ_SET_REPORT\n");
			break;
		case HID_REQ_SET_IDLE:
			qemu_log("  req = HID_REQ_SET_IDLE\n");
			break;
		case HID_REQ_SET_PROTOCOL:
			qemu_log("  req = HID_REQ_SET_PROTOCOL\n");
			break;
		default:
			qemu_log("  req = unknown = 0x%x\n", ctrl->bRequest);
			break;
		}
		break;
	default:
		qemu_log("  req = unknown = 0x%x\n", ctrl->bRequest);
		break;
	}
}

static void log_event(struct usb_raw_event *event) {
	switch (event->type) {
	case USB_RAW_EVENT_CONNECT:
		qemu_log("event: connect, length: %u\n", event->length);
		break;
	case USB_RAW_EVENT_CONTROL:
		qemu_log("event: control, length: %u\n", event->length);
		log_control_request((struct usb_ctrlrequest *)&event->data[0]);
		break;
	case USB_RAW_EVENT_SUSPEND:
		qemu_log("event: suspend\n");
		break;
	case USB_RAW_EVENT_RESUME:
		qemu_log("event: resume, length: %u\n", event->length);
		break;
	case USB_RAW_EVENT_RESET:
		qemu_log("event: reset, length: %u\n", event->length);
		break;
	case USB_RAW_EVENT_DISCONNECT:
		qemu_log("event: disconnect\n");
		break;
	default:
		qemu_log("event: %d (unknown), length: %u\n", event->type, event->length);
	}
}

static bool usb_raw_dt_config(struct usb_ctrlrequest *ctrl)
{
	return ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) &&
			(ctrl->bRequest == USB_REQ_GET_DESCRIPTOR) &&
			((ctrl->wValue >> 8) == USB_DT_CONFIG);
}

static bool usb_req_set_configuration(struct usb_ctrlrequest *ctrl)
{
	return ((ctrl->bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD) &&
			(ctrl->bRequest == USB_REQ_SET_CONFIGURATION);
}

int usb_raw_open(void) {
	int fd = open("/dev/raw-gadget", O_RDWR);
	if (fd < 0) {
		qemu_log("open failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	return fd;
}

void usb_raw_close(int fd) {
	int ret = close(fd);
	if (ret < 0) {
		qemu_log("close failed: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void usb_raw_init(int fd, uint8_t speed, const char *drv, const char *dev)
{
	struct usb_raw_init arg;
	strcpy((char *)&arg.drv_name[0], drv);
	strcpy((char *)&arg.dev_name[0], dev);
	arg.speed = speed;
	int rv = ioctl(fd, USB_RAW_IOCTL_INIT, &arg);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_INIT)");
		exit(EXIT_FAILURE);
	}
}

void usb_raw_run(int fd) {
	int rv = ioctl(fd, USB_RAW_IOCTL_RUN, 0);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_RUN)");
		exit(EXIT_FAILURE);
	}
}

int usb_raw_ep0_read(int fd, struct usb_raw_ep_io *io) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP0_READ, io);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP0_READ)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

int usb_raw_ep0_write(int fd, struct usb_raw_ep_io *io) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP0_WRITE, io);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP0_WRITE)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

static void usb_raw_event_fetch(int fd, struct usb_raw_event *event) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EVENT_FETCH, event);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EVENT_FETCH)");
		exit(EXIT_FAILURE);
	}
}

static int usb_raw_eps_info(int fd, struct usb_raw_eps_info *info) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EPS_INFO, info);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EPS_INFO)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

static bool assign_ep_address(struct usb_raw_ep_info *info,
				struct usb_endpoint_descriptor *ep) {
	if (usb_endpoint_num(ep) != 0)
		return false;  // Already assigned.
	if (usb_endpoint_dir_in(ep) && !info->caps.dir_in)
		return false;
	if (usb_endpoint_dir_out(ep) && !info->caps.dir_out)
		return false;
	if (usb_endpoint_maxp(ep) > info->limits.maxpacket_limit)
		return false;
	switch (usb_endpoint_type(ep)) {
	case USB_ENDPOINT_XFER_BULK:
		if (!info->caps.type_bulk)
			return false;
		break;
	case USB_ENDPOINT_XFER_INT:
		if (!info->caps.type_int)
			return false;
		break;
	default:
		assert(false);
	}
	if (info->addr == USB_RAW_EP_ADDR_ANY) {
		static int addr = 1;
		ep->bEndpointAddress |= addr++;
	} else
		ep->bEndpointAddress |= info->addr;
	return true;
}

static void process_eps_info(int fd, struct usb_endpoint_descriptor *endpoint) {
	struct usb_raw_eps_info info;
	memset(&info, 0, sizeof(info));

	int num = usb_raw_eps_info(fd, &info);
	qemu_log("%s: num = %d\n", __func__, num);
	for (int i = 0; i < num; i++) {
		qemu_log("ep #%d:\n", i);
		qemu_log("  name: %s\n", &info.eps[i].name[0]);
		qemu_log("  addr: %u\n", info.eps[i].addr);
		qemu_log("  type: %s %s %s\n",
			info.eps[i].caps.type_iso ? "iso" : "___",
			info.eps[i].caps.type_bulk ? "blk" : "___",
			info.eps[i].caps.type_int ? "int" : "___");
		qemu_log("  dir : %s %s\n",
			info.eps[i].caps.dir_in ? "in " : "___",
			info.eps[i].caps.dir_out ? "out" : "___");
		qemu_log("  maxpacket_limit: %u\n",
			info.eps[i].limits.maxpacket_limit);
		qemu_log("  max_streams: %u\n", info.eps[i].limits.max_streams);
	}

	for (int i = 0; i < num; i++) {
		if (assign_ep_address(&info.eps[i], endpoint)) {
			qemu_log("true: %d\n", i);
			continue;
		} else {
			qemu_log("false: %d\n", i);
		}
	}

	int ep_addr = usb_endpoint_num(endpoint);
	assert(ep_addr != 0);
	qemu_log("endpoint addr = %u\n", ep_addr);
}


int usb_raw_ep_read(int fd, struct usb_raw_ep_io *io) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP_READ, io);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP_READ)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

int usb_raw_ep_write(int fd, struct usb_raw_ep_io *io) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP_WRITE, io);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP_WRITE)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

static int usb_raw_ep_enable(int fd, struct usb_endpoint_descriptor *desc) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP_ENABLE, desc);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP_ENABLE)");
		exit(EXIT_FAILURE);
	}
	return rv;
}

static void usb_raw_vbus_draw(int fd, uint32_t power) {
	int rv = ioctl(fd, USB_RAW_IOCTL_VBUS_DRAW, power);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_VBUS_DRAW)");
		exit(EXIT_FAILURE);
	}
}

static void usb_raw_configure(int fd) {
	int rv = ioctl(fd, USB_RAW_IOCTL_CONFIGURE, 0);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_CONFIGURED)");
		exit(EXIT_FAILURE);
	}
}

void usb_raw_ep0_stall(int fd) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP0_STALL)");
		exit(EXIT_FAILURE);
	}
}

static uint32_t usb_raw_get_ctrl_epnum(struct usb_ctrlrequest *req)
{
	// return ((req->bRequestType & USB_DIR_IN) ? 0 : 1);
	return 0;
}

static void usb_raw_connet(DWC3DeviceState *s, int buf)
{
	union dwc3_event events[2];

	events[0].raw = dwc3_device_raise_reset();
	events[1].raw = dwc3_device_raise_connect_done();

	dwc3_device_trigger_multi_event(s, buf, events, 2);
}

int ep_bulk_out = -1;
int ep_bulk_in = -1;

#define EP_MAX_PACKET_BULK	512
struct usb_raw_bulk_io {
	struct usb_raw_ep_io inner;
	char data[EP_MAX_PACKET_BULK];
};

pthread_t ep_bulk_out_thread;
pthread_t ep_bulk_in_thread;

struct usb_raw_bulk_io bulk_out_io;
struct usb_raw_bulk_io bulk_in_io;

int usb_raw_memcpy_bulk_out_data(void *dst)
{
	memcpy(dst, bulk_out_io.data, bulk_out_io.inner.length);

	return bulk_out_io.inner.length;
}

void usb_raw_memcpy_bulk_in_data(void *src, int len)
{
	memcpy(bulk_in_io.data, src, len);
	bulk_in_io.inner.length = len;
}

void *ep_bulk_out_loop(void *arg) {
	DWC3DeviceState *gadget = (DWC3DeviceState *)arg;
	int fd = gadget->raw_gadget_fd;
	union dwc3_event event;

	while (true) {
		qemu_log("%s: Entry loop\n", __func__);
		// while (gadget->is_reset) {
		// 	sleep(1);
		// 	qemu_log("bulk out ep on reset\n");
		// }
		if (gadget->is_reset)
			qemu_cond_wait(&gadget->rg_bulk_out_cond, &gadget->mutex);

		assert(ep_bulk_out != -1);
		bulk_out_io.inner.ep = ep_bulk_out;
		bulk_out_io.inner.flags = 0;
		bulk_out_io.inner.length = sizeof(bulk_out_io.data);

		int rv = usb_raw_ep_read(fd, (struct usb_raw_ep_io *)&bulk_out_io);
		qemu_log("bulk_out: read %d bytes\n", rv);
		qemu_hexdump(stderr, "ep_bulk_out", bulk_out_io.data, rv);
		bulk_out_io.inner.length = rv;
		if (rv)
			gadget->is_bulk = true;

		event.raw = dwc3_device_raise_ep0_control(4, DWC3_DEPEVT_XFERNOTREADY, DEPEVT_STATUS_CONTROL_STATUS);
		dwc3_device_trigger_event(gadget, 0, &event);

		qemu_cond_wait(&gadget->rg_bulk_out_cond, &gadget->mutex);
		sleep(1);

		if (gadget->stop_thread)
			break;
	}

	return NULL;
}

void *ep_bulk_in_loop(void *arg) {
	DWC3DeviceState *gadget = (DWC3DeviceState *)arg;
	int fd = gadget->raw_gadget_fd;
	memset(&bulk_in_io, 0, sizeof(bulk_in_io));
	union dwc3_event event;

	while (true) {
		qemu_log("%s: Entry loop\n", __func__);

		if (gadget->is_reset)
			qemu_cond_wait(&gadget->rg_bulk_in_cond, &gadget->mutex);

		qemu_cond_wait(&gadget->rg_bulk_in_cond, &gadget->mutex);
		sleep(1);
		if (gadget->is_bulk) {
			event.raw = dwc3_device_raise_ep0_control(3, DWC3_DEPEVT_XFERNOTREADY, DEPEVT_STATUS_CONTROL_STATUS);
			dwc3_device_trigger_event(gadget, 0, &event);
		}

		while (!bulk_in_io.inner.length) {
			sleep(1);
			qemu_log("bulk_in_io data empty\n");
		}

		assert(ep_bulk_in != -1);
		bulk_in_io.inner.ep = ep_bulk_in;
		bulk_in_io.inner.flags = 0;
		// bulk_in_io.inner.length = sizeof(bulk_in_io.data);

		// for (int i = 0; i < sizeof(io.data); i++)
		// 	io.data[i] = (i % EP_MAX_PACKET_BULK) % 63;

		int rv = usb_raw_ep_write(fd, (struct usb_raw_ep_io *)&bulk_in_io);
		qemu_log("bulk_in: wrote %d bytes\n", rv);
		// qemu_hexdump(stderr, "ep_bulk_in", io.data, rv);

		if (bulk_in_io.inner.length != 13) {
			memset(&bulk_in_io, 0, sizeof(bulk_in_io));
			if (gadget->is_bulk) {
				event.raw = dwc3_device_raise_ep0_control(3, DWC3_DEPEVT_XFERNOTREADY, DEPEVT_STATUS_CONTROL_STATUS);
				dwc3_device_trigger_event(gadget, 0, &event);
			}

			while (!bulk_in_io.inner.length) {
				sleep(1);
				qemu_log("bulk_in_io status empty\n");
			}

			rv = usb_raw_ep_write(fd, (struct usb_raw_ep_io *)&bulk_in_io);
			qemu_log("bulk_in_status: wrote %d bytes\n", rv);
		}

		if (gadget->stop_thread)
			break;
	}

	return NULL;
}

void *usb_ep0_loop_thread(void *arg) {
	DWC3DeviceState *gadget = (DWC3DeviceState *)arg;
	int fd = gadget->raw_gadget_fd;
	uint32_t ep = 0;
	union dwc3_event event;
	struct usb_ctrlrequest *ctrl;

	/* first wait */
	qemu_cond_wait(&gadget->rg_thread_cond, &gadget->mutex);

	while (true) {
		struct usb_raw_control_event raw_event;
		raw_event.inner.type = 0;
		raw_event.inner.length = sizeof(raw_event.ctrl);

		usb_raw_event_fetch(fd, (struct usb_raw_event *)&raw_event);
		log_event((struct usb_raw_event *)&raw_event);
		event.raw = 0;

		switch (raw_event.inner.type) {
		case USB_RAW_EVENT_CONNECT:
			usb_raw_connet(gadget, 0);
			break;
		case USB_RAW_EVENT_RESET:
			if (gadget->is_configured) {
				gadget->is_reset = true;
				gadget->is_set_config = false;
			}
			break;
		case USB_RAW_EVENT_CONTROL:
			ctrl = (struct usb_ctrlrequest *)raw_event.inner.data;
			/* Fake usb set address ctrlreq
			 * dummy_hcd handle that requst in advance
			 * without passing into raw-gadget
			 */
			if (usb_req_set_configuration(ctrl) && !gadget->is_configured) {
				struct usb_ctrlrequest setaddr_ctrl;
				struct usb_config_descriptor config;
				struct usb_endpoint_descriptor endpoint_bulk_out;
				struct usb_endpoint_descriptor endpoint_bulk_in;
				// struct usb_interface_descriptor interface;
				uint32_t no_of_if;
				// uint32_t no_of_ep;
				no_of_if = usb_get_config_descriptor(&config);
				qemu_log("number of interface: %u\n", no_of_if);
				// no_of_ep = usb_get_interface_descriptor(&interface, 0);
				usb_get_endpoint_descriptor(&endpoint_bulk_in, 0, 0);
				process_eps_info(gadget->raw_gadget_fd, &endpoint_bulk_in);
				usb_get_endpoint_descriptor(&endpoint_bulk_out, 0, 1);
				process_eps_info(gadget->raw_gadget_fd, &endpoint_bulk_out);
				if (ep_bulk_out == -1) {
					ep_bulk_out = usb_raw_ep_enable(gadget->raw_gadget_fd,
										&endpoint_bulk_out);
					qemu_log("bulk_out: ep = #%d\n", ep_bulk_out);
				}
				if (ep_bulk_in == -1) {
					ep_bulk_in = usb_raw_ep_enable(gadget->raw_gadget_fd,
									&endpoint_bulk_in);
					qemu_log("bulk_in: ep = #%d\n", ep_bulk_in);
				}

				memset(&setaddr_ctrl, 0x0, sizeof(setaddr_ctrl));
				setaddr_ctrl.bRequestType = USB_TYPE_STANDARD | USB_DIR_OUT;
				setaddr_ctrl.bRequest = USB_REQ_SET_ADDRESS;
				setaddr_ctrl.wValue = cpu_to_le16(5);
				gadget->is_setaddr_ctrlreq = true;
				log_control_request(&setaddr_ctrl);
				ep = usb_raw_get_ctrl_epnum(&setaddr_ctrl);

				event.raw = dwc3_device_raise_ep0_control(0, DWC3_DEPEVT_XFERCOMPLETE, 0);
				dwc3_device_take_ctrl_req(gadget, &setaddr_ctrl, sizeof(setaddr_ctrl));
				dwc3_device_trigger_event(gadget, 0, &event);

				event.raw = dwc3_device_raise_ep0_control(1, DWC3_DEPEVT_XFERNOTREADY, DEPEVT_STATUS_CONTROL_STATUS);
				dwc3_device_trigger_event(gadget, 0, &event);

				/* wait for setaddr ctrlreq completion */
				qemu_cond_wait(&gadget->rg_setaddr_cond, &gadget->mutex);
			}
			gadget->is_dt_conf = usb_raw_dt_config(ctrl);
			ep = usb_raw_get_ctrl_epnum(ctrl);
			event.raw = dwc3_device_raise_ep0_control(ep, DWC3_DEPEVT_XFERCOMPLETE, 0);
			dwc3_device_take_ctrl_req(gadget, &raw_event.ctrl, sizeof(raw_event.ctrl));
			dwc3_device_trigger_event(gadget, 0, &event);
			if (ctrl->bRequest == USB_REQ_SET_CONFIGURATION) {
				usb_raw_vbus_draw(gadget->raw_gadget_fd, 0x32);
				usb_raw_configure(gadget->raw_gadget_fd);
				gadget->is_set_config = true;
			}

			if (!ctrl->wLength) {
				event.raw = dwc3_device_raise_ep0_control(ep, DWC3_DEPEVT_XFERNOTREADY, DEPEVT_STATUS_CONTROL_STATUS);
				dwc3_device_trigger_event(gadget, 0, &event);
			}

			break;
		}

		if ((raw_event.inner.type != USB_RAW_EVENT_CONTROL))
			continue;

		/* event notifier */
		qemu_cond_wait(&gadget->rg_event_notifier, &gadget->mutex);

		if (gadget->stop_thread)
			break;

		qemu_log("44444444444444444\n");

	}

	return NULL;
}
