/*
 * QEMU model of the DesignWare SPI Controller
 *
 * Copyright (C) 2024 Charleye <wangkart@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/designware_spi.h"
#include "qapi/error.h"
#include "trace.h"

#define R_CTRL0        (0x00 / 4)
#define R_CTRL1        (0x04 / 4)
#define R_SSIEN        (0x08 / 4)
#define R_MWC          (0x0C / 4)
#define R_SE           (0x10 / 4)
#define R_BAUD         (0x14 / 4)
#define R_TXFTL        (0x18 / 4)
#define R_RXFTL        (0x1C / 4)
#define R_TXFL         (0x20 / 4)
#define R_RXFL         (0x24 / 4)
#define R_STAT         (0x28 / 4)
#define R_IM           (0x2C / 4)
#define R_IS           (0x30 / 4)
#define R_RIS          (0x34 / 4)
#define R_TXOIC        (0x38 / 4)
#define R_RXOIC        (0x3C / 4)
#define R_RXUIC        (0x40 / 4)
#define R_MSTIC        (0x44 / 4)
#define R_IC           (0x48 / 4)
#define R_ID           (0x58 / 4)
#define R_VID          (0x5c / 4)
#define R_DATA         (0x60 / 4)
#define R_SPI_CTRL     (0xF4 / 4)

/* Bit fields in SR, 7 bits */
#define SR_MASK             MAKE_64BIT_MASK(0, 7)
#define SR_BUSY             BIT(0)
#define SR_TF_NOT_FULL      BIT(1)
#define SR_TF_EMPT          BIT(2)
#define SR_RF_NOT_EMPT      BIT(3)
#define SR_RF_FULL          BIT(4)
#define SR_TX_ERR           BIT(5)
#define SR_DCOL             BIT(6)

/* Bit fields in RISR, 5 bits */
#define RISR_RX_FULL_INT        BIT(4)
#define RISR_RX_OVERFLOW_INT    BIT(3)
#define RISR_RX_UNDERFLOW_INT   BIT(2)
#define RISR_TX_OVERFLOW_INT    BIT(1)
#define RISR_TX_EMPTY_INT       BIT(0)

#define TXFTLR_TFT_MASK      MAKE_64BIT_MASK(0, 8)
#define TXFTLR_TXFTHR_OFFSET 16
#define TXFTLR_TXFTHR_MASK   MAKE_64BIT_MASK(TXFTLR_TXFTHR_OFFSET, 8)

#define RXFTLR_RFT_MASK      MAKE_64BIT_MASK(0, 8)

#define IMR_MASK              MAKE_64BIT_MASK(0, 7)

static uint8_t ssi_op_len;
static uint32_t need_wait_cycle;

static uint8_t get_flash_dummy_cycles(DWSPIState *s)
{
	int64_t needed_bytes = 0;
	DeviceState *flash_dev = NULL;
	int i;

	for (i =0; i < s->flash_dev_num; i++) {
		flash_dev = (DeviceState *)s->flash_dev[i];
		if (s->regs[R_SE] & (1 << i))
			break;
	}

	if (flash_dev)
		needed_bytes = object_property_get_int(OBJECT(flash_dev),
		                    "needed-bytes", &error_fatal);

	return needed_bytes;
}

static void designware_spi_txfifo_reset(DWSPIState *s)
{
	fifo32_reset(&s->tx_fifo);

	s->regs[R_STAT] |= SR_TF_EMPT;
	s->regs[R_STAT] |= SR_TF_NOT_FULL;
	s->regs[R_TXFL] = 0;
}

static void designware_spi_rxfifo_reset(DWSPIState *s)
{
	fifo32_reset(&s->rx_fifo);

	s->regs[R_STAT] &= ~SR_RF_FULL;
	s->regs[R_STAT] &= ~SR_RF_NOT_EMPT;
	s->regs[R_RXFL] = 0;
}

static void designware_spi_update_cs(DWSPIState *s)
{
	int i;

	for (i = 0; i < s->num_cs; i++) {
		if (s->regs[R_SE] & (1 << i)) {
			/* select slave */
			qemu_set_irq(s->cs_lines[i], 0);
			need_wait_cycle = deposit32(need_wait_cycle, i, 1, 1);
			ssi_op_len = 0;
		} else {
			qemu_set_irq(s->cs_lines[i], 1);
			need_wait_cycle = deposit32(need_wait_cycle, i, 1, 0);
		}
	}
	qemu_log_mask(LOG_STRACE, "%s: need_wait_cycle: %d SE: 0x%x\n",
	                    __func__, need_wait_cycle, s->regs[R_SE]);
}

