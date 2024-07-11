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

#ifndef RAW_GADGET_H__
#define RAW_GADGET_H__

#define USB_RAW_NAME_MAX       128
#define USB_RAW_EPS_NUM_MAX    30
#define USB_RAW_EP_NAME_MAX    16
#define USB_RAW_EP_ADDR_ANY    0xff
#define USB_RAW_EP0_MAX_DATA   256
#define USB_RAW_EP_MAX_PACKET  8

struct usb_raw_init {
	uint8_t drv_name[USB_RAW_NAME_MAX];
	uint8_t dev_name[USB_RAW_NAME_MAX];
	uint8_t speed;
};

struct usb_raw_event {
	uint32_t type;
	uint32_t length;
	uint8_t data[];
};

struct usb_raw_ep_io {
	uint16_t ep;
	uint16_t flags;
	uint32_t length;
	uint8_t data[];
};

struct usb_raw_control_io {
	struct usb_raw_ep_io inner;
	char data[USB_RAW_EP0_MAX_DATA];
};

struct usb_raw_int_io {
	struct usb_raw_ep_io inner;
	char data[USB_RAW_EP_MAX_PACKET];
};

struct usb_raw_ep_caps {
	uint32_t type_control:1;
	uint32_t type_iso:1;
	uint32_t type_bulk:1;
	uint32_t type_int:1;
	uint32_t dir_in:1;
	uint32_t dir_out:1;
};

struct usb_raw_ep_limits {
	uint16_t maxpacket_limit;
	uint16_t max_streams;
	uint32_t reserved;
};

struct usb_raw_ep_info {
	uint8_t name[USB_RAW_EP_NAME_MAX];
	uint32_t addr;
	struct usb_raw_ep_caps caps;
	struct usb_raw_ep_limits limits;
};

struct usb_raw_eps_info {
	struct usb_raw_ep_info eps[USB_RAW_EPS_NUM_MAX];
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

#define USB_RAW_IOCTL_INIT          _IOW('U', 0, struct usb_raw_init)
#define USB_RAW_IOCTL_RUN           _IO('U', 1)
#define USB_RAW_IOCTL_EVENT_FETCH   _IOR('U', 2, struct usb_raw_event)
#define USB_RAW_IOCTL_EP0_WRITE     _IOW('U', 3, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP0_READ      _IOWR('U', 4, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_ENABLE     _IOW('U', 5, struct usb_endpoint_descriptor)
#define USB_RAW_IOCTL_EP_DISABLE    _IOW('U', 6, uint32_t)
#define USB_RAW_IOCTL_EP_WRITE      _IOW('U', 7, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_EP_READ       _IOWR('U', 8, struct usb_raw_ep_io)
#define USB_RAW_IOCTL_CONFIGURE     _IO('U', 9)
#define USB_RAW_IOCTL_VBUS_DRAW     _IOW('U', 10, uint32_t)
#define USB_RAW_IOCTL_EPS_INFO      _IOR('U', 11, struct usb_raw_eps_info)
#define USB_RAW_IOCTL_EP0_STALL     _IO('U', 12)
#define USB_RAW_IOCTL_EP_SET_HALT   _IOW('U', 13, uint32_t)
#define USB_RAW_IOCTL_EP_CLEAR_HALT _IOW('U', 14, uint32_t)
#define USB_RAW_IOCTL_EP_SET_WEDGE  _IOW('U', 15, uint32_t)

void usb_raw_init(int fd, uint8_t speed, const char *drv, const char *dev);
int usb_raw_open(void);
void usb_raw_close(int fd);
void usb_raw_run(int fd);

int usb_raw_ep0_write(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep0_read(int fd, struct usb_raw_ep_io *io);
void *usb_ep0_loop_thread(void *arg);
void *ep_bulk_out_loop(void *arg);
void *ep_bulk_in_loop(void *arg);
void usb_raw_ep0_stall(int fd);
int usb_raw_ep_read(int fd, struct usb_raw_ep_io *io);
int usb_raw_ep_write(int fd, struct usb_raw_ep_io *io);

int usb_raw_memcpy_bulk_out_data(void *dst);
void usb_raw_memcpy_bulk_in_data(void *src, int len);

#endif /* RAW_GADGET_H__ */
