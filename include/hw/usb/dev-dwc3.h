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

#define DWC_USB3_DEVICE_NUM_INT 6

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

struct dwc3_event_type {
	uint32_t is_devspec:1;
	uint32_t type:7;
	uint32_t reserved8_31:24;
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

typedef struct DWC3DeviceState {
	uint32_t *regs;
	/* device DMA */
	MemoryRegion *dma_mr;
	AddressSpace *as;

	/* event offset */
	uint32_t evt_buf_off[DWC_USB3_DEVICE_NUM_INT];
	uint32_t epnum;
	hwaddr ctrl_req_addr;
	hwaddr ep0_trb_addr;
	hwaddr data_addr;
	struct dwc3_trb trb;

	int raw_gadget_fd;
	QemuThread ep0_loop_thread;
	bool stop_thread;
	QemuMutex mutex;
	QemuCond rg_thread_cond;
	QemuCond rg_event_notifier;
	QemuCond rg_int_mask;
}DWC3DeviceState;


void dwc3_device_init(DWC3DeviceState *uds);
void dwc3_device_setup_dma(DWC3DeviceState *s);
void dwc3_device_setup_regs(DWC3DeviceState *s, uint32_t *regs);
void dwc3_device_finalize(DWC3DeviceState *s);
uint32_t dwc3_device_get_ep_cmd(DWC3DeviceState *s, int ep);
uint32_t dwc3_device_get_generic_cmd(DWC3DeviceState *s);

#endif