static void designware_spi_update_irq(DWSPIState *s)
{
	int level;

	if (fifo32_num_used(&s->tx_fifo) <= (s->regs[R_TXFTL] & TXFTLR_TFT_MASK)) {
		s->regs[R_RIS] |= RISR_TX_EMPTY_INT;
	} else {
		s->regs[R_RIS] &= ~RISR_TX_EMPTY_INT;
	}

	if (fifo32_is_full(&s->tx_fifo)) {
		// s->regs[R_RIS] |= RISR_TX_OVERFLOW_INT;
	} else {
		s->regs[R_RIS] &= RISR_TX_OVERFLOW_INT;
	}

	if (fifo32_num_used(&s->rx_fifo) >= (s->regs[R_RXFTL] & RXFTLR_RFT_MASK) + 1) {
		s->regs[R_RIS] |= RISR_RX_FULL_INT;
	} else {
		s->regs[R_RIS] &= ~RISR_RX_FULL_INT;
	}

	if (fifo32_is_full(&s->rx_fifo)) {
		// s->regs[R_RIS] |= RISR_RX_OVERFLOW_INT;
	} else {
		s->regs[R_RIS] &= ~RISR_RX_OVERFLOW_INT;
	}

	if (fifo32_is_empty(&s->rx_fifo)) {
		// s->regs[R_RIS] |= RISR_RX_UNDERFLOW_INT;
	} else {
		s->regs[R_RIS] &= ~RISR_RX_UNDERFLOW_INT;
	}

	s->regs[R_IS] = s->regs[R_RIS] & s->regs[R_IM];

	level = s->regs[R_IS] ? 1 : 0;
	qemu_set_irq(s->irq, level);
}

static void designware_spi_reset(DeviceState *d)
{
	DWSPIState *s = DESIGNWARE_SPI(d);

	memset(s->regs, 0, sizeof(s->regs));

	s->regs[R_VID] = 0x3130322a;
	s->regs[R_IM] = IMR_MASK;

	designware_spi_txfifo_reset(s);
	designware_spi_rxfifo_reset(s);

	designware_spi_update_cs(s);
	designware_spi_update_irq(s);
}

static void designware_spi_xfer(DWSPIState *s)
{
	uint32_t tx;
	uint32_t rx;

	while (!fifo32_is_empty(&s->tx_fifo)) {
		if (!(s->regs[R_SSIEN] & BIT(0)))
			break;

		tx = fifo32_pop(&s->tx_fifo);
		s->regs[R_TXFL] = fifo32_num_used(&s->tx_fifo);
		s->regs[R_STAT] |= SR_BUSY;
		rx = ssi_transfer(s->spi, tx);
		s->regs[R_STAT] &= ~SR_BUSY;

		if (!fifo32_is_full(&s->rx_fifo)) {
			s->regs[R_STAT] &= ~SR_RF_FULL;
			fifo32_push(&s->rx_fifo, rx);
			s->regs[R_RXFL] = fifo32_num_used(&s->rx_fifo);
			s->regs[R_STAT] |= SR_RF_NOT_EMPT;
		} else {
			s->regs[R_STAT] |= SR_RF_FULL;
			designware_spi_update_irq(s);
		}
	}

	if (fifo32_is_empty(&s->tx_fifo)) {
		s->regs[R_STAT] |= SR_TF_EMPT;
		designware_spi_update_irq(s);
	}

	if (fifo32_is_full(&s->rx_fifo)) {
		s->regs[R_STAT] |= SR_RF_FULL;
		designware_spi_update_irq(s);
	}
}

static void designware_spi_fill_rxfifo(DWSPIState *s)
{
	uint32_t rx;
	uint32_t wait_cycles = get_flash_dummy_cycles(s);

	qemu_log_mask(LOG_STRACE, "%s: wait_cycles: %u, ssi_op_len: %d\n",
	                       __func__, wait_cycles, ssi_op_len);
	if (wait_cycles >= ssi_op_len - 1)
		wait_cycles -= (ssi_op_len - 1);
	qemu_log_mask(LOG_STRACE, "%s: wait_cycles: %u need_wait_cycle: %d\n",
	                    __func__, wait_cycles, need_wait_cycle);
	while(wait_cycles > 0 &&
	      (need_wait_cycle & s->regs[R_SE])) {
		ssi_transfer(s->spi, 0xff);
		wait_cycles--;
	}
	need_wait_cycle = 0x00;
	ssi_op_len = 0;

	while (s->regs[R_CTRL1]) {
		if (fifo32_is_full(&s->rx_fifo))
			break;

		s->regs[R_STAT] |= SR_BUSY;
		rx = ssi_transfer(s->spi, 0);
		s->regs[R_STAT] &= ~SR_BUSY;

		s->regs[R_STAT] &= ~SR_RF_FULL;
		fifo32_push(&s->rx_fifo, rx);
		s->regs[R_RXFL] = fifo32_num_used(&s->rx_fifo);
		s->regs[R_STAT] |= SR_RF_NOT_EMPT;
		s->regs[R_CTRL1]--;
	}

	if (fifo32_is_full(&s->rx_fifo)) {
		s->regs[R_STAT] |= SR_RF_FULL;
		designware_spi_update_irq(s);
	}
}

