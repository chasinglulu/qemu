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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <zlib.h> /* For crc32 */

#include "hw/irq.h"
#include "hw/net/dwc_eth_qos.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/dma.h"
#include "net/checksum.h"
#include "net/eth.h"
#include "qemu/cutils.h"

#define DWC_ETHER_QOS_ERR_DEBUG 0
#define DB_PRINT(...) do {             \
	if (DWC_ETHER_QOS_ERR_DEBUG) {       \
		qemu_log(": %s: ", __func__);  \
		qemu_log(__VA_ARGS__);         \
	}                                  \
} while (0)

#define DWC_ETHER_QOS_MAC_CFG                    (0x00000000 / 4) /* MAC Configuration reg */
#define DWC_ETHER_QOS_MAC_Q0TXFLOWCTRL           (0x00000070 / 4) /* MAC Flow Control reg */
#define DWC_ETHER_QOS_MAC_RXFLOWCTRL             (0x00000090 / 4) /* MAC Receive Flow Control reg */
#define DWC_ETHER_QOS_MAC_TXQPRTYMAP0            (0x00000098 / 4) /* MAC Transmit Queue Priority Mapping 0 reg */
#define DWC_ETHER_QOS_MAC_RXQ_CTRL0              (0x000000a0 / 4)
#define DWC_ETHER_QOS_MAC_RXQ_CTRL2              (0x000000a8 / 4)
#define DWC_ETHER_QOS_MAC_USTICCOUNTER           (0x000000dc / 4)
#define DWC_ETHER_QOS_MAC_HWFEATURE0             (0x0000011c / 4)
#define DWC_ETHER_QOS_MAC_HWFEATURE1             (0x00000120 / 4)
#define DWC_ETHER_QOS_MAC_HWFEATURE2             (0x00000124 / 4)
#define DWC_ETHER_QOS_MAC_MDIOADDRESS            (0x00000200 / 4)
#define DWC_ETHER_QOS_MAC_MDIODATA               (0x00000204 / 4)
#define DWC_ETHER_QOS_MAC_ADDRESS0HIGH           (0x00000300 / 4)
#define DWC_ETHER_QOS_MAC_ADDRESS0LOW            (0x00000304 / 4)

#define EQOS_MAC_CONFIGURATION_IPC               BIT(27)
#define EQOS_MAC_CONFIGURATION_GPSLCE            BIT(23)
#define EQOS_MAC_CONFIGURATION_CST               BIT(21)
#define EQOS_MAC_CONFIGURATION_ACS               BIT(20)
#define EQOS_MAC_CONFIGURATION_WD                BIT(19)
#define EQOS_MAC_CONFIGURATION_JD                BIT(17)
#define EQOS_MAC_CONFIGURATION_JE                BIT(16)
#define EQOS_MAC_CONFIGURATION_PS                BIT(15)
#define EQOS_MAC_CONFIGURATION_FES               BIT(14)
#define EQOS_MAC_CONFIGURATION_DM                BIT(13)
#define EQOS_MAC_CONFIGURATION_LM                BIT(12)
#define EQOS_MAC_CONFIGURATION_TE                BIT(1)
#define EQOS_MAC_CONFIGURATION_RE                BIT(0)

#define EQOS_MAC_Q0_TX_FLOW_CTRL_PT_SHIFT        16
#define EQOS_MAC_Q0_TX_FLOW_CTRL_PT_MASK         0xffff
#define EQOS_MAC_Q0_TX_FLOW_CTRL_TFE             BIT(1)

#define EQOS_MAC_RX_FLOW_CTRL_RFE                BIT(0)

#define EQOS_MAC_TXQ_PRTY_MAP0_PSTQ0_SHIFT       0
#define EQOS_MAC_TXQ_PRTY_MAP0_PSTQ0_MASK        0xff

#define EQOS_MAC_RXQ_CTRL0_RXQ0EN_SHIFT          0
#define EQOS_MAC_RXQ_CTRL0_RXQ0EN_MASK           3
#define EQOS_MAC_RXQ_CTRL0_RXQ0EN_NOT_ENABLED    0
#define EQOS_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_DCB    2
#define EQOS_MAC_RXQ_CTRL0_RXQ0EN_ENABLED_AV     1

#define EQOS_MAC_RXQ_CTRL2_PSRQ0_SHIFT           0
#define EQOS_MAC_RXQ_CTRL2_PSRQ0_MASK            0xff

#define EQOS_MAC_HW_FEATURE0_MMCSEL_SHIFT        8
#define EQOS_MAC_HW_FEATURE0_HDSEL_SHIFT         2
#define EQOS_MAC_HW_FEATURE0_GMIISEL_SHIFT       1
#define EQOS_MAC_HW_FEATURE0_MIISEL_SHIFT        0

#define EQOS_MAC_HW_FEATURE1_TXFIFOSIZE_SHIFT    6
#define EQOS_MAC_HW_FEATURE1_TXFIFOSIZE_MASK     0x1f
#define EQOS_MAC_HW_FEATURE1_RXFIFOSIZE_SHIFT    0
#define EQOS_MAC_HW_FEATURE1_RXFIFOSIZE_MASK     0x1f

#define EQOS_MAC_HW_FEATURE3_ASP_SHIFT           28
#define EQOS_MAC_HW_FEATURE3_ASP_MASK            0x3

#define EQOS_MAC_MDIO_ADDRESS_PA_SHIFT           21
#define EQOS_MAC_MDIO_ADDRESS_RDA_SHIFT          16
#define EQOS_MAC_MDIO_ADDRESS_CR_SHIFT           8
#define EQOS_MAC_MDIO_ADDRESS_CR_20_35           2
#define EQOS_MAC_MDIO_ADDRESS_CR_250_300         5
#define EQOS_MAC_MDIO_ADDRESS_SKAP               BIT(4)
#define EQOS_MAC_MDIO_ADDRESS_GOC_SHIFT          2
#define EQOS_MAC_MDIO_ADDRESS_GOC_READ           3
#define EQOS_MAC_MDIO_ADDRESS_GOC_WRITE          1
#define EQOS_MAC_MDIO_ADDRESS_C45E               BIT(1)
#define EQOS_MAC_MDIO_ADDRESS_GB                 BIT(0)

#define EQOS_MAC_MDIO_DATA_GD_MASK               0xffff

#define DWC_ETHER_QOS_MTL_TXQ0OPMODE             (0x00000d00 / 4)
#define DWC_ETHER_QOS_MTL_TXQ0DEBUG              (0x00000d08 / 4)
#define DWC_ETHER_QOS_MTL_TXQ0QUANTUMWEIGHT      (0x00000d18 / 4)
#define DWC_ETHER_QOS_MTL_RXQ0OPMODE             (0x00000d30 / 4)
#define DWC_ETHER_QOS_MTL_RXQ0DEBUG              (0x00000d38 / 4)

#define EQOS_MTL_TXQ0_OPMODE_TQS_SHIFT           16
#define EQOS_MTL_TXQ0_OPMODE_TQS_MASK            0x1ff
#define EQOS_MTL_TXQ0_OPMODE_TXQEN_SHIFT         2
#define EQOS_MTL_TXQ0_OPMODE_TXQEN_MASK          3
#define EQOS_MTL_TXQ0_OPMODE_TXQEN_ENABLED       2
#define EQOS_MTL_TXQ0_OPMODE_TSF                 BIT(1)
#define EQOS_MTL_TXQ0_OPMODE_FTQ                 BIT(0)

#define EQOS_MTL_TXQ0_DEBUG_TXQSTS                    BIT(4)
#define EQOS_MTL_TXQ0_DEBUG_TRCSTS_SHIFT              1
#define EQOS_MTL_TXQ0_DEBUG_TRCSTS_MASK               3

#define EQOS_MTL_RXQ0_OPMODE_RQS_SHIFT        20
#define EQOS_MTL_RXQ0_OPMODE_RQS_MASK         0x3ff
#define EQOS_MTL_RXQ0_OPMODE_RFD_SHIFT        14
#define EQOS_MTL_RXQ0_OPMODE_RFD_MASK         0x3f
#define EQOS_MTL_RXQ0_OPMODE_RFA_SHIFT        8
#define EQOS_MTL_RXQ0_OPMODE_RFA_MASK         0x3f
#define EQOS_MTL_RXQ0_OPMODE_EHFC             BIT(7)
#define EQOS_MTL_RXQ0_OPMODE_RSF              BIT(5)

#define EQOS_MTL_RXQ0_DEBUG_PRXQ_SHIFT                16
#define EQOS_MTL_RXQ0_DEBUG_PRXQ_MASK                 0x7fff
#define EQOS_MTL_RXQ0_DEBUG_RXQSTS_SHIFT              4
#define EQOS_MTL_RXQ0_DEBUG_RXQSTS_MASK               3

#define DWC_ETHER_QOS_DMA_MODE                   (0x00001000 / 4)
#define DWC_ETHER_QOS_DMA_SYSBUSMODE             (0x00001004 / 4)
#define DWC_ETHER_QOS_DMA_CH0CTRL                (0x00001100 / 4)
#define DWC_ETHER_QOS_DMA_CH0TXCTRL              (0x00001104 / 4)
#define DWC_ETHER_QOS_DMA_CH0RXCTRL              (0x00001108 / 4)
#define DWC_ETHER_QOS_DMA_CH0TXDESCLISTHADDR     (0x00001110 / 4)
#define DWC_ETHER_QOS_DMA_CH0TXDESCLISTADDR      (0x00001114 / 4)
#define DWC_ETHER_QOS_DMA_CH0RXDESCLISTHADDR     (0x00001118 / 4)
#define DWC_ETHER_QOS_DMA_CH0RXDESCLISTADDR      (0x0000111c / 4)
#define DWC_ETHER_QOS_DMA_CH0TXDESCTAILPOINTER   (0x00001120 / 4)
#define DWC_ETHER_QOS_DMA_CH0RXDESCTAILPOINTER   (0x00001128 / 4)
#define DWC_ETHER_QOS_DMA_CH0TXDESCRINGLENGTH    (0x0000112c / 4)
#define DWC_ETHER_QOS_DMA_CH0RXDESCRINGLENGTH    (0x00001130 / 4)
#define DWC_ETHER_QOS_DMA_CH0_STATUS             (0x00001160 / 4)

#define EQOS_DMA_MODE_SWR                         BIT(0)

#define EQOS_DMA_SYSBUS_MODE_RD_OSR_LMT_SHIFT     16
#define EQOS_DMA_SYSBUS_MODE_RD_OSR_LMT_MASK      0xf
#define EQOS_DMA_SYSBUS_MODE_EAME                 BIT(11)
#define EQOS_DMA_SYSBUS_MODE_BLEN16               BIT(3)
#define EQOS_DMA_SYSBUS_MODE_BLEN8                BIT(2)
#define EQOS_DMA_SYSBUS_MODE_BLEN4                BIT(1)

#define EQOS_DMA_CH0_CTRL_DSL_SHIFT            18
#define EQOS_DMA_CH0_CTRL_DSL_MASK             0x7
#define EQOS_DMA_CH0_CTRL_PBLX8                BIT(16)

#define EQOS_DMA_CH0_TX_CONTROL_TXPBL_SHIFT       16
#define EQOS_DMA_CH0_TX_CONTROL_TXPBL_MASK        0x3f
#define EQOS_DMA_CH0_TX_CONTROL_OSP               BIT(4)
#define EQOS_DMA_CH0_TX_CONTROL_ST                BIT(0)

#define EQOS_DMA_CH0_RX_CONTROL_RXPBL_SHIFT       16
#define EQOS_DMA_CH0_RX_CONTROL_RXPBL_MASK        0x3f
#define EQOS_DMA_CH0_RX_CONTROL_RBSZ_SHIFT        1
#define EQOS_DMA_CH0_RX_CONTROL_RBSZ_MASK         0x3fff
#define EQOS_DMA_CH0_RX_CONTROL_SR                BIT(0)

#define EQOS_DMA_CH0_STATUS_RBU                BIT(7)
#define EQOS_DMA_CH0_STATUS_TBU                BIT(2)

