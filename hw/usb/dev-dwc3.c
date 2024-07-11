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
#include "qemu/cutils.h"

#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/hid.h>
#include <linux/usb/ch9.h>

inline uint32_t dwc3_device_get_ep_cmd(DWC3DeviceState *s, int ep)
{
	return extract32(s->regs[DWC3_DEPCMD(ep)], 0, 4);
}

inline uint32_t dwc3_device_get_generic_cmd(DWC3DeviceState *s)
{
	return extract32(s->regs[DWC3_DGCMD], 0, 8);
}

int32_t dwc3_device_fetch_ctrl_req(DWC3DeviceState *s, void *ctrlreq)
{
	MemTxResult ret = MEMTX_ERROR;
	hwaddr ctrl_req = 0;

	ctrl_req |= s->trb.bph;
	ctrl_req <<= 32;
	ctrl_req |= s->trb.bpl;
	qemu_log("%s: ctrl_req_addr: 0x%lx\n", __func__, ctrl_req);
	s->ctrl_req_addr = ctrl_req;

	ret = dma_memory_read(s->as, ctrl_req, ctrlreq, s->trb.size, MEMTXATTRS_UNSPECIFIED);
	if (!ret)
		return s->trb.size;

	return -ret;
}

int32_t dwc3_device_take_ctrl_req(DWC3DeviceState *s, void *ctrlreq, uint32_t size)
{
	MemTxResult ret = MEMTX_ERROR;
	hwaddr ctrl_req = 0;

	ctrl_req |= s->trb.bph;
	ctrl_req <<= 32;
	ctrl_req |= s->trb.bpl;
	qemu_log("%s: ctrl_req_addr: 0x%lx\n", __func__, ctrl_req);
	s->ctrl_req_addr = ctrl_req;

	ret = dma_memory_write(s->as, ctrl_req, ctrlreq, size, MEMTXATTRS_UNSPECIFIED);
	if (!ret)
		return size;

	return -ret;
}

int32_t dwc3_device_take_bulkout_data(DWC3DeviceState *s, void *data, uint32_t size)
{
	MemTxResult ret = MEMTX_ERROR;
	hwaddr bulk_out_addr = 0;

	bulk_out_addr |= s->trb.bph;
	bulk_out_addr <<= 32;
	bulk_out_addr |= s->trb.bpl;
	qemu_log("%s: bulk_out_addr: 0x%lx\n", __func__, bulk_out_addr);

	ret = dma_memory_write(s->as, bulk_out_addr, data, size, MEMTXATTRS_UNSPECIFIED);
	if (!ret)
		return size;

	return -ret;
}

int32_t dwc3_device_fetch_bulkin_data(DWC3DeviceState *s, void *data)
{
	MemTxResult ret = MEMTX_ERROR;
	hwaddr bulk_in_addr = 0;

	bulk_in_addr |= s->trb.bph;
	bulk_in_addr <<= 32;
	bulk_in_addr |= s->trb.bpl;
	qemu_log("%s: bulk_in_addr: 0x%lx\n", __func__, bulk_in_addr);
	// s->ctrl_data_addr = ctrl_data;

	ret = dma_memory_read(s->as, bulk_in_addr, data, s->trb.size, MEMTXATTRS_UNSPECIFIED);
	if (!ret)
		return s->trb.size;

	return -ret;
}

int32_t dwc3_device_fetch_ctrl_data(DWC3DeviceState *s, void *data, uint32_t size)
{
	MemTxResult ret = MEMTX_ERROR;
	hwaddr ctrl_data = 0;

	ctrl_data |= s->trb.bph;
	ctrl_data <<= 32;
	ctrl_data |= s->trb.bpl;
	qemu_log("%s: ctrl_data_addr: 0x%lx\n", __func__, ctrl_data);
	s->ctrl_data_addr = ctrl_data;

	ret = dma_memory_read(s->as, ctrl_data, data, size, MEMTXATTRS_UNSPECIFIED);
	if (!ret)
		return size;

	return -ret;
}

