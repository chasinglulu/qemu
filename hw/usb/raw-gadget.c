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

struct usb_qualifier_descriptor usb_qualifier;
struct usb_device_descriptor usb_device;

// Assigned dynamically.
#define EP_NUM_INT_IN 0x0
struct usb_endpoint_descriptor usb_endpoint = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_DIR_IN | EP_NUM_INT_IN,
	.bmAttributes = USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize = USB_RAW_EP_MAX_PACKET,
	.bInterval = 5,
};
struct usb_config_descriptor usb_config;
struct usb_interface_descriptor usb_interface;

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

static void process_eps_info(int fd) {
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
		if (assign_ep_address(&info.eps[i], &usb_endpoint)) {
			qemu_log("true: %d\n", i);
			continue;
		} else {
			qemu_log("false: %d\n", i);
		}
	}

	int ep_int_in_addr = usb_endpoint_num(&usb_endpoint);
	assert(ep_int_in_addr != 0);
	qemu_log("ep_int_in: addr = %u\n", ep_int_in_addr);
}

/* TODO: delete */
char usb_hid_report[] = {
	0x05, 0x01,                    // Usage Page (Generic Desktop)        0
	0x09, 0x06,                    // Usage (Keyboard)                    2
	0xa1, 0x01,                    // Collection (Application)            4
	0x05, 0x07,                    //  Usage Page (Keyboard)              6
	0x19, 0xe0,                    //  Usage Minimum (224)                8
	0x29, 0xe7,                    //  Usage Maximum (231)                10
	0x15, 0x00,                    //  Logical Minimum (0)                12
	0x25, 0x01,                    //  Logical Maximum (1)                14
	0x75, 0x01,                    //  Report Size (1)                    16
	0x95, 0x08,                    //  Report Count (8)                   18
	0x81, 0x02,                    //  Input (Data,Var,Abs)               20
	0x95, 0x01,                    //  Report Count (1)                   22
	0x75, 0x08,                    //  Report Size (8)                    24
	0x81, 0x01,                    //  Input (Cnst,Arr,Abs)               26
	0x95, 0x03,                    //  Report Count (3)                   28
	0x75, 0x01,                    //  Report Size (1)                    30
	0x05, 0x08,                    //  Usage Page (LEDs)                  32
	0x19, 0x01,                    //  Usage Minimum (1)                  34
	0x29, 0x03,                    //  Usage Maximum (3)                  36
	0x91, 0x02,                    //  Output (Data,Var,Abs)              38
	0x95, 0x05,                    //  Report Count (5)                   40
	0x75, 0x01,                    //  Report Size (1)                    42
	0x91, 0x01,                    //  Output (Cnst,Arr,Abs)              44
	0x95, 0x06,                    //  Report Count (6)                   46
	0x75, 0x08,                    //  Report Size (8)                    48
	0x15, 0x00,                    //  Logical Minimum (0)                50
	0x26, 0xff, 0x00,              //  Logical Maximum (255)              52
	0x05, 0x07,                    //  Usage Page (Keyboard)              55
	0x19, 0x00,                    //  Usage Minimum (0)                  57
	0x2a, 0xff, 0x00,              //  Usage Maximum (255)                59
	0x81, 0x00,                    //  Input (Data,Arr,Abs)               62
	0xc0,                          // End Collection                      64
};

struct hid_class_descriptor {
	__u8  bDescriptorType;
	__le16 wDescriptorLength;
} __attribute__ ((packed));

struct hid_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__le16 bcdHID;
	__u8  bCountryCode;
	__u8  bNumDescriptors;

	struct hid_class_descriptor desc[1];
} __attribute__ ((packed));

struct hid_descriptor usb_hid;
int ep_int_in = -1;