/* GEM register */
// #define DWC_ETHER_QOS_NWSTATUS      (0x00000008 / 4) /* Network Status reg */
// #define DWC_ETHER_QOS_USERIO        (0x0000000C / 4) /* User IO reg */
// #define DWC_ETHER_QOS_DMACFG        (0x00000010 / 4) /* DMA Control reg */
// #define DWC_ETHER_QOS_TXSTATUS      (0x00000014 / 4) /* TX Status reg */
// #define DWC_ETHER_QOS_RXQBASE       (0x00000018 / 4) /* RX Q Base address reg */
// #define DWC_ETHER_QOS_TXQBASE       (0x0000001C / 4) /* TX Q Base address reg */
// #define DWC_ETHER_QOS_RXSTATUS      (0x00000020 / 4) /* RX Status reg */
// #define DWC_ETHER_QOS_ISR           (0x00000024 / 4) /* Interrupt Status reg */
// #define DWC_ETHER_QOS_IER           (0x00000028 / 4) /* Interrupt Enable reg */
// #define DWC_ETHER_QOS_IDR           (0x0000002C / 4) /* Interrupt Disable reg */
// #define DWC_ETHER_QOS_IMR           (0x00000030 / 4) /* Interrupt Mask reg */
// #define DWC_ETHER_QOS_PHYMNTNC      (0x00000034 / 4) /* Phy Maintenance reg */
// #define DWC_ETHER_QOS_RXPAUSE       (0x00000038 / 4) /* RX Pause Time reg */
// #define DWC_ETHER_QOS_TXPAUSE       (0x0000003C / 4) /* TX Pause Time reg */
// #define DWC_ETHER_QOS_TXPARTIALSF   (0x00000040 / 4) /* TX Partial Store and Forward */
// #define DWC_ETHER_QOS_RXPARTIALSF   (0x00000044 / 4) /* RX Partial Store and Forward */
// #define DWC_ETHER_QOS_JUMBO_MAX_LEN (0x00000048 / 4) /* Max Jumbo Frame Size */
// #define DWC_ETHER_QOS_HASHLO        (0x00000080 / 4) /* Hash Low address reg */
// #define DWC_ETHER_QOS_HASHHI        (0x00000084 / 4) /* Hash High address reg */
// #define DWC_ETHER_QOS_SPADDR1LO     (0x00000088 / 4) /* Specific addr 1 low reg */
// #define DWC_ETHER_QOS_SPADDR1HI     (0x0000008C / 4) /* Specific addr 1 high reg */
// #define DWC_ETHER_QOS_SPADDR2LO     (0x00000090 / 4) /* Specific addr 2 low reg */
// #define DWC_ETHER_QOS_SPADDR2HI     (0x00000094 / 4) /* Specific addr 2 high reg */
// #define DWC_ETHER_QOS_SPADDR3LO     (0x00000098 / 4) /* Specific addr 3 low reg */
// #define DWC_ETHER_QOS_SPADDR3HI     (0x0000009C / 4) /* Specific addr 3 high reg */
// #define DWC_ETHER_QOS_SPADDR4LO     (0x000000A0 / 4) /* Specific addr 4 low reg */
// #define DWC_ETHER_QOS_SPADDR4HI     (0x000000A4 / 4) /* Specific addr 4 high reg */
// #define DWC_ETHER_QOS_TIDMATCH1     (0x000000A8 / 4) /* Type ID1 Match reg */
// #define DWC_ETHER_QOS_TIDMATCH2     (0x000000AC / 4) /* Type ID2 Match reg */
// #define DWC_ETHER_QOS_TIDMATCH3     (0x000000B0 / 4) /* Type ID3 Match reg */
// #define DWC_ETHER_QOS_TIDMATCH4     (0x000000B4 / 4) /* Type ID4 Match reg */
// #define DWC_ETHER_QOS_WOLAN         (0x000000B8 / 4) /* Wake on LAN reg */
// #define DWC_ETHER_QOS_IPGSTRETCH    (0x000000BC / 4) /* IPG Stretch reg */
// #define DWC_ETHER_QOS_SVLAN         (0x000000C0 / 4) /* Stacked VLAN reg */
// #define DWC_ETHER_QOS_MODID         (0x000000FC / 4) /* Module ID reg */
// #define DWC_ETHER_QOS_OCTTXLO       (0x00000100 / 4) /* Octects transmitted Low reg */
// #define DWC_ETHER_QOS_OCTTXHI       (0x00000104 / 4) /* Octects transmitted High reg */
// #define DWC_ETHER_QOS_TXCNT         (0x00000108 / 4) /* Error-free Frames transmitted */
// #define DWC_ETHER_QOS_TXBCNT        (0x0000010C / 4) /* Error-free Broadcast Frames */
// #define DWC_ETHER_QOS_TXMCNT        (0x00000110 / 4) /* Error-free Multicast Frame */
// #define DWC_ETHER_QOS_TXPAUSECNT    (0x00000114 / 4) /* Pause Frames Transmitted */
// #define DWC_ETHER_QOS_TX64CNT       (0x00000118 / 4) /* Error-free 64 TX */
// #define DWC_ETHER_QOS_TX65CNT       (0x0000011C / 4) /* Error-free 65-127 TX */
// #define DWC_ETHER_QOS_TX128CNT      (0x00000120 / 4) /* Error-free 128-255 TX */
// #define DWC_ETHER_QOS_TX256CNT      (0x00000124 / 4) /* Error-free 256-511 */
// #define DWC_ETHER_QOS_TX512CNT      (0x00000128 / 4) /* Error-free 512-1023 TX */
// #define DWC_ETHER_QOS_TX1024CNT     (0x0000012C / 4) /* Error-free 1024-1518 TX */
// #define DWC_ETHER_QOS_TX1519CNT     (0x00000130 / 4) /* Error-free larger than 1519 TX */
// #define DWC_ETHER_QOS_TXURUNCNT     (0x00000134 / 4) /* TX under run error counter */
// #define DWC_ETHER_QOS_SINGLECOLLCNT (0x00000138 / 4) /* Single Collision Frames */
// #define DWC_ETHER_QOS_MULTCOLLCNT   (0x0000013C / 4) /* Multiple Collision Frames */
// #define DWC_ETHER_QOS_EXCESSCOLLCNT (0x00000140 / 4) /* Excessive Collision Frames */
// #define DWC_ETHER_QOS_LATECOLLCNT   (0x00000144 / 4) /* Late Collision Frames */
// #define DWC_ETHER_QOS_DEFERTXCNT    (0x00000148 / 4) /* Deferred Transmission Frames */
// #define DWC_ETHER_QOS_CSENSECNT     (0x0000014C / 4) /* Carrier Sense Error Counter */
// #define DWC_ETHER_QOS_OCTRXLO       (0x00000150 / 4) /* Octects Received register Low */
// #define DWC_ETHER_QOS_OCTRXHI       (0x00000154 / 4) /* Octects Received register High */
// #define DWC_ETHER_QOS_RXCNT         (0x00000158 / 4) /* Error-free Frames Received */
// #define DWC_ETHER_QOS_RXBROADCNT    (0x0000015C / 4) /* Error-free Broadcast Frames RX */
// #define DWC_ETHER_QOS_RXMULTICNT    (0x00000160 / 4) /* Error-free Multicast Frames RX */
// #define DWC_ETHER_QOS_RXPAUSECNT    (0x00000164 / 4) /* Pause Frames Received Counter */
// #define DWC_ETHER_QOS_RX64CNT       (0x00000168 / 4) /* Error-free 64 byte Frames RX */
// #define DWC_ETHER_QOS_RX65CNT       (0x0000016C / 4) /* Error-free 65-127B Frames RX */
// #define DWC_ETHER_QOS_RX128CNT      (0x00000170 / 4) /* Error-free 128-255B Frames RX */
// #define DWC_ETHER_QOS_RX256CNT      (0x00000174 / 4) /* Error-free 256-512B Frames RX */
// #define DWC_ETHER_QOS_RX512CNT      (0x00000178 / 4) /* Error-free 512-1023B Frames RX */
// #define DWC_ETHER_QOS_RX1024CNT     (0x0000017C / 4) /* Error-free 1024-1518B Frames RX */
// #define DWC_ETHER_QOS_RX1519CNT     (0x00000180 / 4) /* Error-free 1519-max Frames RX */
// #define DWC_ETHER_QOS_RXUNDERCNT    (0x00000184 / 4) /* Undersize Frames Received */
// #define DWC_ETHER_QOS_RXOVERCNT     (0x00000188 / 4) /* Oversize Frames Received */
// #define DWC_ETHER_QOS_RXJABCNT      (0x0000018C / 4) /* Jabbers Received Counter */
// #define DWC_ETHER_QOS_RXFCSCNT      (0x00000190 / 4) /* Frame Check seq. Error Counter */
// #define DWC_ETHER_QOS_RXLENERRCNT   (0x00000194 / 4) /* Length Field Error Counter */
// #define DWC_ETHER_QOS_RXSYMERRCNT   (0x00000198 / 4) /* Symbol Error Counter */
// #define DWC_ETHER_QOS_RXALIGNERRCNT (0x0000019C / 4) /* Alignment Error Counter */
// #define DWC_ETHER_QOS_RXRSCERRCNT   (0x000001A0 / 4) /* Receive Resource Error Counter */
// #define DWC_ETHER_QOS_RXORUNCNT     (0x000001A4 / 4) /* Receive Overrun Counter */
// #define DWC_ETHER_QOS_RXIPCSERRCNT  (0x000001A8 / 4) /* IP header Checksum Err Counter */
// #define DWC_ETHER_QOS_RXTCPCCNT     (0x000001AC / 4) /* TCP Checksum Error Counter */
// #define DWC_ETHER_QOS_RXUDPCCNT     (0x000001B0 / 4) /* UDP Checksum Error Counter */

// #define DWC_ETHER_QOS_1588S         (0x000001D0 / 4) /* 1588 Timer Seconds */
// #define DWC_ETHER_QOS_1588NS        (0x000001D4 / 4) /* 1588 Timer Nanoseconds */
// #define DWC_ETHER_QOS_1588ADJ       (0x000001D8 / 4) /* 1588 Timer Adjust */
// #define DWC_ETHER_QOS_1588INC       (0x000001DC / 4) /* 1588 Timer Increment */
// #define DWC_ETHER_QOS_PTPETXS       (0x000001E0 / 4) /* PTP Event Frame Transmitted (s) */
// #define DWC_ETHER_QOS_PTPETXNS      (0x000001E4 / 4) /*
//                                             * PTP Event Frame Transmitted (ns)
//                                             */
// #define DWC_ETHER_QOS_PTPERXS       (0x000001E8 / 4) /* PTP Event Frame Received (s) */
// #define DWC_ETHER_QOS_PTPERXNS      (0x000001EC / 4) /* PTP Event Frame Received (ns) */
// #define DWC_ETHER_QOS_PTPPTXS       (0x000001E0 / 4) /* PTP Peer Frame Transmitted (s) */
// #define DWC_ETHER_QOS_PTPPTXNS      (0x000001E4 / 4) /* PTP Peer Frame Transmitted (ns) */
// #define DWC_ETHER_QOS_PTPPRXS       (0x000001E8 / 4) /* PTP Peer Frame Received (s) */
// #define DWC_ETHER_QOS_PTPPRXNS      (0x000001EC / 4) /* PTP Peer Frame Received (ns) */

// /* Design Configuration Registers */
// #define DWC_ETHER_QOS_DESCONF       (0x00000280 / 4)
// #define DWC_ETHER_QOS_DESCONF2      (0x00000284 / 4)
// #define DWC_ETHER_QOS_DESCONF3      (0x00000288 / 4)
// #define DWC_ETHER_QOS_DESCONF4      (0x0000028C / 4)
// #define DWC_ETHER_QOS_DESCONF5      (0x00000290 / 4)
// #define DWC_ETHER_QOS_DESCONF6      (0x00000294 / 4)
// #define DWC_ETHER_QOS_DESCONF6_64B_MASK (1U << 23)
// #define DWC_ETHER_QOS_DESCONF7      (0x00000298 / 4)

