/*
 * QEMU model of the DWC3 USB device controller emulation.
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
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
#ifndef UDC_DWC3_H_
#define UDC_DWC3_H_

#include "hw/usb/raw-gadget.h"
#include "exec/hwaddr.h"

#define DWC_USB3_NUM_EPS           8
#define DWC_USB3_DEVICE_NUM_INT    6

#define DWC3_DGCMD       (0x614 / 4)

#define DWC3_DEPCMD_CMDACT_OFFSET    10
#define DWC3_DEPCMD_CMDACT           (1 << DWC3_DEPCMD_CMDACT_OFFSET)
#define DWC3_DEPCMD_CMDIOC_OFFSET    8
#define DWC3_DEPCMD_CMDIOC           (1 << 8)

#define DWC3_DEPCMDPAR2(n)    ((0x700 + (n * 0x10)) / 4)
#define DWC3_DEPCMDPAR1(n)    ((0x704 + (n * 0x10)) / 4)
#define DWC3_DEPCMDPAR0(n)    ((0x708 + (n * 0x10)) / 4)
#define DWC3_DEPCMD(n)        ((0x70c + (n * 0x10)) / 4)

#define DWC3_GEVNTADRLO(n)    ((0x300 + (n * 0x10)) / 4)
#define DWC3_GEVNTADRHI(n)    ((0x304 + (n * 0x10)) / 4)
#define DWC3_GEVNTSIZ(n)      ((0x308 + (n * 0x10)) / 4)
#define DWC3_GEVNTCOUNT(n)    ((0x30c + (n * 0x10)) / 4)

#define DWC3_DEVICE_EVENT_DISCONNECT            0
#define DWC3_DEVICE_EVENT_RESET                 1
#define DWC3_DEVICE_EVENT_CONNECT_DONE          2
#define DWC3_DEVICE_EVENT_LINK_STATUS_CHANGE    3
#define DWC3_DEVICE_EVENT_WAKEUP                4
#define DWC3_DEVICE_EVENT_HIBER_REQ             5
#define DWC3_DEVICE_EVENT_EOPF                  6
#define DWC3_DEVICE_EVENT_SOF                   7
#define DWC3_DEVICE_EVENT_ERRATIC_ERROR         9
#define DWC3_DEVICE_EVENT_CMD_CMPL              10
#define DWC3_DEVICE_EVENT_OVERFLOW              11

#define DWC3_DEPEVT_XFERCOMPLETE             0x01
#define DWC3_DEPEVT_XFERINPROGRESS           0x02
#define DWC3_DEPEVT_XFERNOTREADY             0x03
#define DWC3_DEPEVT_RXTXFIFOEVT              0x04
#define DWC3_DEPEVT_STREAMEVT                0x06
#define DWC3_DEPEVT_EPCMDCMPLT               0x07

/* Control-only Status */
#define DEPEVT_STATUS_CONTROL_DATA           1
#define DEPEVT_STATUS_CONTROL_STATUS         2

/* TRB Control */
#define DWC3_TRB_CTRL_HWO            (1 << 0)
#define DWC3_TRB_CTRL_LST            (1 << 1)
#define DWC3_TRB_CTRL_CHN            (1 << 2)
#define DWC3_TRB_CTRL_CSP            (1 << 3)
#define DWC3_TRB_CTRL_TRBCTL(n)      (((n) & 0x3f) << 4)
#define DWC3_TRB_CTRL_ISP_IMI        (1 << 10)
#define DWC3_TRB_CTRL_IOC            (1 << 11)
#define DWC3_TRB_CTRL_SID_SOFN(n)    (((n) & 0xffff) << 14)