#if 0
static int build_config(char *data, int length) {
	struct usb_config_descriptor *config =
		(struct usb_config_descriptor *)data;
	int total_length = 0;

	assert(length >= sizeof(usb_config));
	memcpy(data, &usb_config, sizeof(usb_config));
	data += sizeof(usb_config);
	length -= sizeof(usb_config);
	total_length += sizeof(usb_config);

	assert(length >= sizeof(usb_interface));
	memcpy(data, &usb_interface, sizeof(usb_interface));
	data += sizeof(usb_interface);
	length -= sizeof(usb_interface);
	total_length += sizeof(usb_interface);

	assert(length >= sizeof(usb_hid));
	memcpy(data, &usb_hid, sizeof(usb_hid));
	data += sizeof(usb_hid);
	length -= sizeof(usb_hid);
	total_length += sizeof(usb_hid);

	assert(length >= USB_DT_ENDPOINT_SIZE);
	memcpy(data, &usb_endpoint, USB_DT_ENDPOINT_SIZE);
	data += USB_DT_ENDPOINT_SIZE;
	length -= USB_DT_ENDPOINT_SIZE;
	total_length += USB_DT_ENDPOINT_SIZE;

	config->wTotalLength = __cpu_to_le16(total_length);
	qemu_log("config->wTotalLength: %d\n", total_length);

	return total_length;
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

static bool usb_ep0_request(int fd, struct usb_raw_control_event *event,
				struct usb_raw_control_io *io) {
	switch (event->ctrl.bRequestType & USB_TYPE_MASK) {
	case USB_TYPE_STANDARD:
		switch (event->ctrl.bRequest) {
		case USB_REQ_GET_DESCRIPTOR:
			switch (event->ctrl.wValue >> 8) {
			case USB_DT_DEVICE:
				memcpy(&io->data[0], &usb_device,
							sizeof(usb_device));
				io->inner.length = sizeof(usb_device);
				return true;
			case USB_DT_DEVICE_QUALIFIER:
				memcpy(&io->data[0], &usb_qualifier,
							sizeof(usb_qualifier));
				io->inner.length = sizeof(usb_qualifier);
				return true;
			case USB_DT_CONFIG:
				io->inner.length =
					build_config(&io->data[0],
						sizeof(io->data));
				return true;
			case USB_DT_STRING:
				io->data[0] = 4;
				io->data[1] = USB_DT_STRING;
				if ((event->ctrl.wValue & 0xff) == 0) {
					io->data[2] = 0x09;
					io->data[3] = 0x04;
				} else {
					io->data[2] = 'x';
					io->data[3] = 0x00;
				}
				io->inner.length = 4;
				return true;
			case HID_DT_REPORT:
				memcpy(&io->data[0], &usb_hid_report[0],
							sizeof(usb_hid_report));
				io->inner.length = sizeof(usb_hid_report);
				return true;
			default:
				qemu_log("fail: no response\n");
				exit(EXIT_FAILURE);
			}
			break;
		case USB_REQ_SET_CONFIGURATION:
			ep_int_in = usb_raw_ep_enable(fd, &usb_endpoint);
			qemu_log("ep0: ep_int_in enabled: %d\n", ep_int_in);
			// int rv = pthread_create(&ep_int_in_thread, 0,
			// 		ep_int_in_loop, (void *)(long)fd);
			// if (rv != 0) {
			// 	perror("pthread_create(ep_int_in)");
			// 	exit(EXIT_FAILURE);
			// }
			// ep_int_in_thread_spawned = true;
			// qemu_log("ep0: spawned ep_int_in thread\n");
			usb_raw_vbus_draw(fd, usb_config.bMaxPower);
			usb_raw_configure(fd);
			io->inner.length = 0;
			return true;
		case USB_REQ_GET_INTERFACE:
			io->data[0] = usb_interface.bAlternateSetting;
			io->inner.length = 1;
			return true;
		default:
			qemu_log("fail: no response\n");
			exit(EXIT_FAILURE);
		}
		break;
	case USB_TYPE_CLASS:
		switch (event->ctrl.bRequest) {
		case HID_REQ_SET_REPORT:
			// This is an OUT request, so don't initialize data.
			io->inner.length = 1;
			return true;
		case HID_REQ_SET_IDLE:
			io->inner.length = 0;
			return true;
		case HID_REQ_SET_PROTOCOL:
			io->inner.length = 0;
			return true;
		default:
			qemu_log("fail: no response\n");
			exit(EXIT_FAILURE);
		}
		break;
	case USB_TYPE_VENDOR:
		switch (event->ctrl.bRequest) {
		default:
			qemu_log("fail: no response\n");
			exit(EXIT_FAILURE);
		}
		break;
	default:
		qemu_log("fail: no response\n");
		exit(EXIT_FAILURE);
	}
}
#endif

void usb_raw_ep0_stall(int fd) {
	int rv = ioctl(fd, USB_RAW_IOCTL_EP0_STALL, 0);
	if (rv < 0) {
		perror("ioctl(USB_RAW_IOCTL_EP0_STALL)");
		exit(EXIT_FAILURE);
	}
}

void *usb_ep0_loop_thread(void *arg) {
	DWC3DeviceState *gadget = (DWC3DeviceState *)arg;
	int fd = gadget->raw_gadget_fd;
	dma_addr_t dma;
	uint32_t evt_typ = 0;

	/* first wait */
	qemu_cond_wait(&gadget->rg_thread_cond, &gadget->mutex);

	while (true) {
		struct usb_raw_control_event event;
		event.inner.type = 0;
		event.inner.length = sizeof(event.ctrl);

		/* mask event interrupt */
		if (gadget->regs[DWC3_GEVNTSIZ(gadget->epnum)] & BIT(31))
			qemu_cond_wait(&gadget->rg_int_mask, &gadget->mutex);

		usb_raw_event_fetch(fd, (struct usb_raw_event *)&event);
		log_event((struct usb_raw_event *)&event);
		// gadget->evt = (struct usb_raw_event *)&event;
		evt_typ = 0;

		if (event.inner.type == USB_RAW_EVENT_CONNECT) {
			process_eps_info(fd);
			continue;
		}

		if (event.inner.type != USB_RAW_EVENT_CONTROL)
			continue;

		dma = gadget->regs[DWC3_GEVNTADRHI(gadget->epnum)];
		dma <<= 32;
		dma |= gadget->regs[DWC3_GEVNTADRLO(gadget->epnum)];

		switch (event.inner.type) {
		case USB_RAW_EVENT_CONNECT:
			process_eps_info(fd);
			// evt_typ |= 0x1;
			// evt_typ = deposit32(evt_typ, 8, 4, DWC3_DEVICE_EVENT_CONNECT_DONE);
			break;
		case USB_RAW_EVENT_CONTROL:
			evt_typ = deposit32(evt_typ, 6, 4, DWC3_DEPEVT_XFERCOMPLETE);
			dma_memory_write(gadget->as, gadget->ctrl_req_addr, &event.ctrl, sizeof(event.ctrl), MEMTXATTRS_UNSPECIFIED);
			break;
		case USB_RAW_EVENT_RESUME:
			// evt_typ |= 0x1;
			// evt_typ = deposit32(evt_typ, 8, 4, DWC3_DEVICE_EVENT_WAKEUP);
			break;
		case USB_RAW_EVENT_RESET:
			// evt_typ |= 0x1;
			// evt_typ = deposit32(evt_typ, 8, 4, DWC3_DEVICE_EVENT_RESET);
			break;
		}

		if (evt_typ) {
			dma_memory_write(gadget->as, dma + gadget->evt_buf_off[gadget->epnum], &evt_typ, sizeof(evt_typ), MEMTXATTRS_UNSPECIFIED);
			qemu_log("event type: 0x%x\n", evt_typ);
			gadget->regs[DWC3_GEVNTCOUNT(gadget->epnum)] += 4;
			gadget->evt_buf_off[gadget->epnum] = (gadget->evt_buf_off[gadget->epnum] + 4) % (gadget->regs[DWC3_GEVNTSIZ(gadget->epnum)] & 0xFFFC);
		}

		// if ((gadget->trb.ctrl & (0x3F << 4)) != DWC3_TRBCTL_CONTROL_DATA) {
		// 	qemu_log("trb ctrl: 0x%x\n", gadget->trb.ctrl);
		// 	continue;
		// }

		/* event notifier */
		qemu_cond_wait(&gadget->rg_event_notifier, &gadget->mutex);

		// if (event.inner.type == USB_RAW_EVENT_RESET) {
		// 	if (ep_int_in_thread_spawned) {
		// 		qemu_log("ep0: stopping ep_int_in thread\n");
		// 		// Even though normally, on a device reset,
		// 		// the endpoint threads should exit due to
		// 		// ESHUTDOWN, let's also attempt to cancel
		// 		// them just in case.
		// 		pthread_cancel(ep_int_in_thread);
		// 		int rv = pthread_join(ep_int_in_thread, NULL);
		// 		if (rv != 0) {
		// 			perror("pthread_join(ep_int_in)");
		// 			exit(EXIT_FAILURE);
		// 		}
		// 		usb_raw_ep_disable(fd, ep_int_in);
		// 		ep_int_in_thread_spawned = false;
		// 		qemu_log("ep0: stopped ep_int_in thread\n");
		// 	}
		// 	continue;
		// }

		struct usb_raw_control_io io;
		io.inner.ep = 0;
		io.inner.flags = 0;
		io.inner.length = 0;


		if ((gadget->trb.ctrl & (0x3F << 4)) == DWC3_TRBCTL_CONTROL_DATA) {
			dma_memory_read(gadget->as, gadget->data_addr, io.data, sizeof(usb_device), MEMTXATTRS_UNSPECIFIED);
			io.inner.length = sizeof(usb_device);
			qemu_hexdump(stderr, "usb device", io.data, sizeof(usb_device));
		}

		// bool reply = usb_ep0_request(fd, &event, &io);
		// if (!reply) {
		// 	qemu_log("ep0: stalling\n");
		// 	usb_raw_ep0_stall(fd);
		// 	continue;
		// }

		if (event.ctrl.wLength < io.inner.length)
			io.inner.length = event.ctrl.wLength;

		if (event.ctrl.bRequestType & USB_DIR_IN) {
			int rv = usb_raw_ep0_write(fd, (struct usb_raw_ep_io *)&io);
			qemu_log("ep0: transferred %d bytes (in)\n", rv);
		} else {
			int rv = usb_raw_ep0_read(fd, (struct usb_raw_ep_io *)&io);
			qemu_log("ep0: transferred %d bytes (out)\n", rv);
		}
	}
}