// #define DWC_ETHER_QOS_INT_Q1_STATUS               (0x00000400 / 4)
// #define DWC_ETHER_QOS_INT_Q1_MASK                 (0x00000640 / 4)

// #define DWC_ETHER_QOS_TRANSMIT_Q1_PTR             (0x00000440 / 4)
// #define DWC_ETHER_QOS_TRANSMIT_Q7_PTR             (DWC_ETHER_QOS_TRANSMIT_Q1_PTR + 6)

// #define DWC_ETHER_QOS_RECEIVE_Q1_PTR              (0x00000480 / 4)
// #define DWC_ETHER_QOS_RECEIVE_Q7_PTR              (DWC_ETHER_QOS_RECEIVE_Q1_PTR + 6)

// #define DWC_ETHER_QOS_TBQPH                       (0x000004C8 / 4)
// #define DWC_ETHER_QOS_RBQPH                       (0x000004D4 / 4)

// #define DWC_ETHER_QOS_INT_Q1_ENABLE               (0x00000600 / 4)
// #define DWC_ETHER_QOS_INT_Q7_ENABLE               (DWC_ETHER_QOS_INT_Q1_ENABLE + 6)

// #define DWC_ETHER_QOS_INT_Q1_DISABLE              (0x00000620 / 4)
// #define DWC_ETHER_QOS_INT_Q7_DISABLE              (DWC_ETHER_QOS_INT_Q1_DISABLE + 6)

// #define DWC_ETHER_QOS_INT_Q1_MASK                 (0x00000640 / 4)
// #define DWC_ETHER_QOS_INT_Q7_MASK                 (DWC_ETHER_QOS_INT_Q1_MASK + 6)

// #define DWC_ETHER_QOS_SCREENING_TYPE1_REGISTER_0  (0x00000500 / 4)

// #define DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_ENABLE  (1 << 29)
// #define DWC_ETHER_QOS_ST1R_DSTC_ENABLE            (1 << 28)
// #define DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_SHIFT   (12)
// #define DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_WIDTH   (27 - DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_SHIFT + 1)
// #define DWC_ETHER_QOS_ST1R_DSTC_MATCH_SHIFT       (4)
// #define DWC_ETHER_QOS_ST1R_DSTC_MATCH_WIDTH       (11 - DWC_ETHER_QOS_ST1R_DSTC_MATCH_SHIFT + 1)
// #define DWC_ETHER_QOS_ST1R_QUEUE_SHIFT            (0)
// #define DWC_ETHER_QOS_ST1R_QUEUE_WIDTH            (3 - DWC_ETHER_QOS_ST1R_QUEUE_SHIFT + 1)

// #define DWC_ETHER_QOS_SCREENING_TYPE2_REGISTER_0  (0x00000540 / 4)

// #define DWC_ETHER_QOS_ST2R_COMPARE_A_ENABLE       (1 << 18)
// #define DWC_ETHER_QOS_ST2R_COMPARE_A_SHIFT        (13)
// #define DWC_ETHER_QOS_ST2R_COMPARE_WIDTH          (17 - DWC_ETHER_QOS_ST2R_COMPARE_A_SHIFT + 1)
// #define DWC_ETHER_QOS_ST2R_ETHERTYPE_ENABLE       (1 << 12)
// #define DWC_ETHER_QOS_ST2R_ETHERTYPE_INDEX_SHIFT  (9)
// #define DWC_ETHER_QOS_ST2R_ETHERTYPE_INDEX_WIDTH  (11 - DWC_ETHER_QOS_ST2R_ETHERTYPE_INDEX_SHIFT + 1)
// #define DWC_ETHER_QOS_ST2R_QUEUE_SHIFT            (0)
// #define DWC_ETHER_QOS_ST2R_QUEUE_WIDTH            (3 - DWC_ETHER_QOS_ST2R_QUEUE_SHIFT + 1)

// #define DWC_ETHER_QOS_SCREENING_TYPE2_ETHERTYPE_REG_0     (0x000006e0 / 4)
// #define DWC_ETHER_QOS_TYPE2_COMPARE_0_WORD_0              (0x00000700 / 4)

// #define DWC_ETHER_QOS_T2CW1_COMPARE_OFFSET_SHIFT  (7)
// #define DWC_ETHER_QOS_T2CW1_COMPARE_OFFSET_WIDTH  (8 - DWC_ETHER_QOS_T2CW1_COMPARE_OFFSET_SHIFT + 1)
// #define DWC_ETHER_QOS_T2CW1_OFFSET_VALUE_SHIFT    (0)
// #define DWC_ETHER_QOS_T2CW1_OFFSET_VALUE_WIDTH    (6 - DWC_ETHER_QOS_T2CW1_OFFSET_VALUE_SHIFT + 1)

// /*****************************************/
// #define DWC_ETHER_QOS_NWCTRL_TXSTART     0x00000200 /* Transmit Enable */
// #define DWC_ETHER_QOS_NWCTRL_TXENA       0x00000008 /* Transmit Enable */
// #define DWC_ETHER_QOS_NWCTRL_RXENA       0x00000004 /* Receive Enable */
// #define DWC_ETHER_QOS_NWCTRL_LOCALLOOP   0x00000002 /* Local Loopback */

// #define DWC_ETHER_QOS_NWCFG_STRIP_FCS    0x00020000 /* Strip FCS field */
// #define DWC_ETHER_QOS_NWCFG_LERR_DISC    0x00010000 /* Discard RX frames with len err */
// #define DWC_ETHER_QOS_NWCFG_BUFF_OFST_M  0x0000C000 /* Receive buffer offset mask */
// #define DWC_ETHER_QOS_NWCFG_BUFF_OFST_S  14         /* Receive buffer offset shift */
// #define DWC_ETHER_QOS_NWCFG_RCV_1538     0x00000100 /* Receive 1538 bytes frame */
// #define DWC_ETHER_QOS_NWCFG_UCAST_HASH   0x00000080 /* accept unicast if hash match */
// #define DWC_ETHER_QOS_NWCFG_MCAST_HASH   0x00000040 /* accept multicast if hash match */
// #define DWC_ETHER_QOS_NWCFG_BCAST_REJ    0x00000020 /* Reject broadcast packets */
// #define DWC_ETHER_QOS_NWCFG_PROMISC      0x00000010 /* Accept all packets */
// #define DWC_ETHER_QOS_NWCFG_JUMBO_FRAME  0x00000008 /* Jumbo Frames enable */

// #define DWC_ETHER_QOS_DMACFG_ADDR_64B    (1U << 30)
// #define DWC_ETHER_QOS_DMACFG_TX_BD_EXT   (1U << 29)
// #define DWC_ETHER_QOS_DMACFG_RX_BD_EXT   (1U << 28)
// #define DWC_ETHER_QOS_DMACFG_RBUFSZ_M    0x00FF0000 /* DMA RX Buffer Size mask */
// #define DWC_ETHER_QOS_DMACFG_RBUFSZ_S    16         /* DMA RX Buffer Size shift */
// #define DWC_ETHER_QOS_DMACFG_RBUFSZ_MUL  64         /* DMA RX Buffer Size multiplier */
// #define DWC_ETHER_QOS_DMACFG_TXCSUM_OFFL 0x00000800 /* Transmit checksum offload */

// #define DWC_ETHER_QOS_TXSTATUS_TXCMPL    0x00000020 /* Transmit Complete */
// #define DWC_ETHER_QOS_TXSTATUS_USED      0x00000001 /* sw owned descriptor encountered */

// #define DWC_ETHER_QOS_RXSTATUS_FRMRCVD   0x00000002 /* Frame received */
// #define DWC_ETHER_QOS_RXSTATUS_NOBUF     0x00000001 /* Buffer unavailable */

// /* DWC_ETHER_QOS_ISR DWC_ETHER_QOS_IER DWC_ETHER_QOS_IDR DWC_ETHER_QOS_IMR */
// #define DWC_ETHER_QOS_INT_TXCMPL        0x00000080 /* Transmit Complete */
// #define DWC_ETHER_QOS_INT_AMBA_ERR      0x00000040
// #define DWC_ETHER_QOS_INT_TXUSED         0x00000008
// #define DWC_ETHER_QOS_INT_RXUSED         0x00000004
// #define DWC_ETHER_QOS_INT_RXCMPL        0x00000002

// #define DWC_ETHER_QOS_PHYMNTNC_OP_R      0x20000000 /* read operation */
// #define DWC_ETHER_QOS_PHYMNTNC_OP_W      0x10000000 /* write operation */
// #define DWC_ETHER_QOS_PHYMNTNC_ADDR      0x0F800000 /* Address bits */
// #define DWC_ETHER_QOS_PHYMNTNC_ADDR_SHFT 23
// #define DWC_ETHER_QOS_PHYMNTNC_REG       0x007C0000 /* register bits */
// #define DWC_ETHER_QOS_PHYMNTNC_REG_SHIFT 18

/* Marvell PHY definitions */
#define BOARD_PHY_ADDRESS    0 /* PHY address we will emulate a device at */

#define PHY_REG_CONTROL      0
#define PHY_REG_STATUS       1
#define PHY_REG_PHYID1       2
#define PHY_REG_PHYID2       3
#define PHY_REG_ANEGADV      4
#define PHY_REG_LINKPABIL    5
#define PHY_REG_ANEGEXP      6
#define PHY_REG_NEXTP        7
#define PHY_REG_LINKPNEXTP   8
#define PHY_REG_100BTCTRL    9
#define PHY_REG_1000BTSTAT   10
#define PHY_REG_EXTSTAT      15
#define PHY_REG_PHYSPCFC_CTL 16
#define PHY_REG_PHYSPCFC_ST  17
#define PHY_REG_INT_EN       18
#define PHY_REG_INT_ST       19
#define PHY_REG_EXT_PHYSPCFC_CTL  20
#define PHY_REG_RXERR        21
#define PHY_REG_EACD         22
#define PHY_REG_LED          24
#define PHY_REG_LED_OVRD     25
#define PHY_REG_EXT_PHYSPCFC_CTL2 26
#define PHY_REG_EXT_PHYSPCFC_ST   27
#define PHY_REG_CABLE_DIAG   28

#define PHY_REG_CONTROL_RST       0x8000
#define PHY_REG_CONTROL_LOOP      0x4000
#define PHY_REG_CONTROL_ANEG      0x1000
#define PHY_REG_CONTROL_ANRESTART 0x0200

#define PHY_REG_STATUS_LINK     0x0004
#define PHY_REG_STATUS_ANEGCMPL 0x0020

#define PHY_REG_INT_ST_ANEGCMPL 0x0800
#define PHY_REG_INT_ST_LINKC    0x0400
#define PHY_REG_INT_ST_ENERGY   0x0010

/***********************************************************************/
#define DWC_ETHER_QOS_RX_REJECT                   (-1)
#define DWC_ETHER_QOS_RX_PROMISCUOUS_ACCEPT       (-2)
#define DWC_ETHER_QOS_RX_BROADCAST_ACCEPT         (-3)
#define DWC_ETHER_QOS_RX_MULTICAST_HASH_ACCEPT    (-4)
#define DWC_ETHER_QOS_RX_UNICAST_HASH_ACCEPT      (-5)

#define DWC_ETHER_QOS_RX_SAR_ACCEPT               0

/***********************************************************************/

/* Transmit Descriptor common field */
#define EQOS_TX_DESC3_OWN       BIT(31)
#define EQOS_TX_DESC3_CTXT      BIT(30)
#define EQOS_TX_DESC3_FD        BIT(29)
#define EQOS_TX_DESC3_LD        BIT(28)

/* Read Format */
#define EQOS_TX_DESC2_IOC       BIT(31)
#define EQOS_TX_DESC2_LENGTH    MAKE_64BIT_MASK(0, 14)