#define DWC3_TRBCTL_NORMAL              DWC3_TRB_CTRL_TRBCTL(1)
#define DWC3_TRBCTL_CONTROL_SETUP       DWC3_TRB_CTRL_TRBCTL(2)
#define DWC3_TRBCTL_CONTROL_STATUS2     DWC3_TRB_CTRL_TRBCTL(3)
#define DWC3_TRBCTL_CONTROL_STATUS3     DWC3_TRB_CTRL_TRBCTL(4)
#define DWC3_TRBCTL_CONTROL_DATA        DWC3_TRB_CTRL_TRBCTL(5)
#define DWC3_TRBCTL_ISOCHRONOUS_FIRST   DWC3_TRB_CTRL_TRBCTL(6)
#define DWC3_TRBCTL_ISOCHRONOUS         DWC3_TRB_CTRL_TRBCTL(7)
#define DWC3_TRBCTL_LINK_TRB            DWC3_TRB_CTRL_TRBCTL(8)

/* Device Physical Endpoint-specific Command Register */
#define DWC3_DEPCMD_DEPSTARTCFG        (0x09 << 0)
#define DWC3_DEPCMD_ENDTRANSFER        (0x08 << 0)
#define DWC3_DEPCMD_UPDATETRANSFER     (0x07 << 0)
#define DWC3_DEPCMD_STARTTRANSFER      (0x06 << 0)
#define DWC3_DEPCMD_CLEARSTALL         (0x05 << 0)
#define DWC3_DEPCMD_SETSTALL           (0x04 << 0)
#define DWC3_DEPCMD_GETEPSTATE         (0x03 << 0)
#define DWC3_DEPCMD_SETTRANSFRESOURCE  (0x02 << 0)
#define DWC3_DEPCMD_SETEPCONFIG        (0x01 << 0)

/* Device Generic Command Register */
#define DWC3_DGCMD_SET_PERIODIC_PAR         0x02
#define DWC3_DGCMD_SET_SCRATCHPAD_ADDR_LO   0x04
#define DWC3_DGCMD_SET_SCRATCHPAD_ADDR_HI   0x05
#define DWC3_DGCMD_TRAN_DEV_NOTIFI          0x07
#define DWC3_DGCMD_SELECTED_FIFO_FLUSH      0x09
#define DWC3_DGCMD_ALL_FIFO_FLUSH           0x0a
#define DWC3_DGCMD_SET_ENDPOINT_NRDY        0x0c
#define DWC3_DGCMD_RUN_SOC_BUS_LOOPBACK     0x10
#define DWC3_DGCMD_RESTART_AFTER_DISCONNECT 0x11

/* DEPCFG parameter 1 */
#define DWC3_DEPCFG_INT_NUM(n)		((n) << 0)
#define DWC3_DEPCFG_XFER_COMPLETE_EN	(1 << 8)
#define DWC3_DEPCFG_XFER_IN_PROGRESS_EN	(1 << 9)
#define DWC3_DEPCFG_XFER_NOT_READY_EN	(1 << 10)
#define DWC3_DEPCFG_FIFO_ERROR_EN	(1 << 11)
#define DWC3_DEPCFG_STREAM_EVENT_EN	(1 << 13)
#define DWC3_DEPCFG_BINTERVAL_M1(n)	((n) << 16)
#define DWC3_DEPCFG_STREAM_CAPABLE	(1 << 24)
#define DWC3_DEPCFG_EP_NUMBER(n)	((n) << 25)
#define DWC3_DEPCFG_BULK_BASED		(1 << 30)
#define DWC3_DEPCFG_FIFO_BASED		(1 << 31)

/* DEPCFG parameter 0 */
#define DWC3_DEPCFG_EP_TYPE(n)		((n) << 1)
#define DWC3_DEPCFG_MAX_PACKET_SIZE(n)	((n) << 3)
#define DWC3_DEPCFG_FIFO_NUMBER(n)	((n) << 17)
#define DWC3_DEPCFG_BURST_SIZE(n)	((n) << 22)
#define DWC3_DEPCFG_DATA_SEQ_NUM(n)	((n) << 26)
#define DWC3_DEPCFG_ACTION_INIT		(0 << 30)
#define DWC3_DEPCFG_ACTION_RESTORE	(1 << 30)
#define DWC3_DEPCFG_ACTION_MODIFY	(2 << 30)

