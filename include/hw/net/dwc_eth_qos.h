/*
 * QEMU Sysnopsys Ethernet QoS Controller emulation
 *
 * Copyright (C) 2023 Charley<wangkart@aliyun.com>
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINSysnopsys Ethernet QoSENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef DWC_ETH_QOS_H
#define DWC_ETH_QOS_H
#include "qom/object.h"

#define TYPE_DWC_ETHER_QOS "dwc_ether_qos"
OBJECT_DECLARE_SIMPLE_TYPE(DesignwareEtherQoSState, DWC_ETHER_QOS)

#include "net/net.h"
#include "hw/sysbus.h"

/* Last valid Sysnopsys Ethernet QoS address */
#define DWC_ETHER_QOS_MAXREG        (0x1500 / 4)

/* Max number of words in a DMA descriptor.
 * determined by cache line size.
 * cache line size / size of word in byte (64 / 4)
 */
#define DESC_MAX_NUM_WORDS              16

#define MAX_PRIORITY_QUEUES             8
#define MAX_TYPE1_SCREENERS             16
#define MAX_TYPE2_SCREENERS             16

#define EQOS_AXI_WIDTH_32               4
#define EQOS_AXI_WIDTH_64               8
#define EQOS_AXI_WIDTH_128              16

#define MAX_JUMBO_FRAME_SIZE_MASK 0x3FFF
#define MAX_FRAME_SIZE MAX_JUMBO_FRAME_SIZE_MASK

struct DesignwareEtherQoSState {
	/*< private >*/
	SysBusDevice parent_obj;

	/*< public >*/
	MemoryRegion iomem;
	MemoryRegion *dma_mr;
	AddressSpace dma_as;
	NICState *nic;
	NICConf conf;
	qemu_irq irq[MAX_PRIORITY_QUEUES];

	/* Static properties */
	uint8_t num_priority_queues;
	uint8_t num_type1_screeners;
	uint8_t num_type2_screeners;
	uint32_t revision;
	uint16_t jumbo_max_len;

	/* Sysnopsys Ethernet QoS registers backing store */
	uint32_t regs[DWC_ETHER_QOS_MAXREG];
	/* Mask of register bits which are read only */
	uint32_t regs_ro[DWC_ETHER_QOS_MAXREG];


	/* PHY address */
	uint8_t phy_addr;
	/* PHY registers backing store */
	uint16_t phy_regs[32];

	uint8_t phy_loop; /* Are we in phy loopback? */

	uint8_t axi_bus_width;
	/* The current DMA descriptor pointers */
	uint32_t rx_desc_addr[MAX_PRIORITY_QUEUES];
	uint32_t tx_desc_addr[MAX_PRIORITY_QUEUES];

	uint8_t can_rx_state; /* Debug only */

	uint8_t tx_packet[MAX_FRAME_SIZE];
	uint8_t rx_packet[MAX_FRAME_SIZE];
	uint32_t rx_desc[MAX_PRIORITY_QUEUES][DESC_MAX_NUM_WORDS];

	bool sar_active[4];
};

#endif