#define EQOS_TX_DESC3_CPC       MAKE_64BIT_MASK(26, 2)
#define EQOS_TX_DESC3_FL        MAKE_64BIT_MASK(0, 15)

/* Write-back Format */
#define EQOS_TX_DESC3_OE        BIT(23)
#define EQOS_TX_DESC3_TTSS      BIT(17)
#define EQOS_TX_DESC3_EUE       BIT(16)
#define EQOS_TX_DESC3_ES        BIT(15)
#define EQOS_TX_DESC3_JT        BIT(14)
#define EQOS_TX_DESC3_FF        BIT(13)
#define EQOS_TX_DESC3_PCE       BIT(12)
#define EQOS_TX_DESC3_LOC       BIT(11)
#define EQOS_TX_DESC3_NC        BIT(10)
#define EQOS_TX_DESC3_LC        BIT(9)
#define EQOS_TX_DESC3_EC        BIT(8)
#define EQOS_TX_DESC3_CC        MAKE_64BIT_MASK(4, 4)
#define EQOS_TX_DESC3_ED        BIT(3)
#define EQOS_TX_DESC3_UF        BIT(2)
#define EQOS_TX_DESC3_DB        BIT(1)
#define EQOS_TX_DESC3_IHE       BIT(0)

/* Receive Descriptor common field */
#define EQOS_RX_DESC3_OWN       BIT(31)

/* Read Format */
#define EQOS_RX_DESC3_IOC       BIT(30)
#define EQOS_RX_DESC3_BUF2V     BIT(25)
#define EQOS_RX_DESC3_BUF1V     BIT(24)

/* Write-back Format */
#define EQOS_RX_DESC3_CTXT      BIT(30)
#define EQOS_RX_DESC3_FD        BIT(29)
#define EQOS_RX_DESC3_LD        BIT(28)
#define EQOS_RX_DESC3_RS2V      BIT(27)
#define EQOS_RX_DESC3_RS1V      BIT(26)
#define EQOS_RX_DESC3_RS0V      BIT(25)
#define EQOS_RX_DESC3_CE        BIT(24)
#define EQOS_RX_DESC3_GP        BIT(23)
#define EQOS_RX_DESC3_RWT       BIT(22)
#define EQOS_RX_DESC3_OE        BIT(21)
#define EQOS_RX_DESC3_RE        BIT(20)
#define EQOS_RX_DESC3_DE        BIT(19)
#define EQOS_RX_DESC3_LT        MAKE_64BIT_MASK(16, 3)
#define EQOS_RX_DESC3_ES        BIT(15)
#define EQOS_RX_DESC3_LENGTH    MAKE_64BIT_MASK(0, 14)

#define DWC_ETHER_QOS_MODID_VALUE 0x00020118

static inline uint64_t desc_get_buffer_addr(uint32_t *desc)
{
	uint64_t ret = desc[0];

	if (desc[1]) {
		ret |= (uint64_t)desc[1] << 32;
	}
	return ret;
}

static inline unsigned tx_desc_get_own(uint32_t *desc)
{
	return (desc[3] & EQOS_TX_DESC3_OWN) ? 1 : 0;
}

static inline void tx_desc_set_own(uint32_t *desc)
{
	desc[3] &= ~EQOS_TX_DESC3_OWN;
}

static inline unsigned tx_desc_get_first(uint32_t *desc)
{
	return (desc[3] & EQOS_TX_DESC3_FD) ? 1 : 0;
}

static inline unsigned tx_desc_get_last(uint32_t *desc)
{
	return (desc[3] & EQOS_TX_DESC3_LD) ? 1 : 0;
}

static inline unsigned tx_desc_get_length(uint32_t *desc)
{
	return desc[2] & EQOS_TX_DESC2_LENGTH;
}

static inline void print_dwc_ether_qos_tx_desc(uint32_t *desc, uint8_t queue)
{
	DB_PRINT("TXDESC (queue %" PRId8 "):\n", queue);
	DB_PRINT("  Buffer 1 Addr: 0x%08x\n", desc[0]);
	DB_PRINT("  Buffer 2 Addr: 0x%08x\n", desc[1]);
	DB_PRINT("  Own:           %d\n", tx_desc_get_own(desc));
	DB_PRINT("  First:         %d\n", tx_desc_get_first(desc));
	DB_PRINT("  Last:          %d\n", tx_desc_get_last(desc));
	DB_PRINT("  length:        %d\n", tx_desc_get_length(desc));
}

static inline int dwc_ether_qos_get_desc_len(DesignwareEtherQoSState *s, int q)
{
	int ret = 4;
	uint8_t dsl = (s->regs[DWC_ETHER_QOS_DMA_CH0CTRL + (0x40 * q)] >>
					EQOS_DMA_CH0_CTRL_DSL_SHIFT) & EQOS_DMA_CH0_CTRL_DSL_MASK;

	ret = ret + dsl * s->axi_bus_width / sizeof(uint32_t);

	assert(ret <= DESC_MAX_NUM_WORDS);
	return ret;
}

static inline unsigned rx_desc_get_ownership(uint32_t *desc)
{
	return desc[3] & EQOS_RX_DESC3_OWN ? 1 : 0;
}

static inline void rx_desc_set_ownership(uint32_t *desc)
{
	desc[3] &= ~EQOS_RX_DESC3_OWN;
}

static inline void rx_desc_set_sof(uint32_t *desc)
{
	desc[3] |= EQOS_RX_DESC3_FD;
}

static inline void rx_desc_clear_control(uint32_t *desc)
{
	// desc[1]  = 0;
}

static inline void rx_desc_set_eof(uint32_t *desc)
{
	desc[3] |= EQOS_RX_DESC3_LD;
}

static inline void rx_desc_set_length(uint32_t *desc, unsigned len)
{
	desc[3] &= ~EQOS_RX_DESC3_LENGTH;
	desc[3] |= len;
}

// static inline void rx_desc_set_broadcast(uint32_t *desc)
// {
// 	desc[1] |= R_DESC_1_RX_BROADCAST;
// }

// static inline void rx_desc_set_unicast_hash(uint32_t *desc)
// {
// 	desc[1] |= R_DESC_1_RX_UNICAST_HASH;
// }

// static inline void rx_desc_set_multicast_hash(uint32_t *desc)
// {
// 	desc[1] |= R_DESC_1_RX_MULTICAST_HASH;
// }

// static inline void rx_desc_set_sar(uint32_t *desc, int sar_idx)
// {
// 	desc[1] = deposit32(desc[1], R_DESC_1_RX_SAR_SHIFT, R_DESC_1_RX_SAR_LENGTH,
// 						sar_idx);
// 	desc[1] |= R_DESC_1_RX_SAR_MATCH;
// }

/* The broadcast MAC address: 0xFFFFFFFFFFFF */
// static const uint8_t broadcast_addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static uint32_t dwc_ether_qos_get_max_buf_len(DesignwareEtherQoSState *s, bool tx)
{
	uint32_t size;

	/* untagged frame maximum size */
	size = 1518;

	return size;
}

#if 0
static void dwc_ether_qos_set_isr(DesignwareEtherQoSState *s, int q, uint32_t flag)
{
	// if (q == 0) {
	// 	s->regs[DWC_ETHER_QOS_ISR] |= flag & ~(s->regs[DWC_ETHER_QOS_IMR]);
	// } else {
	// 	s->regs[DWC_ETHER_QOS_INT_Q1_STATUS + q - 1] |= flag &
	// 									~(s->regs[DWC_ETHER_QOS_INT_Q1_MASK + q - 1]);
	// }
}
#endif

/*
 * dwc_ether_qos_init_register_masks:
 * One time initialization.
 * Set masks to identify which register bits have magical clear properties
 */
static void dwc_ether_qos_init_register_masks(DesignwareEtherQoSState *s)
{
	int length = 0;
	uint64_t mask;

	switch (s->axi_bus_width) {
	case 4:  /* 32-bits */
		length = 2;
		break;
	case 8:  /* 64-bits */
		length = 3;
		break;
	case 16:  /* 128-bits */
		length = 4;
		break;
	}
	mask = ((~0ULL) >> (64 - length));

	/* Mask of register bits which are read only */
	memset(&s->regs_ro[0], 0, sizeof(s->regs_ro));
	s->regs_ro[DWC_ETHER_QOS_MAC_CFG]            = BIT(7);
	s->regs_ro[DWC_ETHER_QOS_MAC_Q0TXFLOWCTRL]   = 0x0000FF0C;
	s->regs_ro[DWC_ETHER_QOS_MAC_RXFLOWCTRL]     = 0xFFFFFEFC;
	s->regs_ro[DWC_ETHER_QOS_MAC_RXQ_CTRL0]      = 0xFFFF0000;
	s->regs_ro[DWC_ETHER_QOS_MAC_USTICCOUNTER]   = 0xFFFFF000;
	s->regs_ro[DWC_ETHER_QOS_MAC_HWFEATURE0]     = 0xFFFFFFFF;
	s->regs_ro[DWC_ETHER_QOS_MAC_HWFEATURE1]     = 0xFFFFFFFF;
	s->regs_ro[DWC_ETHER_QOS_MAC_HWFEATURE2]     = 0xFFFFFFFF;
	s->regs_ro[DWC_ETHER_QOS_MAC_MDIOADDRESS]    = 0xE00080E0;

	s->regs_ro[DWC_ETHER_QOS_MTL_TXQ0OPMODE]     = 0xFE00FF80;
	s->regs_ro[DWC_ETHER_QOS_MTL_TXQ0DEBUG]      = 0xFFFFFFFF;
	s->regs_ro[DWC_ETHER_QOS_MTL_TXQ0QUANTUMWEIGHT] = 0xFFE00000;
	s->regs_ro[DWC_ETHER_QOS_MTL_RXQ0OPMODE]     = 0xC0000004;
	s->regs_ro[DWC_ETHER_QOS_MTL_RXQ0DEBUG]      = 0xFFFFFFFF;

	s->regs_ro[DWC_ETHER_QOS_DMA_MODE]                  = 0xFF0480E0;
	s->regs_ro[DWC_ETHER_QOS_DMA_SYSBUSMODE]            = 0x30F00300;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0CTRL]               = 0xFEE2C000;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0TXCTRL]             = 0x80800FE0;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0RXCTRL]             = 0x70808000 | mask << 1;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0TXDESCLISTHADDR]    = 0xFFFF0000;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0TXDESCLISTADDR]     = mask;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0RXDESCLISTHADDR]    = 0xFFFF0000;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0RXDESCLISTADDR]     = mask;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0TXDESCTAILPOINTER]  = mask;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0RXDESCTAILPOINTER]  = mask;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0TXDESCRINGLENGTH]   = 0xFFFFFC00;
	s->regs_ro[DWC_ETHER_QOS_DMA_CH0RXDESCRINGLENGTH]   = 0xFF00FC00;
}

static bool dwc_ether_qos_txqen_enabled(DesignwareEtherQoSState *s, int q)
{
	uint32_t opmode = s->regs[DWC_ETHER_QOS_MTL_TXQ0OPMODE];
	uint8_t enable = (opmode >> EQOS_MTL_TXQ0_OPMODE_TXQEN_SHIFT) &
						EQOS_MTL_TXQ0_OPMODE_TXQEN_MASK;

	if (enable == EQOS_MTL_TXQ0_OPMODE_TXQEN_ENABLED)
		return true;

	return false;
}

static inline bool dwc_ether_qos_rxsf_enabled(DesignwareEtherQoSState *s, int q)
{
	uint32_t opmode = s->regs[DWC_ETHER_QOS_MTL_RXQ0OPMODE];

	if (opmode & EQOS_MTL_RXQ0_OPMODE_RSF)
		return true;

	return false;
}

/*
 * phy_update_link:
 * Make the emulated PHY link state match the QEMU "interface" state.
 */