struct dwc3_event_type {
	uint32_t is_devspec:1;
	uint32_t type:7;
	uint32_t reserved8_31:24;
};

/**
 * struct dwc3_event_depvt - Device Endpoint Events
 * @one_bit: indicates this is an endpoint event (not used)
 * @endpoint_number: number of the endpoint
 * @endpoint_event: The event we have:
 *	0x00	- Reserved
 *	0x01	- XferComplete
 *	0x02	- XferInProgress
 *	0x03	- XferNotReady
 *	0x04	- RxTxFifoEvt (IN->Underrun, OUT->Overrun)
 *	0x05	- Reserved
 *	0x06	- StreamEvt
 *	0x07	- EPCmdCmplt
 * @reserved11_10: Reserved, don't use.
 * @status: Indicates the status of the event. Refer to databook for
 *	more information.
 * @parameters: Parameters of the current event. Refer to databook for
 *	more information.
 */
struct dwc3_event_depevt {
	uint32_t one_bit:1;
	uint32_t endpoint_number:5;
	uint32_t endpoint_event:4;
	uint32_t reserved11_10:2;
	uint32_t status:4;
	uint32_t parameters:16;
};

/**
 * struct dwc3_event_devt - Device Events
 * @one_bit: indicates this is a non-endpoint event (not used)
 * @device_event: indicates it's a device event. Should read as 0x00
 * @type: indicates the type of device event.
 *	0	- DisconnEvt
 *	1	- USBRst
 *	2	- ConnectDone
 *	3	- ULStChng
 *	4	- WkUpEvt
 *	5	- Reserved
 *	6	- EOPF
 *	7	- SOF
 *	8	- Reserved
 *	9	- ErrticErr
 *	10	- CmdCmplt
 *	11	- EvntOverflow
 *	12	- VndrDevTstRcved
 * @reserved15_12: Reserved, not used
 * @event_info: Information about this event
 * @reserved31_25: Reserved, not used
 */
struct dwc3_event_devt {
	uint32_t one_bit:1;
	uint32_t device_event:7;
	uint32_t type:4;
	uint32_t reserved15_12:4;
	uint32_t event_info:9;
	uint32_t reserved31_25:7;
};

/**
 * struct dwc3_event_gevt - Other Core Events
 * @one_bit: indicates this is a non-endpoint event (not used)
 * @device_event: indicates it's (0x03) Carkit or (0x04) I2C event.
 * @phy_port_number: self-explanatory
 * @reserved31_12: Reserved, not used.
 */
struct dwc3_event_gevt {
	uint32_t one_bit:1;
	uint32_t device_event:7;
	uint32_t phy_port_number:4;
	uint32_t reserved31_12:20;
};

/**
 * union dwc3_event - representation of Event Buffer contents
 * @raw: raw 32-bit event
 * @type: the type of the event
 * @depevt: Device Endpoint Event
 * @devt: Device Event
 * @gevt: Global Event
 */
union dwc3_event {
	uint32_t raw;
	struct dwc3_event_type type;
	struct dwc3_event_depevt depevt;
	struct dwc3_event_devt devt;
	struct dwc3_event_gevt gevt;
};

/**
 * struct dwc3_trb - transfer request block (hw format)
 * @bpl: DW0-3
 * @bph: DW4-7
 * @size: DW8-B
 * @trl: DWC-F
 */
struct dwc3_trb {
	uint32_t bpl;
	uint32_t bph;
	uint32_t size;
	uint32_t ctrl;
};

/**
 * struct dwc3_event_buffer - event buffer representation
 * @length: size of this buffer
 * @lpos: event offset
 * @count: count of pending event in buffer
 * @flags: flags related to this event buffer
 * @dma: dma_addr_t
 */