static void designware_spi_flush_txfifo(DWSPIState *s)
{
	uint32_t tx;

	while (!fifo32_is_empty(&s->tx_fifo)) {
		if (!(s->regs[R_SSIEN] & BIT(0)))
			break;

		tx = fifo32_pop(&s->tx_fifo);
		s->regs[R_TXFL] = fifo32_num_used(&s->tx_fifo);
		s->regs[R_STAT] |= SR_BUSY;
		ssi_transfer(s->spi, tx);
		s->regs[R_STAT] &= ~SR_BUSY;
		ssi_op_len++;
	}

	if (fifo32_is_empty(&s->tx_fifo)) {
		s->regs[R_STAT] |= SR_TF_EMPT;
		designware_spi_update_irq(s);
	}
}

static bool designware_spi_is_bad_reg(hwaddr addr, bool allow_reserved)
{
	bool bad;

	switch (addr) {
	/* reserved offsets */
	case 0x4C:
	case 0x50:
	case 0x54:
	case 0xF0:
	case 0xF8:
	case 0xFC:
		bad = allow_reserved ? false : true;
		break;
	default:
		bad = false;
	}

	if (addr >= (DESIGNWARE_SPI_REG_NUM << 2)) {
		bad = true;
	}

	return bad;
}

static uint64_t designware_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
	DWSPIState *s = opaque;
	uint32_t r;

	if (designware_spi_is_bad_reg(addr, true)) {
		qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read at address 0x%"
						HWADDR_PRIx "\n", __func__, addr);
		return 0;
	}

	addr >>= 2;
	switch (addr) {
	case R_DATA:
		if (fifo32_is_empty(&s->rx_fifo)) {
			qemu_log_mask(LOG_GUEST_ERROR, "%s: rx fifo empty\n", __func__);
			return -1;
		}

		s->regs[R_STAT] |= SR_RF_NOT_EMPT;
		r = fifo32_pop(&s->rx_fifo);
		s->regs[R_STAT] &= ~SR_RF_FULL;
		s->regs[R_RXFL] = fifo32_num_used(&s->rx_fifo);

		if (fifo32_is_empty(&s->rx_fifo))
			s->regs[R_STAT] &= ~SR_RF_NOT_EMPT;

		break;
	case R_RXFL:
		if (!s->regs[R_RXFL] && s->regs[R_CTRL1])
			designware_spi_fill_rxfifo(s);

		r = s->regs[addr];
		break;
	default:
		r = s->regs[addr];
		break;
	}

	designware_spi_update_irq(s);

	trace_designware_spi_read(addr << 2, size, r);
	return r;
}