static void phy_update_link(DesignwareEtherQoSState *s)
{
	DB_PRINT("down %d\n", qemu_get_queue(s->nic)->link_down);

	/* Autonegotiation status mirrors link status.  */
	if (qemu_get_queue(s->nic)->link_down) {
		s->phy_regs[PHY_REG_STATUS] &= ~(PHY_REG_STATUS_ANEGCMPL | PHY_REG_STATUS_LINK);
		s->phy_regs[PHY_REG_INT_ST] |= PHY_REG_INT_ST_LINKC;
	} else {
		s->phy_regs[PHY_REG_STATUS] |= (PHY_REG_STATUS_ANEGCMPL | PHY_REG_STATUS_LINK);
		s->phy_regs[PHY_REG_INT_ST] |= (PHY_REG_INT_ST_LINKC | PHY_REG_INT_ST_ANEGCMPL |
										PHY_REG_INT_ST_ENERGY);
	}
}

static bool dwc_ether_qos_can_receive(NetClientState *nc)
{
	DesignwareEtherQoSState *s;
	int i;

	s = qemu_get_nic_opaque(nc);

	/* Do nothing if receive is not enabled. */
	if (!(s->regs[DWC_ETHER_QOS_MAC_CFG] & EQOS_MAC_CONFIGURATION_RE)) {
		if (s->can_rx_state != 1) {
			s->can_rx_state = 1;
			DB_PRINT("can't receive - no enable\n");
		}
		return false;
	}

	for (i = 0; i < s->num_priority_queues; i++) {
		if (rx_desc_get_ownership(s->rx_desc[i])) {
			break;
		}
	};

	if (i == s->num_priority_queues) {
		if (s->can_rx_state != 2) {
			s->can_rx_state = 2;
			DB_PRINT("can't receive - all the buffer descriptors are busy\n");
		}
		return false;
	}

	if (s->can_rx_state != 0) {
		s->can_rx_state = 0;
		DB_PRINT("can receive\n");
	}
	return true;
}

/*
 * dwc_ether_qos_update_int_status:
 * Raise or lower interrupt based on current status.
 */
static void dwc_ether_qos_update_int_status(DesignwareEtherQoSState *s)
{
	// int i;

	// qemu_set_irq(s->irq[0], !!s->regs[DWC_ETHER_QOS_ISR]);

	// for (i = 1; i < s->num_priority_queues; ++i) {
	// 	qemu_set_irq(s->irq[i], !!s->regs[DWC_ETHER_QOS_INT_Q1_STATUS + i - 1]);
	// }
}

/*
 * dwc_ether_qos_receive_updatestats:
 * Increment receive statistics.
 */
static void dwc_ether_qos_receive_updatestats(DesignwareEtherQoSState *s, const uint8_t *packet,
                                    unsigned bytes)
{
	// uint64_t octets;

	// /* Total octets (bytes) received */
	// octets = ((uint64_t)(s->regs[DWC_ETHER_QOS_OCTRXLO]) << 32) |
	// 			s->regs[DWC_ETHER_QOS_OCTRXHI];
	// octets += bytes;
	// s->regs[DWC_ETHER_QOS_OCTRXLO] = octets >> 32;
	// s->regs[DWC_ETHER_QOS_OCTRXHI] = octets;

	// /* Error-free Frames received */
	// s->regs[DWC_ETHER_QOS_RXCNT]++;

	// /* Error-free Broadcast Frames counter */
	// if (!memcmp(packet, broadcast_addr, 6)) {
	// 	s->regs[DWC_ETHER_QOS_RXBROADCNT]++;
	// }

	// /* Error-free Multicast Frames counter */
	// if (packet[0] == 0x01) {
	// 	s->regs[DWC_ETHER_QOS_RXMULTICNT]++;
	// }

	// if (bytes <= 64) {
	// 	s->regs[DWC_ETHER_QOS_RX64CNT]++;
	// } else if (bytes <= 127) {
	// 	s->regs[DWC_ETHER_QOS_RX65CNT]++;
	// } else if (bytes <= 255) {
	// 	s->regs[DWC_ETHER_QOS_RX128CNT]++;
	// } else if (bytes <= 511) {
	// 	s->regs[DWC_ETHER_QOS_RX256CNT]++;
	// } else if (bytes <= 1023) {
	// 	s->regs[DWC_ETHER_QOS_RX512CNT]++;
	// } else if (bytes <= 1518) {
	// 	s->regs[DWC_ETHER_QOS_RX1024CNT]++;
	// } else {
	// 	s->regs[DWC_ETHER_QOS_RX1519CNT]++;
	// }
}

#if 0
/*
 * Get the MAC Address bit from the specified position
 */
static unsigned get_bit(const uint8_t *mac, unsigned bit)
{
	unsigned byte;

	byte = mac[bit / 8];
	byte >>= (bit & 0x7);
	byte &= 1;

	return byte;
}

/*
 * Calculate a GEM MAC Address hash index
 */
static unsigned calc_mac_hash(const uint8_t *mac)
{
	int index_bit, mac_bit;
	unsigned hash_index;

	hash_index = 0;
	mac_bit = 5;
	for (index_bit = 5; index_bit >= 0; index_bit--) {
		hash_index |= (get_bit(mac,  mac_bit) ^
						get_bit(mac, mac_bit + 6) ^
						get_bit(mac, mac_bit + 12) ^
						get_bit(mac, mac_bit + 18) ^
						get_bit(mac, mac_bit + 24) ^
						get_bit(mac, mac_bit + 30) ^
						get_bit(mac, mac_bit + 36) ^
						get_bit(mac, mac_bit + 42)) << index_bit;
		mac_bit--;
	}

	return hash_index;
}
#endif

/*
 * dwc_ether_qos_mac_address_filter:
 * Accept or reject this destination address?
 * Returns:
 * DWC_ETHER_QOS_RX_REJECT: reject
 * >= 0: Specific address accept (which matched SAR is returned)
 * others for various other modes of accept:
 * DWC_ETHER_QOS_RM_PROMISCUOUS_ACCEPT, DWC_ETHER_QOS_RX_BROADCAST_ACCEPT,
 * DWC_ETHER_QOS_RX_MULTICAST_HASH_ACCEPT or DWC_ETHER_QOS_RX_UNICAST_HASH_ACCEPT
 */
static int dwc_ether_qos_mac_address_filter(DesignwareEtherQoSState *s, const uint8_t *packet)
{
	// uint8_t *dwc_ether_qos_spaddr;
	// int i;
	//is_mc;

	// /* Promiscuous mode? */
	// if (s->regs[DWC_ETHER_QOS_NWCFG] & DWC_ETHER_QOS_NWCFG_PROMISC) {
	// 	return DWC_ETHER_QOS_RX_PROMISCUOUS_ACCEPT;
	// }

	// if (!memcmp(packet, broadcast_addr, 6)) {
	// 	/* Reject broadcast packets? */
	// 	if (s->regs[DWC_ETHER_QOS_NWCFG] & DWC_ETHER_QOS_NWCFG_BCAST_REJ) {
	// 		return DWC_ETHER_QOS_RX_REJECT;
	// 	}
	// 	return DWC_ETHER_QOS_RX_BROADCAST_ACCEPT;
	// }

	/* Accept packets -w- hash match? */
	// is_mc = is_multicast_ether_addr(packet);
	// if ((is_mc && (s->regs[DWC_ETHER_QOS_NWCFG] & DWC_ETHER_QOS_NWCFG_MCAST_HASH)) ||
	// 	(!is_mc && (s->regs[DWC_ETHER_QOS_NWCFG] & DWC_ETHER_QOS_NWCFG_UCAST_HASH))) {
	// 	uint64_t buckets;
	// 	unsigned hash_index;

	// 	hash_index = calc_mac_hash(packet);
	// 	buckets = ((uint64_t)s->regs[DWC_ETHER_QOS_HASHHI] << 32) | s->regs[DWC_ETHER_QOS_HASHLO];
	// 	if ((buckets >> hash_index) & 1) {
	// 		return is_mc ? DWC_ETHER_QOS_RX_MULTICAST_HASH_ACCEPT
	// 						: DWC_ETHER_QOS_RX_UNICAST_HASH_ACCEPT;
	// 	}
	// }

	/* Check all 4 specific addresses */
	// dwc_ether_qos_spaddr = (uint8_t *)&(s->regs[DWC_ETHER_QOS_SPADDR1LO]);
	// for (i = 3; i >= 0; i--) {
	// 	if (s->sar_active[i] && !memcmp(packet, dwc_ether_qos_spaddr + 8 * i, 6)) {
	// 		return DWC_ETHER_QOS_RX_SAR_ACCEPT + i;
	// 	}
	// }

	/* No address match; reject the packet */
	// return DWC_ETHER_QOS_RX_REJECT;
	return DWC_ETHER_QOS_RX_SAR_ACCEPT;
}

#if 0
/* Figure out which queue the received data should be sent to */
static int get_queue_from_screen(DesignwareEtherQoSState *s, uint8_t *rxbuf_ptr,
                                 unsigned rxbufsize)
{
	uint32_t reg;
	bool matched, mismatched;
	int i, j;

	for (i = 0; i < s->num_type1_screeners; i++) {
		reg = s->regs[DWC_ETHER_QOS_SCREENING_TYPE1_REGISTER_0 + i];
		matched = false;
		mismatched = false;

		/* Screening is based on UDP Port */
		if (reg & DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_ENABLE) {
			uint16_t udp_port = rxbuf_ptr[14 + 22] << 8 | rxbuf_ptr[14 + 23];
			if (udp_port == extract32(reg, DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_SHIFT,
											DWC_ETHER_QOS_ST1R_UDP_PORT_MATCH_WIDTH)) {
				matched = true;
			} else {
				mismatched = true;
			}
		}

		/* Screening is based on DS/TC */
		if (reg & DWC_ETHER_QOS_ST1R_DSTC_ENABLE) {
			uint8_t dscp = rxbuf_ptr[14 + 1];
			if (dscp == extract32(reg, DWC_ETHER_QOS_ST1R_DSTC_MATCH_SHIFT,
										DWC_ETHER_QOS_ST1R_DSTC_MATCH_WIDTH)) {
				matched = true;
			} else {
				mismatched = true;
			}
		}

		if (matched && !mismatched) {
			return extract32(reg, DWC_ETHER_QOS_ST1R_QUEUE_SHIFT, DWC_ETHER_QOS_ST1R_QUEUE_WIDTH);
		}
	}

	for (i = 0; i < s->num_type2_screeners; i++) {
		reg = s->regs[DWC_ETHER_QOS_SCREENING_TYPE2_REGISTER_0 + i];
		matched = false;
		mismatched = false;

		if (reg & DWC_ETHER_QOS_ST2R_ETHERTYPE_ENABLE) {
			uint16_t type = rxbuf_ptr[12] << 8 | rxbuf_ptr[13];
			int et_idx = extract32(reg, DWC_ETHER_QOS_ST2R_ETHERTYPE_INDEX_SHIFT,
										DWC_ETHER_QOS_ST2R_ETHERTYPE_INDEX_WIDTH);

			if (et_idx > s->num_type2_screeners) {
				qemu_log_mask(LOG_GUEST_ERROR, "Out of range ethertype "
								"register index: %d\n", et_idx);
			}
			if (type == s->regs[DWC_ETHER_QOS_SCREENING_TYPE2_ETHERTYPE_REG_0 +
								et_idx]) {
				matched = true;
			} else {
				mismatched = true;
			}
		}

		/* Compare A, B, C */
		for (j = 0; j < 3; j++) {
			uint32_t cr0, cr1, mask;
			uint16_t rx_cmp;
			int offset;
			int cr_idx = extract32(reg, DWC_ETHER_QOS_ST2R_COMPARE_A_SHIFT + j * 6,
										DWC_ETHER_QOS_ST2R_COMPARE_WIDTH);

			if (!(reg & (DWC_ETHER_QOS_ST2R_COMPARE_A_ENABLE << (j * 6)))) {
				continue;
			}
			if (cr_idx > s->num_type2_screeners) {
				qemu_log_mask(LOG_GUEST_ERROR, "Out of range compare "
								"register index: %d\n", cr_idx);
			}

			cr0 = s->regs[DWC_ETHER_QOS_TYPE2_COMPARE_0_WORD_0 + cr_idx * 2];
			cr1 = s->regs[DWC_ETHER_QOS_TYPE2_COMPARE_0_WORD_0 + cr_idx * 2 + 1];
			offset = extract32(cr1, DWC_ETHER_QOS_T2CW1_OFFSET_VALUE_SHIFT,
									DWC_ETHER_QOS_T2CW1_OFFSET_VALUE_WIDTH);

			switch (extract32(cr1, DWC_ETHER_QOS_T2CW1_COMPARE_OFFSET_SHIFT,
									DWC_ETHER_QOS_T2CW1_COMPARE_OFFSET_WIDTH)) {
			case 3: /* Skip UDP header */
				qemu_log_mask(LOG_UNIMP, "TCP compare offsets"
								"unimplemented - assuming UDP\n");
				offset += 8;
				/* Fallthrough */
			case 2: /* skip the IP header */
				offset += 20;
				/* Fallthrough */
			case 1: /* Count from after the ethertype */
				offset += 14;
				break;
			case 0:
				/* Offset from start of frame */
				break;
			}

			rx_cmp = rxbuf_ptr[offset] << 8 | rxbuf_ptr[offset];
			mask = extract32(cr0, 0, 16);

			if ((rx_cmp & mask) == (extract32(cr0, 16, 16) & mask)) {
				matched = true;
			} else {
				mismatched = true;
			}
		}

		if (matched && !mismatched) {
			return extract32(reg, DWC_ETHER_QOS_ST2R_QUEUE_SHIFT, DWC_ETHER_QOS_ST2R_QUEUE_WIDTH);
		}
	}

	/* We made it here, assume it's queue 0 */
	return 0;
}
#endif