struct dwc3_event_buffer {
	uint32_t length;
	uint32_t lpos;
	uint32_t count;
	uint32_t flags;

#define DWC3_EVENT_BUFF_ENABLED  (1UL << 0)
#define DWC3_EVENT_BUFF_INTMASK  (1UL << 1)

	hwaddr dma;
};

struct dwc3_ep_config {
	uint32_t usb_ep_num:5;
	uint32_t depevten:6;
	uint32_t intr_num:5;
	uint32_t brst_sz:4;
	uint32_t fifo_num:5;
	uint32_t max_packet_size:11;
	uint32_t ep_type:2;
	uint32_t saved_state;
};

typedef struct DWC3DeviceState {
	uint32_t *regs;
	/* device DMA */
	MemoryRegion *dma_mr;
	AddressSpace *as;

	/* event offset */
	struct dwc3_event_buffer ev_buffs[DWC_USB3_DEVICE_NUM_INT];
	struct dwc3_ep_config eps[DWC_USB3_NUM_EPS];
	bool is_dt_conf;
	bool is_setaddr_ctrlreq;
	bool is_configured;
	bool is_reset;
	bool is_set_config;
	hwaddr ctrl_req_addr;
	hwaddr ep0_trb_addr;
	hwaddr ctrl_data_addr;
	struct dwc3_trb trb;
	bool is_bulk;

	int raw_gadget_fd;
	QemuThread ep0_loop_thread;
	QemuThread ep_bulk_out_thread;
	QemuThread ep_bulk_in_thread;
	bool stop_thread;
	QemuMutex mutex;
	QemuCond rg_thread_cond;
	QemuCond rg_event_notifier;
	QemuCond rg_setaddr_cond;

	QemuCond rg_bulk_out_cond;
	QemuCond rg_bulk_in_cond;
} DWC3DeviceState;


void dwc3_device_init(DWC3DeviceState *uds);
void dwc3_device_setup_dma(DWC3DeviceState *s);
void dwc3_device_setup_regs(DWC3DeviceState *s, uint32_t *regs);
void dwc3_device_finalize(DWC3DeviceState *s);
uint32_t dwc3_device_get_ep_cmd(DWC3DeviceState *s, int ep);
uint32_t dwc3_device_get_generic_cmd(DWC3DeviceState *s);
int32_t dwc3_device_fetch_ctrl_req(DWC3DeviceState *s, void *ctrlreq);
int32_t dwc3_device_take_ctrl_req(DWC3DeviceState *s, void *ctrlreq, uint32_t size);
int32_t dwc3_device_fetch_ctrl_data(DWC3DeviceState *s, void *data, uint32_t size);
void dwc3_device_prefetch_trb(DWC3DeviceState *s, int ep);
void dwc3_device_update_trb(DWC3DeviceState *s, int ep);
void dwc3_device_get_desc(DWC3DeviceState *s);
void dwc3_device_update_status(DWC3DeviceState *s);
void dwc3_device_process_usb_ctrlreq(void *ctrl);
uint32_t dwc3_device_raise_connect_done(void);
uint32_t dwc3_device_raise_reset(void);
uint32_t dwc3_device_raise_ep0_control(uint8_t epn, uint8_t epe, uint8_t stat);
void dwc3_device_trigger_event(DWC3DeviceState *s, int buf, union dwc3_event *event);
void dwc3_device_trigger_multi_event(DWC3DeviceState *s, int buf, union dwc3_event *event, int num);

int32_t dwc3_device_take_bulkout_data(DWC3DeviceState *s, void *data, uint32_t size);
int32_t dwc3_device_fetch_bulkin_data(DWC3DeviceState *s, void *data);

int usb_parse_config(unsigned char *buffer, int cfgno);
uint32_t usb_get_config_descriptor(void *config);
uint32_t usb_get_interface_descriptor(void *interface, int ifno);
void usb_get_endpoint_descriptor(void *endpoint, int ifno, int ep);


#endif