static void designware_spi_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
	DWSPIState *s = opaque;
	uint32_t value = val64;

	trace_designware_spi_write(addr, val64, size);

	if (designware_spi_is_bad_reg(addr, false)) {
		qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at addr=0x%"
						HWADDR_PRIx " value=0x%x\n", __func__, addr, value);
		return;
	}

	addr >>= 2;
	switch (addr) {
	case R_CTRL0:
	case R_CTRL1:
		if ((s->regs[R_SSIEN] & BIT(0))) {
			qemu_log_mask(LOG_GUEST_ERROR, "Unable to write to CTRL due to SSI enabled\n");
		} else {
			s->regs[addr] = value;
			if (addr == R_CTRL1)
				s->regs[R_CTRL1] += 1;
		}
		break;

	case R_SSIEN:
		if (!(value & BIT(0))) {
			designware_spi_txfifo_reset(s);
			designware_spi_rxfifo_reset(s);
		}
		s->regs[R_SSIEN] = value;
		break;

	case R_SE:
		if (value >= (1 << s->num_cs)) {
			qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid cs value %x\n",
							__func__, value);
		} else {
			if ((s->regs[R_SSIEN] & BIT(0)) &&
					s->regs[R_STAT] & SR_BUSY) {
				qemu_log_mask(LOG_GUEST_ERROR,
						"can not write to SER when SSI enabled and busy.\n");
			} else {
				s->regs[R_SE] = value;
				designware_spi_update_cs(s);

				if (s->regs[R_CTRL1] > 0 && !!value)
					designware_spi_flush_txfifo(s);

				if (fifo32_num_used(&s->tx_fifo) >=
					(s->regs[R_TXFTL] & TXFTLR_TXFTHR_MASK) >> TXFTLR_TXFTHR_OFFSET
					&& !s->regs[R_CTRL1] && !!value)
						designware_spi_xfer(s);
			}
		}
		break;

	case R_DATA:
		if (fifo32_is_full(&s->tx_fifo)) {
			qemu_log_mask(LOG_GUEST_ERROR, "%s: tx fifo full\n", __func__);
			return;
		}

		s->regs[R_STAT] |= SR_TF_NOT_FULL;
		fifo32_push(&s->tx_fifo, value);
		s->regs[R_TXFL] = fifo32_num_used(&s->tx_fifo);
		s->regs[R_STAT] &= ~SR_TF_EMPT;

		if (fifo32_is_full(&s->tx_fifo))
			s->regs[R_STAT] &= ~SR_TF_NOT_FULL;

		if (fifo32_num_used(&s->tx_fifo) >=
			(s->regs[R_TXFTL] & TXFTLR_TXFTHR_MASK) >> TXFTLR_TXFTHR_OFFSET
			&& !s->regs[R_CTRL1] && !!s->regs[R_SE])
				designware_spi_flush_txfifo(s);

		break;

	case R_STAT:
	case R_TXFL:
	case R_RXFL:
		qemu_log_mask(LOG_GUEST_ERROR,
						"%s: invalid write to read-only reigster 0x%"
						HWADDR_PRIx " with 0x%x\n", __func__, addr << 2, value);
		break;

	case R_TXFTL:
	case R_RXFTL:
		if ((value & TXFTLR_TFT_MASK) >= s->fifo_depth) {
			qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid threshold %d\n",
							__func__, value);
		} else {
			s->regs[addr] = value;
		}
		break;

	default:
		s->regs[addr] = value;
		break;
	}

	designware_spi_update_irq(s);
}

static const MemoryRegionOps designware_spi_ops = {
	.read = designware_spi_read,
	.write = designware_spi_write,
	.endianness = DEVICE_LITTLE_ENDIAN,
	.valid = {
		.min_access_size = 4,
		.max_access_size = 4
	}
};

static void designware_spi_realize(DeviceState *dev, Error **errp)
{
	SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
	DWSPIState *s = DESIGNWARE_SPI(dev);
	int i;

	/* Up to 16 slave-select output pins available */
	assert(s->num_cs <= 16);

	s->spi = ssi_create_bus(dev, "spi");
	sysbus_init_irq(sbd, &s->irq);

	s->cs_lines = g_new0(qemu_irq, s->num_cs);
	for (i = 0; i < s->num_cs; i++) {
		sysbus_init_irq(sbd, &s->cs_lines[i]);
	}

	memory_region_init_io(&s->mmio, OBJECT(s), &designware_spi_ops, s,
							TYPE_DESIGNWARE_SPI, 0x1000);
	sysbus_init_mmio(sbd, &s->mmio);

	fifo32_create(&s->tx_fifo, s->fifo_depth);
	fifo32_create(&s->rx_fifo, s->fifo_depth);
}

static Property designware_spi_properties[] = {
	DEFINE_PROP_UINT32("num-cs", DWSPIState, num_cs, 1),
	DEFINE_PROP_UINT32("fifo-depth", DWSPIState, fifo_depth, 64),
	DEFINE_PROP_ARRAY("flash-dev", DWSPIState, flash_dev_num,
	                            flash_dev, qdev_prop_uint64, uint64_t),
	DEFINE_PROP_END_OF_LIST(),
};

static void designware_spi_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);

	device_class_set_props(dc, designware_spi_properties);
	dc->reset = designware_spi_reset;
	dc->realize = designware_spi_realize;
}

static const TypeInfo designware_spi_info = {
	.name           = TYPE_DESIGNWARE_SPI,
	.parent         = TYPE_SYS_BUS_DEVICE,
	.instance_size  = sizeof(DWSPIState),
	.class_init     = designware_spi_class_init,
};

static void designware_spi_register_types(void)
{
	type_register_static(&designware_spi_info);
}

type_init(designware_spi_register_types)