static inline hwaddr dwc_ether_qos_get_queue_base_addr(DesignwareEtherQoSState *s, int q, bool tx)
{
	hwaddr base;
	uint32_t high_addr;

	if (tx)
		high_addr = s->regs[DWC_ETHER_QOS_DMA_CH0TXDESCLISTHADDR + (0x10 * q)];
	else
		high_addr = s->regs[DWC_ETHER_QOS_DMA_CH0RXDESCLISTHADDR + (0x10 * q)];;

	if (high_addr)
		base = (hwaddr)high_addr << 32;
	else
		base = 0;

	if (tx)
		base |= s->regs[DWC_ETHER_QOS_DMA_CH0TXDESCLISTADDR + (0x10 * q)];
	else
		base |= s->regs[DWC_ETHER_QOS_DMA_CH0RXDESCLISTADDR + (0x10 * q)];

	return base;
}

static inline hwaddr dwc_ether_qos_get_tx_queue_base_addr(DesignwareEtherQoSState *s, int q)
{
	return dwc_ether_qos_get_queue_base_addr(s, q, true);
}

static inline hwaddr dwc_ether_qos_get_rx_queue_base_addr(DesignwareEtherQoSState *s, int q)
{
	return dwc_ether_qos_get_queue_base_addr(s, q, false);
}

static hwaddr dwc_ether_qos_get_desc_addr(DesignwareEtherQoSState *s, bool tx, int q)
{
	hwaddr desc_addr = 0;

	if (tx)
		desc_addr = s->regs[DWC_ETHER_QOS_DMA_CH0TXDESCLISTHADDR + (0x10 * q)];
	else
		desc_addr = s->regs[DWC_ETHER_QOS_DMA_CH0RXDESCLISTHADDR + (0x10 * q)];

	if (desc_addr)
		desc_addr <<= 32;

	desc_addr |= tx ? s->tx_desc_addr[q] : s->rx_desc_addr[q];
	return desc_addr;
}

static hwaddr dwc_ether_qos_get_tx_desc_addr(DesignwareEtherQoSState *s, int q)
{
	return dwc_ether_qos_get_desc_addr(s, true, q);
}

static hwaddr dwc_ether_qos_get_rx_desc_addr(DesignwareEtherQoSState *s, int q)
{
	return dwc_ether_qos_get_desc_addr(s, false, q);
}

static void dwc_ether_qos_get_rx_desc(DesignwareEtherQoSState *s, int q)
{
	hwaddr desc_addr = dwc_ether_qos_get_rx_desc_addr(s, q);

	DB_PRINT("read descriptor 0x%" HWADDR_PRIx "\n", desc_addr);

	/* read current descriptor */
	address_space_read(&s->dma_as, desc_addr, MEMTXATTRS_UNSPECIFIED,
						s->rx_desc[q],
						sizeof(uint32_t) * dwc_ether_qos_get_desc_len(s, true));

	/* Descriptor owned by software ? */
	if (!rx_desc_get_ownership(s->rx_desc[q])) {
		DB_PRINT("descriptor 0x%" HWADDR_PRIx " owned by SW.\n", desc_addr);
		s->regs[DWC_ETHER_QOS_DMA_CH0_STATUS] |= EQOS_DMA_CH0_STATUS_RBU;
		//dwc_ether_qos_set_isr(s, q, DWC_ETHER_QOS_INT_RXUSED);
		/* Handle interrupt consequences */
		dwc_ether_qos_update_int_status(s);
	}
}

static inline
hwaddr dwc_ether_qos_get_next_rxdesc(DesignwareEtherQoSState *s,
										hwaddr prev_desc, uint8_t q)
{
	uint32_t desc_len = 4 * dwc_ether_qos_get_desc_len(s, q);
	hwaddr base, next, limit;

	base = dwc_ether_qos_get_rx_queue_base_addr(s, q);
	next = prev_desc + desc_len;
	limit = base + (s->regs[DWC_ETHER_QOS_DMA_CH0RXDESCRINGLENGTH +
											(0x10 * q)] + 1) * desc_len;

	if (next >= limit)
		next = base;

	return next;
}

/*
 * dwc_ether_qos_receive:
 * Fit a packet handed to us by QEMU into the receive descriptor ring.
 */
static ssize_t dwc_ether_qos_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
	DesignwareEtherQoSState *s = qemu_get_nic_opaque(nc);
	unsigned   rxbufsize, bytes_to_copy;
	uint8_t   *rxbuf_ptr = NULL;
	bool first_desc = true;
	int maf;
	int q = 0;

	/* Is this destination MAC address "for us" ? */
	maf = dwc_ether_qos_mac_address_filter(s, buf);
	if (maf == DWC_ETHER_QOS_RX_REJECT) {
		return size;  /* no, drop siliently b/c it's not an error */
	}

	/* Discard packets with receive length error enabled ? */
	// if (s->regs[DWC_ETHER_QOS_NWCFG] & DWC_ETHER_QOS_NWCFG_LERR_DISC) {
	// 	unsigned type_len;

	// 	/* Fish the ethertype / length field out of the RX packet */
	// 	type_len = buf[12] << 8 | buf[13];
	// 	/* It is a length field, not an ethertype */
	// 	if (type_len < 0x600) {
	// 		if (size < type_len) {
	// 			/* discard */
	// 			return -1;
	// 		}
	// 	}
	// }

	/* The configure size of each receive buffer. Determines how many
	 * buffers needed to hold this packet.
	 */
	rxbufsize = ((s->regs[DWC_ETHER_QOS_DMA_CH0RXCTRL] >>
					EQOS_DMA_CH0_RX_CONTROL_RBSZ_SHIFT) &
					EQOS_DMA_CH0_RX_CONTROL_RBSZ_MASK);
	bytes_to_copy = size;

	/* Hardware allows a zero value here but warns against it. To avoid QEMU
	 * indefinite loops we enforce a minimum value here
	 */
	if (rxbufsize < 64) {
		rxbufsize = 64;
	}

	/* Pad to minimum length. Assume FCS field is stripped, logic
	 * below will increment it to the real minimum of 64 when
	 * not FCS stripping
	 */
	if (size < 60) {
		size = 60;
	}

	/* Strip of FCS field ? (usually yes) */
	if (s->regs[DWC_ETHER_QOS_MAC_CFG] & EQOS_MAC_CONFIGURATION_CST) {
		rxbuf_ptr = (void *)buf;
	} else {
		unsigned crc_val;

		if (size > MAX_FRAME_SIZE - sizeof(crc_val)) {
			size = MAX_FRAME_SIZE - sizeof(crc_val);
		}
		bytes_to_copy = size;
		/* The application wants the FCS field, which QEMU does not provide.
		 * We must try and calculate one.
		 */

		memcpy(s->rx_packet, buf, size);
		memset(s->rx_packet + size, 0, MAX_FRAME_SIZE - size);
		rxbuf_ptr = s->rx_packet;
		crc_val = cpu_to_le32(crc32(0, s->rx_packet, MAX(size, 60)));
		memcpy(s->rx_packet + size, &crc_val, sizeof(crc_val));

		bytes_to_copy += 4;
		size += 4;
	}

	DB_PRINT("config bufsize: %u packet size: %zd\n", rxbufsize, size);

	/* Find which queue we are targeting */
	// q = get_queue_from_screen(s, rxbuf_ptr, rxbufsize);

	if (size > dwc_ether_qos_get_max_buf_len(s, false)) {
		qemu_log_mask(LOG_GUEST_ERROR, "rx frame too long\n");
		// dwc_ether_qos_set_isr(s, q, DWC_ETHER_QOS_INT_AMBA_ERR);
		return -1;
	}

	while (bytes_to_copy) {
		hwaddr desc_addr;

		/* Do nothing if receive is not enabled. */
		if (!dwc_ether_qos_can_receive(nc)) {
			return -1;
		}

		DB_PRINT("copy %" PRIu32 " bytes to 0x%" PRIx64 "\n",
				MIN(bytes_to_copy, rxbufsize),
				desc_get_buffer_addr(s->rx_desc[q]));

		//qemu_hexdump(stderr, "RX packets ", rxbuf_ptr, bytes_to_copy);
		/* Copy packet data to emulated DMA buffer */
		address_space_write(&s->dma_as, desc_get_buffer_addr(s->rx_desc[q]),
							MEMTXATTRS_UNSPECIFIED, rxbuf_ptr,
							MIN(bytes_to_copy, rxbufsize));
		// rxbuf_ptr += MIN(bytes_to_copy, rxbufsize);
		bytes_to_copy -= MIN(bytes_to_copy, rxbufsize);

		rx_desc_clear_control(s->rx_desc[q]);

		/* Update the descriptor.  */
		if (first_desc) {
			rx_desc_set_sof(s->rx_desc[q]);
			first_desc = false;
		}
		if (bytes_to_copy == 0) {
			rx_desc_set_eof(s->rx_desc[q]);
			rx_desc_set_length(s->rx_desc[q], size);
		}
		rx_desc_set_ownership(s->rx_desc[q]);

		// switch (maf) {
		// case DWC_ETHER_QOS_RX_PROMISCUOUS_ACCEPT:
		// 	break;
		// case DWC_ETHER_QOS_RX_BROADCAST_ACCEPT:
		// 	rx_desc_set_broadcast(s->rx_desc[q]);
		// 	break;
		// case DWC_ETHER_QOS_RX_UNICAST_HASH_ACCEPT:
		// 	rx_desc_set_unicast_hash(s->rx_desc[q]);
		// 	break;
		// case DWC_ETHER_QOS_RX_MULTICAST_HASH_ACCEPT:
		// 	rx_desc_set_multicast_hash(s->rx_desc[q]);
		// 	break;
		// case DWC_ETHER_QOS_RX_REJECT:
		// 	abort();
		// default: /* SAR */
		// 	rx_desc_set_sar(s->rx_desc[q], maf);
		// }

		/* Descriptor write-back.  */
		desc_addr = dwc_ether_qos_get_rx_desc_addr(s, q);
		address_space_write(&s->dma_as, desc_addr, MEMTXATTRS_UNSPECIFIED,
							s->rx_desc[q],
							sizeof(uint32_t) * dwc_ether_qos_get_desc_len(s, true));

		/* Next descriptor */
		s->rx_desc_addr[q] = (uint32_t)dwc_ether_qos_get_next_rxdesc(s, desc_addr, q);
		dwc_ether_qos_get_rx_desc(s, q);
	}

	/* Count it */
	dwc_ether_qos_receive_updatestats(s, buf, size);

	// s->regs[DWC_ETHER_QOS_RXSTATUS] |= DWC_ETHER_QOS_RXSTATUS_FRMRCVD;
	// dwc_ether_qos_set_isr(s, q, DWC_ETHER_QOS_INT_RXCMPL);

	/* Handle interrupt consequences */
	dwc_ether_qos_update_int_status(s);

	return size;
}