void dwc3_device_prefetch_trb(DWC3DeviceState *s, int ep)
{
	hwaddr ep0_trb = 0;

	ep0_trb |= s->regs[DWC3_DEPCMDPAR0(ep)];
	ep0_trb <<= 32;
	ep0_trb |= s->regs[DWC3_DEPCMDPAR1(ep)];

	qemu_log("%s: ep0_trb_addr: 0x%lx in ep %d\n", __func__, ep0_trb, ep);
	s->ep0_trb_addr = ep0_trb;
	dma_memory_read(s->as, ep0_trb, &s->trb, sizeof(s->trb), MEMTXATTRS_UNSPECIFIED);
	qemu_log("trb ctrl: 0x%x\n", s->trb.ctrl);
	qemu_log("trb size: 0x%x\n", s->trb.size);
}

void dwc3_device_update_trb(DWC3DeviceState *s, int ep)
{
	hwaddr ep_trb = 0;

	ep_trb |= s->regs[DWC3_DEPCMDPAR0(ep)];
	ep_trb <<= 32;
	ep_trb |= s->regs[DWC3_DEPCMDPAR1(ep)];

	qemu_log("%s: ep_trb_addr: 0x%lx in ep %d\n", __func__, ep_trb, ep);
	// s->ep0_trb_addr = ep_trb;
	dma_memory_write(s->as, ep_trb, &s->trb, sizeof(s->trb), MEMTXATTRS_UNSPECIFIED);
	qemu_log("%s: trb ctrl: 0x%x\n", __func__, s->trb.ctrl);
	qemu_log("%s: trb size: 0x%x\n", __func__, s->trb.size);
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

inline void dwc3_device_setup_regs(DWC3DeviceState *s, uint32_t *regs)
{
	s->regs = regs;
}

void dwc3_device_process_usb_ctrlreq(void *ctrl)
{

}

void dwc3_device_trigger_multi_event(DWC3DeviceState *s, int buf,
				union dwc3_event *events, int num)
{
	struct dwc3_event_buffer *ev_buf = &s->ev_buffs[buf];
	hwaddr dma = ev_buf->dma;
	loff_t offset = ev_buf->lpos;
	int i;

	if (ev_buf->flags & DWC3_EVENT_BUFF_INTMASK) {
		qemu_log("%s: event buffer [%d] interrupt mask\n", __func__, buf);
		return;
	}

	for (i = 0; i < num; i++) {
		dma_memory_write(s->as, dma + offset, &events[i],
				sizeof(*events), MEMTXATTRS_UNSPECIFIED);
		qemu_log("raw event[%d]: 0x%x\n", i, events[i].raw);

		ev_buf->lpos = (offset + 4) % ev_buf->length;
		offset = ev_buf->lpos;
		ev_buf->count += 4;
	}

	if (ev_buf->flags & DWC3_EVENT_BUFF_ENABLED)
		s->regs[DWC3_GEVNTCOUNT(buf)] += 4 * num;
}

void dwc3_device_trigger_event(DWC3DeviceState *s, int buf,
				union dwc3_event *event)
{
	struct dwc3_event_buffer *ev_buf = &s->ev_buffs[buf];
	hwaddr dma = ev_buf->dma;
	loff_t offset = ev_buf->lpos;

	// if (ev_buf->flags & DWC3_EVENT_BUFF_INTMASK) {
	// 	qemu_log("%s: event buffer [%d] interrupt mask\n", __func__, buf);
	// 	return;
	// }

	dma_memory_write(s->as, dma + offset, event,
			sizeof(*event), MEMTXATTRS_UNSPECIFIED);
	qemu_log("raw event: 0x%x\n", event->raw);

	ev_buf->lpos = (offset + 4) % ev_buf->length;
	ev_buf->count += 4;
	if (ev_buf->flags & DWC3_EVENT_BUFF_ENABLED)
		s->regs[DWC3_GEVNTCOUNT(buf)] += 4;
}

uint32_t dwc3_device_raise_connect_done(void)
{
	union dwc3_event event;

	event.type.is_devspec = 0x1;
	event.devt.type = DWC3_DEVICE_EVENT_CONNECT_DONE;

	return event.raw;
}

uint32_t dwc3_device_raise_reset(void)
{
	union dwc3_event event;

	event.type.is_devspec = 0x1;
	event.devt.type = DWC3_DEVICE_EVENT_RESET;

	return event.raw;
}

uint32_t dwc3_device_raise_ep0_control(uint8_t epn, uint8_t epe, uint8_t stat)
{
	union dwc3_event event;

	event.type.is_devspec = 0x0;
	event.depevt.endpoint_number = epn;
	event.depevt.endpoint_event = epe;
	event.depevt.status = stat;

	return event.raw;
}

#define USB_MAXENDPOINTS     16
#define USB_MAXINTERFACES    8
struct usb_interface {
	struct usb_interface_descriptor desc;

	__u8	no_of_ep;
	__u8	num_altsetting;
	__u8	act_altsetting;

	struct usb_endpoint_descriptor ep_desc[USB_MAXENDPOINTS];
	/*
	 * Super Speed Device will have Super Speed Endpoint
	 * Companion Descriptor  (section 9.6.7 of usb 3.0 spec)
	 * Revision 1.0 June 6th 2011
	 */
	struct usb_ss_ep_comp_descriptor ss_ep_comp_desc[USB_MAXENDPOINTS];
} __attribute__ ((packed));

/* Configuration information.. */
struct usb_config {
	struct usb_config_descriptor desc;

	__u8	no_of_if;	/* number of interfaces */
	struct usb_interface if_desc[USB_MAXINTERFACES];
} __attribute__ ((packed));

/*******************************************************************************
 * Parse the config, located in buffer, and fills the dev->config structure.
 * Note that all little/big endian swapping are done automatically.
 * (wTotalLength has already been swapped and sanitized when it was read.)
 */
struct usb_config usbconfig;
int usb_parse_config(unsigned char *buffer, int cfgno)
{
	struct usb_descriptor_header *head;
	int index, ifno, epno, curr_if_num;
	// uint16_t ep_wMaxPacketSize;
	struct usb_interface *if_desc = NULL;
	struct usb_config *config = &usbconfig;

	ifno = -1;
	epno = -1;
	curr_if_num = -1;

	// dev->configno = cfgno;
	head = (struct usb_descriptor_header *) &buffer[0];
	if (head->bDescriptorType != USB_DT_CONFIG) {
		printf(" ERROR: NOT USB_CONFIG_DESC %x\n",
			head->bDescriptorType);
		return -EINVAL;
	}
	if (head->bLength != USB_DT_CONFIG_SIZE) {
		printf("ERROR: Invalid USB CFG length (%d)\n", head->bLength);
		return -EINVAL;
	}
	memcpy(config, head, USB_DT_CONFIG_SIZE);
	config->no_of_if = 0;
	qemu_hexdump(stderr, "usb config: ", config, USB_DT_CONFIG_SIZE);

	index = config->desc.bLength;
	/* Ok the first entry must be a configuration entry,
	 * now process the others */
	head = (struct usb_descriptor_header *) &buffer[index];
	while (index + 1 < config->desc.wTotalLength && head->bLength) {
		switch (head->bDescriptorType) {
		case USB_DT_INTERFACE:
			if (head->bLength != USB_DT_INTERFACE_SIZE) {
				printf("ERROR: Invalid USB IF length (%d)\n",
					head->bLength);
				break;
			}
			if (index + USB_DT_INTERFACE_SIZE > config->desc.wTotalLength) {
				puts("USB IF descriptor overflowed buffer!\n");
				break;
			}
			if (((struct usb_interface_descriptor *) \
				head)->bInterfaceNumber != curr_if_num) {
				/* this is a new interface, copy new desc */
				ifno = config->no_of_if;
				if (ifno >= USB_MAXINTERFACES) {
					puts("Too many USB interfaces!\n");
					/* try to go on with what we have */
					return -EINVAL;
				}
				if_desc = &config->if_desc[ifno];
				config->no_of_if++;
				memcpy(if_desc, head,
					USB_DT_INTERFACE_SIZE);
				if_desc->no_of_ep = 0;
				if_desc->num_altsetting = 1;
				curr_if_num = if_desc->desc.bInterfaceNumber;
			} else {
				/* found alternate setting for the interface */
				if (ifno >= 0) {
					if_desc = &config->if_desc[ifno];
					if_desc->num_altsetting++;
				}
			}
			break;
		case USB_DT_ENDPOINT:
			if (head->bLength != USB_DT_ENDPOINT_SIZE &&
				head->bLength != USB_DT_ENDPOINT_AUDIO_SIZE) {
				printf("ERROR: Invalid USB EP length (%d)\n",
					head->bLength);
				break;
			}
			if (index + head->bLength > config->desc.wTotalLength) {
				puts("USB EP descriptor overflowed buffer!\n");
				break;
			}
			if (ifno < 0) {
				puts("Endpoint descriptor out of order!\n");
				break;
			}
			epno = config->if_desc[ifno].no_of_ep;
			if_desc = &config->if_desc[ifno];
			if (epno >= USB_MAXENDPOINTS) {
				printf("Interface %d has too many endpoints!\n",
					if_desc->desc.bInterfaceNumber);
				return -EINVAL;
			}
			/* found an endpoint */
			if_desc->no_of_ep++;
			memcpy(&if_desc->ep_desc[epno], head,
				USB_DT_ENDPOINT_SIZE);
#if 0
			ep_wMaxPacketSize = get_unaligned(&config->\
							if_desc[ifno].\
							ep_desc[epno].\
							wMaxPacketSize);
			put_unaligned(le16_to_cpu(ep_wMaxPacketSize),
					config->\
					if_desc[ifno].\
					ep_desc[epno].\
					wMaxPacketSize);
#endif
			qemu_log("if %d, ep %d\n", ifno, epno);
			break;
		case USB_DT_SS_ENDPOINT_COMP:
			if (head->bLength != USB_DT_SS_EP_COMP_SIZE) {
				printf("ERROR: Invalid USB EPC length (%d)\n",
					head->bLength);
				break;
			}
			if (index + USB_DT_SS_EP_COMP_SIZE > config->desc.wTotalLength) {
				puts("USB EPC descriptor overflowed buffer!\n");
				break;
			}
			if (ifno < 0 || epno < 0) {
				puts("EPC descriptor out of order!\n");
				break;
			}
			if_desc = &config->if_desc[ifno];
			memcpy(&if_desc->ss_ep_comp_desc[epno], head,
				USB_DT_SS_EP_COMP_SIZE);
			break;
		default:
			if (head->bLength == 0)
				return -EINVAL;

			qemu_log("unknown Description Type : %x\n", head->bDescriptorType);

			unsigned char *ch = (unsigned char *)head;
			int i;

			for (i = 0; i < head->bLength; i++)
				qemu_log("%02X ", *ch++);
			qemu_log("\n\n\n");

			break;
		}
		index += head->bLength;
		head = (struct usb_descriptor_header *)&buffer[index];
	}
	return 0;
}

uint32_t usb_get_config_descriptor(void *config)
{
	memcpy(config, &usbconfig.desc, sizeof(usbconfig.desc));

	return usbconfig.no_of_if;
}
uint32_t usb_get_interface_descriptor(void *interface, int ifno)
{
	memcpy(interface, &usbconfig.if_desc[ifno], sizeof(usbconfig.if_desc[ifno]));

	return usbconfig.if_desc[ifno].no_of_ep;
}
void usb_get_endpoint_descriptor(void *endpoint, int ifno, int ep)
{
	memcpy(endpoint, &usbconfig.if_desc[ifno].ep_desc[ep],
				sizeof(struct usb_endpoint_descriptor));
}


void dwc3_device_init(DWC3DeviceState *s)
{
	memset(s, 0, sizeof(DWC3DeviceState));

	s->raw_gadget_fd = -1;
	s->stop_thread = false;
	s->is_configured = false;
	s->is_reset = false;
	s->is_set_config = false;

	qemu_mutex_init(&s->mutex);
	qemu_cond_init(&s->rg_thread_cond);
	qemu_cond_init(&s->rg_event_notifier);
	qemu_cond_init(&s->rg_setaddr_cond);
	qemu_cond_init(&s->rg_bulk_out_cond);
	qemu_cond_init(&s->rg_bulk_in_cond);
}

void dwc3_device_finalize(DWC3DeviceState *s)
{
	s->stop_thread = true;
	qemu_thread_join(&s->ep0_loop_thread);
	qemu_thread_join(&s->ep_bulk_in_thread);
	qemu_thread_join(&s->ep_bulk_out_thread);

	if (s->raw_gadget_fd > 0)
		usb_raw_close(s->raw_gadget_fd);
	s->raw_gadget_fd = -1;

	qemu_cond_destroy(&s->rg_thread_cond);
	qemu_cond_destroy(&s->rg_event_notifier);
	qemu_cond_destroy(&s->rg_setaddr_cond);
	qemu_cond_destroy(&s->rg_bulk_out_cond);
	qemu_cond_destroy(&s->rg_bulk_in_cond);
	qemu_mutex_destroy(&s->mutex);
}