#if 0
/*
 * dwc_ether_qos_transmit_updatestats:
 * Increment transmit statistics.
 */
static void dwc_ether_qos_transmit_updatestats(DesignwareEtherQoSState *s, const uint8_t *packet,
                                     unsigned bytes)
{
	uint64_t octets;

	/* Total octets (bytes) transmitted */
	octets = ((uint64_t)(s->regs[DWC_ETHER_QOS_OCTTXLO]) << 32) |
				s->regs[DWC_ETHER_QOS_OCTTXHI];
	octets += bytes;
	s->regs[DWC_ETHER_QOS_OCTTXLO] = octets >> 32;
	s->regs[DWC_ETHER_QOS_OCTTXHI] = octets;

	/* Error-free Frames transmitted */
	s->regs[DWC_ETHER_QOS_TXCNT]++;

	/* Error-free Broadcast Frames counter */
	if (!memcmp(packet, broadcast_addr, 6)) {
		s->regs[DWC_ETHER_QOS_TXBCNT]++;
	}

	/* Error-free Multicast Frames counter */
	if (packet[0] == 0x01) {
		s->regs[DWC_ETHER_QOS_TXMCNT]++;
	}

	if (bytes <= 64) {
		s->regs[DWC_ETHER_QOS_TX64CNT]++;
	} else if (bytes <= 127) {
		s->regs[DWC_ETHER_QOS_TX65CNT]++;
	} else if (bytes <= 255) {
		s->regs[DWC_ETHER_QOS_TX128CNT]++;
	} else if (bytes <= 511) {
		s->regs[DWC_ETHER_QOS_TX256CNT]++;
	} else if (bytes <= 1023) {
		s->regs[DWC_ETHER_QOS_TX512CNT]++;
	} else if (bytes <= 1518) {
		s->regs[DWC_ETHER_QOS_TX1024CNT]++;
	} else {
		s->regs[DWC_ETHER_QOS_TX1519CNT]++;
	}
}
#endif

static inline
hwaddr dwc_ether_qos_get_next_txdesc(DesignwareEtherQoSState *s,
										hwaddr prev_desc, uint8_t q)
{
	uint32_t desc_len = 4 * dwc_ether_qos_get_desc_len(s, q);
	hwaddr base, next, limit;

	base = dwc_ether_qos_get_tx_queue_base_addr(s, q);
	next = prev_desc + desc_len;
	limit = base + (s->regs[DWC_ETHER_QOS_DMA_CH0TXDESCRINGLENGTH + (0x10 * q)] + 1) * desc_len;

	if (next >= limit)
		next = base;

	return next;
}

/*
 * dwc_ether_qos_transmit:
 * Fish packets out of the descriptor ring and feed them to QEMU
 */
static void dwc_ether_qos_transmit(DesignwareEtherQoSState *s)
{
	uint32_t desc[DESC_MAX_NUM_WORDS];
	hwaddr packet_desc_addr;
	uint8_t     *p;
	unsigned    total_bytes;
	int q = 0;

	/* Do nothing if transmit is not enabled. */
	if (!(s->regs[DWC_ETHER_QOS_MAC_CFG] & EQOS_MAC_CONFIGURATION_TE)) {
		return;
	}

	DB_PRINT("\n");

	/* The packet we will hand off to QEMU.
	 * Packets scattered across multiple descriptors are gathered to this
	 * one contiguous buffer first.
	 */
	p = s->tx_packet;
	total_bytes = 0;

	for (q = s->num_priority_queues - 1; q >= 0; q--) {
		/* Do nothing if transmit queue is not enabled. */
		if (!dwc_ether_qos_txqen_enabled(s, q))
			return;

		/* read current descriptor */
		packet_desc_addr = dwc_ether_qos_get_tx_desc_addr(s, q);

		DB_PRINT("read descriptor 0x%" HWADDR_PRIx "\n", packet_desc_addr);
		address_space_read(&s->dma_as, packet_desc_addr,
							MEMTXATTRS_UNSPECIFIED, desc,
							sizeof(uint32_t) * dwc_ether_qos_get_desc_len(s, q));
		/* Handle all descriptors owned by hardware */
		while (tx_desc_get_own(desc)) {
			/* Do nothing if transmission command is stopped. */
			if (!(s->regs[DWC_ETHER_QOS_DMA_CH0TXCTRL] & EQOS_DMA_CH0_TX_CONTROL_ST)) {
				return;
			}
			print_dwc_ether_qos_tx_desc(desc, q);
			//qemu_hexdump(stderr, "tx desc ", desc, dwc_ether_qos_get_desc_len(s, q));

			/* The real hardware would eat this (and possibly crash).
			 * For QEMU let's lend a helping hand.
			 */
			if ((desc_get_buffer_addr(desc) == 0) ||
				(tx_desc_get_length(desc) == 0)) {
				DB_PRINT("Invalid TX descriptor @ 0x%" HWADDR_PRIx "\n",
							packet_desc_addr);
				break;
			}

			if (tx_desc_get_length(desc) > dwc_ether_qos_get_max_buf_len(s, true) -
												(p - s->tx_packet)) {
				qemu_log_mask(LOG_GUEST_ERROR, "TX descriptor @ 0x%" \
							HWADDR_PRIx " too large: size 0x%x space 0x%zx\n",
							packet_desc_addr, tx_desc_get_length(desc),
							dwc_ether_qos_get_max_buf_len(s, true) - (p - s->tx_packet));
				// dwc_ether_qos_set_isr(s, q, DWC_ETHER_QOS_INT_AMBA_ERR);
				break;
			}

			/* Gather this fragment of the packet from "dma memory" to our
			 * contig buffer.
			 */
			address_space_read(&s->dma_as, desc_get_buffer_addr(desc),
								MEMTXATTRS_UNSPECIFIED,
								p, tx_desc_get_length(desc));
			//qemu_hexdump(stderr, "buffer ", p, tx_desc_get_length(desc));
			p += tx_desc_get_length(desc);
			total_bytes += tx_desc_get_length(desc);

			/* Modify the descriptor of this packet to be owned by
			 * the processor.
			 */
			tx_desc_set_own(desc);
			address_space_write(&s->dma_as, packet_desc_addr,
								MEMTXATTRS_UNSPECIFIED, desc,
								sizeof(desc));

			/* Last descriptor for this packet; hand the whole thing off */
			if (tx_desc_get_last(desc)) {
				s->tx_desc_addr[q] = dwc_ether_qos_get_next_txdesc(s,
														packet_desc_addr, q);
				DB_PRINT("TX descriptor next: 0x%08x\n", s->tx_desc_addr[q]);

				// s->regs[DWC_ETHER_QOS_TXSTATUS] |= DWC_ETHER_QOS_TXSTATUS_TXCMPL;
				// dwc_ether_qos_set_isr(s, q, DWC_ETHER_QOS_INT_TXCMPL);

				/* Handle interrupt consequences */
				dwc_ether_qos_update_int_status(s);

				/* Is checksum offload enabled? */
				if (s->regs[DWC_ETHER_QOS_MAC_CFG] & EQOS_MAC_CONFIGURATION_IPC) {
					net_checksum_calculate(s->tx_packet, total_bytes, CSUM_ALL);
				}

				/* Update MAC statistics */
				// dwc_ether_qos_transmit_updatestats(s, s->tx_packet, total_bytes);

				/* Send the packet somewhere */
				if (s->phy_loop || (s->regs[DWC_ETHER_QOS_MAC_CFG] &
									EQOS_MAC_CONFIGURATION_LM)) {
					qemu_receive_packet(qemu_get_queue(s->nic), s->tx_packet,
										total_bytes);
				} else {
					qemu_send_packet(qemu_get_queue(s->nic), s->tx_packet,
										total_bytes);
				}

				/* Prepare for next packet */
				p = s->tx_packet;
				total_bytes = 0;
			}

			/* read next descriptor */
			packet_desc_addr = dwc_ether_qos_get_next_txdesc(s, packet_desc_addr, q);
			DB_PRINT("read next descriptor 0x%" HWADDR_PRIx "\n", packet_desc_addr);
			address_space_read(&s->dma_as, packet_desc_addr,
								MEMTXATTRS_UNSPECIFIED, desc,
								sizeof(uint32_t) * dwc_ether_qos_get_desc_len(s, false));
		}

		// if (tx_desc_get_own(desc)) {
		// 	s->regs[DWC_ETHER_QOS_TXSTATUS] |= DWC_ETHER_QOS_TXSTATUS_USED;
		// 	/* IRQ TXUSED is defined only for queue 0 */
		// 	if (q == 0) {
		// 		dwc_ether_qos_set_isr(s, 0, DWC_ETHER_QOS_INT_TXUSED);
		// 	}
		// 	dwc_ether_qos_update_int_status(s);
		// }
	}
}

static void dwc_ether_qos_phy_reset(DesignwareEtherQoSState *s)
{
	memset(&s->phy_regs[0], 0, sizeof(s->phy_regs));
	s->phy_regs[PHY_REG_CONTROL] = 0x1140;
	s->phy_regs[PHY_REG_STATUS] = 0x7969;
	s->phy_regs[PHY_REG_PHYID1] = 0x0141;
	s->phy_regs[PHY_REG_PHYID2] = 0x0CC2;
	s->phy_regs[PHY_REG_ANEGADV] = 0x01E1;
	s->phy_regs[PHY_REG_LINKPABIL] = 0xCDE1;
	s->phy_regs[PHY_REG_ANEGEXP] = 0x000F;
	s->phy_regs[PHY_REG_NEXTP] = 0x2001;
	s->phy_regs[PHY_REG_LINKPNEXTP] = 0x40E6;
	s->phy_regs[PHY_REG_100BTCTRL] = 0x0300;
	s->phy_regs[PHY_REG_1000BTSTAT] = 0x7C00;
	s->phy_regs[PHY_REG_EXTSTAT] = 0x3000;
	s->phy_regs[PHY_REG_PHYSPCFC_CTL] = 0x0078;
	s->phy_regs[PHY_REG_PHYSPCFC_ST] = 0x7C00;
	s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL] = 0x0C60;
	s->phy_regs[PHY_REG_LED] = 0x4100;
	s->phy_regs[PHY_REG_EXT_PHYSPCFC_CTL2] = 0x000A;
	s->phy_regs[PHY_REG_EXT_PHYSPCFC_ST] = 0x848B;

	phy_update_link(s);
}

static void dwc_ether_qos_reset(DeviceState *d)
{
	DesignwareEtherQoSState *s = DWC_ETHER_QOS(d);
	const uint8_t *a;

	DB_PRINT("\n");

	/* Set post reset register values */
	memset(&s->regs[0], 0, sizeof(s->regs));
	s->regs[DWC_ETHER_QOS_MAC_HWFEATURE1] = 0x00000145;

	/* Set MAC address */
	a = &s->conf.macaddr.a[0];
	s->regs[DWC_ETHER_QOS_MAC_ADDRESS0LOW] = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24);
	s->regs[DWC_ETHER_QOS_MAC_ADDRESS0HIGH] = a[4] | (a[5] << 8);

	dwc_ether_qos_phy_reset(s);
	dwc_ether_qos_update_int_status(s);
}

static uint16_t dwc_ether_qos_phy_read(DesignwareEtherQoSState *s, unsigned reg_num)
{
	DB_PRINT("reg: %d value: 0x%04x\n", reg_num, s->phy_regs[reg_num]);
	return s->phy_regs[reg_num];
}

static void dwc_ether_qos_phy_write(DesignwareEtherQoSState *s, unsigned reg_num, uint16_t val)
{
	DB_PRINT("reg: %d value: 0x%04x\n", reg_num, val);

	switch (reg_num) {
	case PHY_REG_CONTROL:
		if (val & PHY_REG_CONTROL_RST) {
			/* Phy reset */
			dwc_ether_qos_phy_reset(s);
			val &= ~(PHY_REG_CONTROL_RST | PHY_REG_CONTROL_LOOP);
			s->phy_loop = 0;
		}
		if (val & PHY_REG_CONTROL_ANEG) {
			/* Complete autonegotiation immediately */
			val &= ~(PHY_REG_CONTROL_ANEG | PHY_REG_CONTROL_ANRESTART);
			s->phy_regs[PHY_REG_STATUS] |= PHY_REG_STATUS_ANEGCMPL;
		}
		if (val & PHY_REG_CONTROL_LOOP) {
			DB_PRINT("PHY placed in loopback\n");
			s->phy_loop = 1;
		} else {
			s->phy_loop = 0;
		}
		break;
	}
	s->phy_regs[reg_num] = val;
}

/*
 * dwc_ether_qos_read32:
 * Read a DWC Ethernet QoS register.
 */
static uint64_t dwc_ether_qos_read(void *opaque, hwaddr offset, unsigned size)
{
	DesignwareEtherQoSState *s;
	uint32_t retval;
	s = (DesignwareEtherQoSState *)opaque;

	offset >>= 2;
	retval = s->regs[offset];

	DB_PRINT("offset: 0x%04x read: 0x%08x\n", (unsigned)offset*4, retval);

	switch (offset) {
	case DWC_ETHER_QOS_MAC_MDIOADDRESS:
		if (retval & EQOS_MAC_MDIO_ADDRESS_GB) {
			uint32_t phy_addr;

			phy_addr = (retval >> EQOS_MAC_MDIO_ADDRESS_PA_SHIFT) & 0x1F;
			if (phy_addr == s->phy_addr) {
				/* The MAC writes 0 to GB bit after the MDIO frame transfer is complete */
				retval &= ~EQOS_MAC_MDIO_ADDRESS_GB;
				s->regs[offset] = retval;
			}
		}
		break;

	case DWC_ETHER_QOS_MAC_MDIODATA:
		if (!(s->regs[DWC_ETHER_QOS_MAC_MDIOADDRESS] & EQOS_MAC_MDIO_ADDRESS_C45E)) {
			qemu_log_mask(LOG_GUEST_ERROR, "high 16-bits vaild only when C45E is set\n");
			s->regs[offset] &= 0x0000FFFF;
		}

		uint32_t addr = s->regs[DWC_ETHER_QOS_MAC_MDIOADDRESS];
		uint32_t rw = (addr >> EQOS_MAC_MDIO_ADDRESS_GOC_SHIFT) & 0x3;
		uint32_t phy_addr, reg_num;
		phy_addr = (addr >> EQOS_MAC_MDIO_ADDRESS_PA_SHIFT) & 0x1F;
		if (phy_addr == s->phy_addr && rw == EQOS_MAC_MDIO_ADDRESS_GOC_READ) {
			reg_num = (addr >> EQOS_MAC_MDIO_ADDRESS_RDA_SHIFT) & 0x1F;
			retval = dwc_ether_qos_phy_read(s, reg_num);
		}

		break;
	}

	DB_PRINT("0x%08x\n", retval);
	dwc_ether_qos_update_int_status(s);
	return retval;
}

/*
 * dwc_ether_qos_write32:
 * Write a DWC Ethernet QoS register.
 */
static void dwc_ether_qos_write(void *opaque, hwaddr offset, uint64_t val,
        unsigned size)
{
	DesignwareEtherQoSState *s = (DesignwareEtherQoSState *)opaque;
	uint32_t readonly;
	int i;

	DB_PRINT("offset: 0x%04x write: 0x%08x\n", (unsigned)offset, (unsigned)val);
	offset >>= 2;

	/* Squash bits which are read only in write value */
	val &= ~(s->regs_ro[offset]);

	/* Preserve (only) bits which are read only in register */
	readonly = s->regs[offset] & s->regs_ro[offset];

	/* Copy register write to backing store */
	s->regs[offset] = val | readonly;

	/* Handle register write side effects */
	switch (offset) {
	case DWC_ETHER_QOS_MAC_CFG:
		if (val & EQOS_MAC_CONFIGURATION_RE) {
			for (i = 0; i < s->num_priority_queues; ++i) {
				dwc_ether_qos_get_rx_desc(s, i);
			}

			if (dwc_ether_qos_can_receive(qemu_get_queue(s->nic))) {
				qemu_flush_queued_packets(qemu_get_queue(s->nic));
			}
		}
		break;
	case DWC_ETHER_QOS_DMA_CH0TXDESCLISTADDR:
		s->tx_desc_addr[0] = val;
		break;
	case DWC_ETHER_QOS_DMA_CH0RXDESCLISTADDR:
		s->rx_desc_addr[0] = val;
		break;
	case DWC_ETHER_QOS_DMA_CH0TXDESCTAILPOINTER:
		dwc_ether_qos_transmit(s);
		break;
	case DWC_ETHER_QOS_DMA_CH0RXDESCTAILPOINTER:
		if (dwc_ether_qos_can_receive(qemu_get_queue(s->nic))) {
			qemu_flush_queued_packets(qemu_get_queue(s->nic));
		}
		break;
	case DWC_ETHER_QOS_MAC_MDIOADDRESS:
		if (val & EQOS_MAC_MDIO_ADDRESS_C45E) {
			qemu_log_mask(LOG_GUEST_ERROR, "Clause 45 PHY not support\n");
			return;
		}

		if (val & EQOS_MAC_MDIO_ADDRESS_GB) {
			uint32_t phy_addr, reg_num;
			uint16_t data;
			uint8_t rw = (val >> EQOS_MAC_MDIO_ADDRESS_GOC_SHIFT) & 0x3;

			phy_addr = (val >> EQOS_MAC_MDIO_ADDRESS_PA_SHIFT) & 0x1F;
			if (phy_addr == s->phy_addr && rw == EQOS_MAC_MDIO_ADDRESS_GOC_WRITE) {
				reg_num = (val >> EQOS_MAC_MDIO_ADDRESS_RDA_SHIFT) & 0x1F;
				data = s->regs[DWC_ETHER_QOS_MAC_MDIODATA] & 0xFFFF;
				dwc_ether_qos_phy_write(s, reg_num, data);
			}
		}
		break;

	case DWC_ETHER_QOS_MAC_MDIODATA:
		if (!(s->regs[DWC_ETHER_QOS_MAC_MDIOADDRESS] & EQOS_MAC_MDIO_ADDRESS_C45E)) {
			qemu_log_mask(LOG_GUEST_ERROR, "high 16-bits vaild only when C45E is set\n");
			val &= 0x0000FFFF;
		}
		break;
	}

	DB_PRINT("newval: 0x%08x\n", s->regs[offset]);
}

static const MemoryRegionOps dwc_ether_qos_ops = {
	.read = dwc_ether_qos_read,
	.write = dwc_ether_qos_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
};

static void dwc_ether_qos_set_link(NetClientState *nc)
{
	DesignwareEtherQoSState *s = qemu_get_nic_opaque(nc);

	DB_PRINT("\n");
	phy_update_link(s);
	dwc_ether_qos_update_int_status(s);
}

static NetClientInfo net_dwc_ether_qos_info = {
	.type = NET_CLIENT_DRIVER_NIC,
	.size = sizeof(NICState),
	.can_receive = dwc_ether_qos_can_receive,
	.receive = dwc_ether_qos_receive,
	.link_status_changed = dwc_ether_qos_set_link,
};

static void dwc_ether_qos_realize(DeviceState *dev, Error **errp)
{
	DesignwareEtherQoSState *s = DWC_ETHER_QOS(dev);
	int i;

	address_space_init(&s->dma_as,
						s->dma_mr ? s->dma_mr : get_system_memory(), "dma");

	if (s->num_priority_queues == 0 ||
		s->num_priority_queues > MAX_PRIORITY_QUEUES) {
		error_setg(errp, "Invalid num-priority-queues value: %" PRIx8,
					s->num_priority_queues);
		return;
	}

	for (i = 0; i < s->num_priority_queues; ++i) {
		sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq[i]);
	}

	qemu_macaddr_default_if_unset(&s->conf.macaddr);

	s->nic = qemu_new_nic(&net_dwc_ether_qos_info, &s->conf,
							object_get_typename(OBJECT(dev)), dev->id, s);

	if (s->jumbo_max_len > MAX_FRAME_SIZE) {
		error_setg(errp, "jumbo-max-len is greater than %d",
					MAX_FRAME_SIZE);
		return;
	}
}

static void dwc_ether_qos_init(Object *obj)
{
	DesignwareEtherQoSState *s = DWC_ETHER_QOS(obj);
	DeviceState *dev = DEVICE(obj);

	DB_PRINT("\n");

	dwc_ether_qos_init_register_masks(s);
	memory_region_init_io(&s->iomem, OBJECT(s), &dwc_ether_qos_ops, s,
							"enet", sizeof(s->regs));

	sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

	object_property_add_link(obj, "dma", TYPE_MEMORY_REGION,
								(Object **)&s->dma_mr,
								qdev_prop_allow_set_link_before_realize,
								OBJ_PROP_LINK_STRONG);
}

static const VMStateDescription vmstate_dwc_eth_qos = {
	.name = "dwc_ether_qos",
	.version_id = 4,
	.minimum_version_id = 4,
	.fields = (VMStateField[]) {
		VMSTATE_UINT32_ARRAY(regs, DesignwareEtherQoSState, DWC_ETHER_QOS_MAXREG),
		VMSTATE_UINT16_ARRAY(phy_regs, DesignwareEtherQoSState, 32),
		VMSTATE_UINT8(phy_loop, DesignwareEtherQoSState),
		VMSTATE_UINT32_ARRAY(rx_desc_addr, DesignwareEtherQoSState,
								MAX_PRIORITY_QUEUES),
		VMSTATE_UINT32_ARRAY(tx_desc_addr, DesignwareEtherQoSState,
								MAX_PRIORITY_QUEUES),
		VMSTATE_BOOL_ARRAY(sar_active, DesignwareEtherQoSState, 4),
		VMSTATE_END_OF_LIST(),
	}
};

static Property dwc_ether_qos_properties[] = {
	DEFINE_NIC_PROPERTIES(DesignwareEtherQoSState, conf),
	DEFINE_PROP_UINT32("revision", DesignwareEtherQoSState, revision,
						DWC_ETHER_QOS_MODID_VALUE),
	DEFINE_PROP_UINT8("phy-addr", DesignwareEtherQoSState, phy_addr, BOARD_PHY_ADDRESS),
	DEFINE_PROP_UINT8("axi-bus-width", DesignwareEtherQoSState, axi_bus_width, EQOS_AXI_WIDTH_64),
	DEFINE_PROP_UINT8("num-priority-queues", DesignwareEtherQoSState,
						num_priority_queues, 1),
	DEFINE_PROP_UINT16("jumbo-max-len", DesignwareEtherQoSState,
						jumbo_max_len, 10240),
	DEFINE_PROP_END_OF_LIST(),
};

static void dwc_ether_qos_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	dc->realize = dwc_ether_qos_realize;
	device_class_set_props(dc, dwc_ether_qos_properties);
	dc->vmsd = &vmstate_dwc_eth_qos;
	dc->reset = dwc_ether_qos_reset;
}

static const TypeInfo dwc_ether_qos_info = {
	.name  = TYPE_DWC_ETHER_QOS,
	.parent = TYPE_SYS_BUS_DEVICE,
	.instance_size = sizeof(DesignwareEtherQoSState),
	.instance_init = dwc_ether_qos_init,
	.class_init = dwc_ether_qos_class_init,
};

static void dwc_ether_qos_register_types(void)
{
    type_register_static(&dwc_ether_qos_info);
}

type_init(dwc_ether_qos_register_types)
