/*
 * Toshiba TC58CxGxSxHRAIx SPI NAND emulator. Emulate all SPI flash devices based on
 * the TC58CxGxSxHRAIx command set. Known devices table current as of Jun/2024
 * and taken from linux. See drivers/mtd/nand/spi/toshiba.c.
 *
 * Copyright (C) 2023 Charleye <wangkart@aliyun.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) a later version of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "sysemu/block-backend.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "trace.h"
#include "qom/object.h"
#include "qemu/cutils.h"

#define SPINAND_HAS_QE_BIT  BIT(0)
#define SPINAND_MAX_ID_LEN  4
#define SPINAND_MAX_UID_LEN 512
#define SPINAND_UID_LEN     32
#define SPINAND_UID_COPIES  16
#define SPINAND_MAX_FEAT_TABLES  8

/**
 * struct nand_memory_organization - Memory organization structure
 * @bits_per_cell: number of bits per NAND cell
 * @pagesize: page size
 * @oobsize: OOB area size
 * @pages_per_eraseblock: number of pages per eraseblock
 * @eraseblocks_per_lun: number of eraseblocks per LUN (Logical Unit Number)
 * @planes_per_lun: number of planes per LUN
 * @luns_per_target: number of LUN per target (target is a synonym for die)
 * @ntargets: total number of targets exposed by the NAND device
 */
struct nand_memory_organization {
	unsigned int bits_per_cell;
	unsigned int pagesize;
	unsigned int oobsize;
	unsigned int pages_per_eraseblock;
	unsigned int eraseblocks_per_lun;
	unsigned int planes_per_lun;
	unsigned int luns_per_target;
	unsigned int ntargets;
};

#define NAND_MEMORG(bpc, ps, os, ppe, epl, ppl, lpt, nt)   \
	{                                      \
		.bits_per_cell = (bpc),            \
		.pagesize = (ps),                  \
		.oobsize = (os),                   \
		.pages_per_eraseblock = (ppe),     \
		.eraseblocks_per_lun = (epl),      \
		.planes_per_lun = (ppl),           \
		.luns_per_target = (lpt),          \
		.ntargets = (nt),                  \
	}

/**
 * struct nand_ecc_req - NAND ECC requirements
 * @strength: ECC strength
 * @step_size: ECC step/block size
 */
struct nand_ecc_req {
	unsigned int strength;
	unsigned int step_size;
};

#define NAND_ECCREQ(str, stp) { .strength = (str), .step_size = (stp) }

typedef struct SPINandhPartInfo {
	const char *model;
	uint8_t devid[SPINAND_MAX_ID_LEN];
	uint32_t flags;

	/*
	 * This array stores the Unique ID bytes.
	 */
	uint8_t uid[SPINAND_MAX_UID_LEN];
	uint8_t uid_len;

	struct nand_memory_organization memorg;
	struct nand_ecc_req eccreq;
} NandFlashPartInfo;


#define SPINAND_INFO(__model, __id, __memorg, __eccreq, __flags, ...)  \
	{                                       \
		.model = __model,                   \
		.devid = {                          \
		    0x00,                           \
		    ((__id) >> 16) & 0xff,          \
		    ((__id) >> 8) & 0xff,           \
		    (__id) & 0xff,                  \
		},                                  \
		.memorg = __memorg,                 \
		.eccreq = __eccreq,                 \
		.flags = __flags,                   \
		__VA_ARGS__                         \
	}

static const NandFlashPartInfo known_devices[] = {
	/* 3.3V 1Gb (1st generation) */
	SPINAND_INFO("TC58CVG0S3HRAIG", 0x98C200,
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),
	/* 3.3V 2Gb (1st generation) */
	SPINAND_INFO("TC58CVG1S3HRAIG", 0x98CB00,
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),
	/* 3.3V 4Gb (1st generation) */
	SPINAND_INFO("TC58CVG2S0HRAIG", 0x98CD00,
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),
	/* 1.8V 1Gb (1st generation) */
	SPINAND_INFO("TC58CYG0S3HRAIG", 0x98B200,
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),
	/* 1.8V 2Gb (1st generation) */
	SPINAND_INFO("TC58CYG1S3HRAIG", 0x98BB00,
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),
	/* 1.8V 4Gb (1st generation) */
	SPINAND_INFO("TC58CYG2S0HRAIG", 0x98BD00,
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512), 0),

	/*
	 * 2nd generation serial nand has HOLD_D which is equivalent to
	 * QE_BIT.
	 */
	/* 3.3V 1Gb (2nd generation) */
	SPINAND_INFO("TC58CVG0S3HRAIJ", 0x98E200,
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 3.3V 2Gb (2nd generation) */
	SPINAND_INFO("TC58CVG1S3HRAIJ", 0x98EB00,
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 3.3V 4Gb (2nd generation) */
	SPINAND_INFO("TC58CVG2S0HRAIJ", 0x98ED00,
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 3.3V 8Gb (2nd generation) */
	SPINAND_INFO("TH58CVG3S0HRAIJ", 0x98E400,
		     NAND_MEMORG(1, 4096, 256, 64, 4096, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 1.8V 1Gb (2nd generation) */
	SPINAND_INFO("TC58CYG0S3HRAIJ", 0x98D200,
		     NAND_MEMORG(1, 2048, 128, 64, 1024, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 1.8V 2Gb (2nd generation) */
	SPINAND_INFO("TC58CYG1S3HRAIJ", 0x98DB00,
		     NAND_MEMORG(1, 2048, 128, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 1.8V 4Gb (2nd generation) */
	SPINAND_INFO("TC58CYG2S0HRAIJ", 0x98DD00,
		     NAND_MEMORG(1, 4096, 256, 64, 2048, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
	/* 1.8V 8Gb (2nd generation) */
	SPINAND_INFO("TH58CYG3S0HRAIJ", 0x98D400,
		     NAND_MEMORG(1, 4096, 256, 64, 4096, 1, 1, 1),
		     NAND_ECCREQ(8, 512),
		     SPINAND_HAS_QE_BIT),
};

typedef enum {
	NOP = 0,
	WRDI = 0x4,
	WREN = 0x6,
	GETFEAT = 0x0F,
	SETFEAT = 0x1F,
	JEDEC_READ = 0x9f,

	READ = 0x03,
	READ4 = 0x13,
	FAST_READ = 0x0b,
	DOR = 0x3b,
	QOR = 0x6b,
	DIOR = 0xbb,
	QIOR = 0xeb,

	PP = 0x02,
	QPP = 0x32,
	PE = 0x10,
	PPR = 0x84,
	PPR4 = 0xC4,
	FAST_PPR4 = 0x34,
	QIOPPR = 0x72,

	BP = 0x2A,
	ERASE_BLK = 0xd8,

	RESET = 0xFF,
	FAST_RST = 0xFE,
} NANDFlashCMD;

typedef enum {
	STATE_IDLE,
	STATE_PAGE_PROGRAM,
	STATE_READ,
	STATE_COLLECTING_DATA,
	STATE_COLLECTING_VAR_LEN_DATA,
	STATE_READING_DATA,
} CMDState;

typedef enum {
	MFR_GIGADEVICE,
	MFR_MACRONIX,
	MFR_MICRON,
	MFR_WINBOND,
	MFR_TOSHIBA,
	MFR_PARAGON,
	MFR_XTX,
	MFR_GENERIC,
} Manufacturer;

typedef enum {
	MODE_STD = 0,
	MODE_DIO = 1,
	MODE_QIO = 2
} SPIMode;

#define INTERNAL_DATA_BUFFER_SZ 64
#define INTERNAL_CACHE_SZ 2048

typedef struct SPINANDFlashState SPINANDFlashState;
struct SPINANDFlashState {
	SSIPeripheral parent_obj;

	BlockBackend *blk;

	uint8_t *storage;
	uint8_t *cache;
	int size, oobsize, pages;
	int block_size;
	int page_size, oob_size;
	// int page_shift, oob_shift, erase_shift, addr_shift;
	int block_shift, lun_shift;

	uint8_t state;
	uint8_t data[INTERNAL_DATA_BUFFER_SZ];
	uint32_t len;
	uint32_t pos;
	bool data_read_loop;
	uint8_t needed_bytes;
	uint8_t cmd_in_progress;
	uint32_t cur_addr;
	uint8_t *oobbuf;

	bool wp_level;
	bool write_enable;
	bool four_bytes_address_mode;
	bool reset_enable;
	bool quad_enable;
	bool block_protect0;
	bool block_protect1;
	bool block_protect2;
	bool block_protect3;
	bool top_bottom_bit;
	bool status_register_write_disabled;
	uint8_t feat[SPINAND_MAX_FEAT_TABLES];

	int64_t dirty_page;
	int64_t dirty_block;

	const NandFlashPartInfo *pi;
};

struct TC58CXGClass {
	SSIPeripheralClass parent_class;
	NandFlashPartInfo *pi;
};

#define TYPE_TC58CXG "tc58cxg-generic"
OBJECT_DECLARE_TYPE(SPINANDFlashState, TC58CXGClass, TC58CXG)

static inline Manufacturer get_man(SPINANDFlashState *s)
{
	switch (s->pi->devid[1]) {
	case 0x2C:
		return MFR_MICRON;
	case 0xEF:
		return MFR_WINBOND;
	case 0x98:
		return MFR_TOSHIBA;
	case 0xC2:
		return MFR_MACRONIX;
	case 0xA1:
		return MFR_PARAGON;
	case 0x9D:
		return MFR_XTX;
	case 0xC8:
		return MFR_GIGADEVICE;
	default:
		return MFR_GENERIC;
	}
}

static uint8_t get_feat_toshiba(uint8_t *feat, uint8_t addr)
{
	switch (addr) {
	case 0xA0:
	case 0xB0:
	case 0xC0:
		return feat[(addr - 0xA0) >> 4];
	case 0x10:
	case 0x20:
	case 0x30:
	case 0x40:
	case 0x50:
		return feat[3 + ((addr - 0x10) >> 4)];
	default:
		qemu_log_mask(LOG_GUEST_ERROR,
		            "TC58CXG: Invalid get feature address\n");
		return 0xFF;
	}
}

static void set_feat_toshiba(uint8_t *feat, uint8_t addr, uint8_t val)
{
	switch (addr) {
	case 0xA0:
	case 0xB0:
	case 0xC0:
		feat[(addr - 0xA0) >> 4] = val;
		break;
	case 0x10:
	case 0x20:
	case 0x30:
	case 0x40:
	case 0x50:
		feat[3 + ((addr - 0x10) >> 4)] = val;
		break;
	default:
		qemu_log_mask(LOG_GUEST_ERROR,
		            "TC58CXG: Invalid set feature address\n");
		break;
	}
}

static uint8_t get_feat_gigadevice(uint8_t *feat, uint8_t addr)
{
	switch (addr) {
	case 0xA0:
	case 0xB0:
	case 0xC0:
		return feat[(addr - 0xA0) >> 4];
	default:
		qemu_log_mask(LOG_GUEST_ERROR,
		            "TC58CXG: Invalid get feature address\n");
		return 0xFF;
	}
}

static uint8_t get_feat(SPINANDFlashState *s, uint8_t cur_addr)
{
	switch (get_man(s)) {
	case MFR_TOSHIBA:
		return get_feat_toshiba(s->feat, cur_addr);
	case MFR_GIGADEVICE:
		return get_feat_gigadevice(s->feat, cur_addr);
	default:
		return 0;
	}
}

static void set_feat(SPINANDFlashState *s, uint8_t cur_addr, uint8_t val)
{
	switch (get_man(s)) {
	case MFR_TOSHIBA:
		set_feat_toshiba(s->feat, cur_addr, val);
		break;
	default:
		qemu_log_mask(LOG_GUEST_ERROR,
		            "TC58CXG: Invalid manufacturer ID\n");
		break;
	}
}

static void blk_sync_complete(void *opaque, int ret)
{
	QEMUIOVector *iov = opaque;

	qemu_iovec_destroy(iov);
	g_free(iov);

	/* do nothing. Masters do not directly interact with the backing store,
	 * only the working copy so no mutexing required.
	 */
}

static void flash_sync_page(SPINANDFlashState *s, int block, int page)
{
	QEMUIOVector *iov;
	uint64_t offset = block * s->block_size + page * s->page_size;

	if (!s->blk || !blk_is_writable(s->blk) || offset > s->size) {
		return;
	}

	iov = g_new(QEMUIOVector, 1);
	qemu_iovec_init(iov, 1);
	qemu_iovec_add(iov, s->storage + offset,
					s->page_size);
	blk_aio_pwritev(s->blk, offset, iov, 0,
					blk_sync_complete, iov);
}

static inline void flash_sync_area(SPINANDFlashState *s, int64_t off, int64_t len)
{
	QEMUIOVector *iov;

	if (!s->blk || !blk_is_writable(s->blk) || off + len > s->size) {
		qemu_log_mask(LOG_GUEST_ERROR, "Invalid arguments\n");
		return;
	}

	assert(!(len % BDRV_SECTOR_SIZE));
	iov = g_new(QEMUIOVector, 1);
	qemu_iovec_init(iov, 1);
	qemu_iovec_add(iov, s->storage + off, len);
	blk_aio_pwritev(s->blk, off, iov, 0, blk_sync_complete, iov);
}

static void flash_erase(SPINANDFlashState *s, int row)
{
	uint32_t len = s->block_size;
	uint32_t blk_addr_mask = (~0U) >> (32 - s->lun_shift + s->block_shift);
	uint32_t page_addr_mask = (~0U) >> (32 - s->block_shift);
	uint32_t block, page;
	uint64_t offset;

	block = (row >> s->block_shift) & blk_addr_mask;
	page = row & page_addr_mask;
	offset = block * s->block_size + page * s->page_size;
	// oob_off = s->oob_size * (block * s->pi->memorg.pages_per_eraseblock + page);
	// uint8_t capa_to_assert = 0;

	// switch (cmd) {
	// case ERASE_4K:
	// case ERASE4_4K:
	// 	len = 4 * KiB;
	// 	capa_to_assert = ER_4K;
	// 	break;
	// case ERASE_32K:
	// case ERASE4_32K:
	// 	len = 32 * KiB;
	// 	capa_to_assert = ER_32K;
	// 	break;
	// case ERASE_SECTOR:
	// case ERASE4_SECTOR:
	// 	len = s->pi->sector_size;
	// 	break;
	// case BULK_ERASE:
	// 	len = s->size;
	// 	break;
	// case DIE_ERASE:
	// 	if (s->pi->die_cnt) {
	// 		len = s->size / s->pi->die_cnt;
	// 		offset = offset & (~(len - 1));
	// 	} else {
	// 		qemu_log_mask(LOG_GUEST_ERROR, "M25P80: die erase is not supported"
	// 						" by device\n");
	// 		return;
	// 	}
	// 	break;
	// default:
	// 	abort();
	// }

	trace_m25p80_flash_erase(s, offset, len);

	// if ((s->pi->flags & capa_to_assert) != capa_to_assert) {
	// 	qemu_log_mask(LOG_GUEST_ERROR, "TC58CXG: %d erase size not supported by"
	// 	                        " device\n", len);
	// }

	if (!s->write_enable) {
		qemu_log_mask(LOG_GUEST_ERROR, "TC58CXG: erase with write protect!\n");
		return;
	}

	memset(s->storage + offset, 0xff, len);
	flash_sync_area(s, offset, len);
}

static inline void flash_sync_dirty(SPINANDFlashState *s, int64_t newblk, int64_t newpage)
{
	if ((s->dirty_page >= 0 && s->dirty_page != newpage) ||
	    (s->dirty_block >=0 && s->dirty_block != newblk)) {
		flash_sync_page(s, s->dirty_block, s->dirty_page);
		s->dirty_page = newpage;
		s->dirty_block = newblk;
	}
}

static inline int get_addr_length(SPINANDFlashState *s)
{
	switch (s->cmd_in_progress) {
	case SETFEAT:
	case GETFEAT:
		return 1;
	case READ:
	case FAST_READ:
	case DOR:
	case QOR:
	case DIOR:
	case QIOR:
	case PP:
	case QPP:
	case PPR:
	case PPR4:
		return 2;
	default:
		return s->four_bytes_address_mode ? 4 : 3;
	}
	return 0;
}

static void reset_memory(SPINANDFlashState *s)
{
	s->cmd_in_progress = NOP;
	s->cur_addr = 0;
	s->four_bytes_address_mode = false;
	s->len = 0;
	s->needed_bytes = 0;
	s->pos = 0;
	s->state = STATE_IDLE;
	s->write_enable = false;
	s->reset_enable = false;
	s->quad_enable = false;

	switch (get_man(s)) {
	case MFR_TOSHIBA:
		/* 0xA0: All blocks locked */
		s->feat[0] = 0x38;
		/* 0xB0: ECC_E, BBI, HSE */
		s->feat[1] = 0x16;
		break;
	default:
		break;
	}

	trace_m25p80_reset_done(s);
}

static void decode_set_feature_cmd(SPINANDFlashState *s)
{
	s->needed_bytes = get_addr_length(s);

	/* a data byte after address byte */
	s->needed_bytes++;
}

static void decode_read_cmd(SPINANDFlashState *s)
{
	s->needed_bytes = get_addr_length(s);

	/* Dummy cycles - modeled with bytes writes instead of bits */
	switch (s->cmd_in_progress) {
	case READ4:
		break;
	default:
		s->needed_bytes += 8;
		break;
	}

	s->pos = 0;
	s->len = 0;
	s->state = STATE_COLLECTING_DATA;
}

static void update_feature_c0(SPINANDFlashState *s)
{
	switch (get_man(s)) {
	case MFR_TOSHIBA:
		/* set/clear WEL */
		s->feat[2] = deposit32(s->feat[2], 1, 1, s->write_enable);

		/* set/clear OIP bit */
		if (s->cmd_in_progress != NOP && s->cmd_in_progress != GETFEAT)
			s->feat[2] |= 0x01;
		else
			s->feat[2] &= 0xFE;
		break;
	default:
		break;
	}
}

static void decode_new_cmd(SPINANDFlashState *s, uint32_t value)
{
	int i;

	s->cmd_in_progress = value;
	trace_tc58cxg_command_decoded(s, value);
	update_feature_c0(s);

	switch (value) {
	case FAST_RST:
	case RESET:
		reset_memory(s);
		break;
	case GETFEAT:
		s->needed_bytes = get_addr_length(s);
		s->pos = 0;
		s->len = 0;
		s->state = STATE_COLLECTING_DATA;
		break;
	case SETFEAT:
		decode_set_feature_cmd(s);
		s->state = STATE_COLLECTING_DATA;
		break;
	case JEDEC_READ:
		trace_tc58cxg_populated_jedec(s);
		for (i = 0; i < SPINAND_MAX_ID_LEN; i++) {
			s->data[i] = s->pi->devid[i];
		}

		s->len = SPINAND_MAX_ID_LEN;
		s->pos = 0;
		s->state = STATE_READING_DATA;
		break;
	case READ:
	case READ4:
	case FAST_READ:
	case DOR:
	case DIOR:
	case QIOR:
		decode_read_cmd(s);
		break;

	case WRDI:
		s->write_enable = false;
		update_feature_c0(s);
		break;
	case WREN:
		s->write_enable = true;
		update_feature_c0(s);
		break;

	case PP:
	case QPP:
	case PPR:
	case PPR4:
	case FAST_PPR4:
	case QIOPPR:
		s->needed_bytes = get_addr_length(s);
		s->pos = 0;
		s->len = 0;
		s->state = STATE_COLLECTING_DATA;
		break;
	case ERASE_BLK:
	case PE:
		s->needed_bytes = get_addr_length(s);
		s->cur_addr = 0;
		s->state = STATE_COLLECTING_DATA;
		break;
	default:
		qemu_log_mask(LOG_GUEST_ERROR, "TC58CXG: Unknown cmd %x\n", value);
		break;
	}
}

static void load_page(SPINANDFlashState *s, int row)
{
	uint32_t blk_addr_mask = (~0U) >> (32 - s->lun_shift + s->block_shift);
	uint32_t page_addr_mask = (~0U) >> (32 - s->block_shift);
	uint32_t block, page;
	uint64_t offset, oob_off;

	trace_tc58cxg_load_page_raw(row, s->block_shift, s->block_size, s->lun_shift, s->page_size);

	block = (row >> s->block_shift) & blk_addr_mask;
	page = row & page_addr_mask;
	offset = block * s->block_size + page * s->page_size;
	oob_off = s->oob_size * (block * s->pi->memorg.pages_per_eraseblock + page);

	trace_tc58cxg_load_page(s, block, page, offset, oob_off);

	memcpy(s->cache, s->storage + offset, s->page_size);
	memcpy(s->cache + s->page_size, s->oobbuf + oob_off, s->oob_size);
}

static inline
void program_load8(SPINANDFlashState *s, uint8_t data)
{
	uint8_t prev = s->cache[s->cur_addr];

	if (!s->write_enable) {
		qemu_log_mask(LOG_GUEST_ERROR, "TC58CXG: write with write protect!\n");
		return;
	}

	if ((prev ^ data) & data) {
		trace_tc58cxg_programming_zero_to_one(s, s->cur_addr, prev, data);
	}

	s->cache[s->cur_addr] = data;
}

static void flash_write(SPINANDFlashState *s, uint32_t row)
{
	uint32_t blk_addr_mask = (~0U) >> (32 - s->lun_shift + s->block_shift);
	uint32_t page_addr_mask = (~0U) >> (32 - s->block_shift);
	uint32_t block, page;
	uint64_t offset, oob_off;

	if (!s->write_enable) {
		qemu_log_mask(LOG_GUEST_ERROR, "TC58CXG: write with write protect!\n");
		return;
	}

	block = (row >> s->block_shift) & blk_addr_mask;
	page = row & page_addr_mask;
	offset = block * s->block_size + page * s->page_size;
	oob_off = s->oob_size * (block * s->pi->memorg.pages_per_eraseblock + page);

	trace_tc58cxg_flash_write(s, block, page, offset, oob_off);
	memcpy(s->storage + offset, s->cache, s->page_size);
	memcpy(s->oobbuf + oob_off, s->cache + s->page_size, s->oob_size);

	flash_sync_dirty(s, block, page);
	s->dirty_block = block;
	s->dirty_page = page;
}

static void complete_collecting_data(SPINANDFlashState *s)
{
	int i, n;

	n = get_addr_length(s);
	s->cur_addr = 0;
	for (i = 0; i < n; ++i) {
		s->cur_addr <<= 8;
		s->cur_addr |= s->data[i];
	}

	// if (s->cur_addr < s->page_size)
	// 	s->cur_addr &= s->page_size - 1;
	// else if (s->cur_addr < s->page_size + s->oob_size)
	// 	s->cur_addr &= s->page_size + s->oob_size - 1;

	s->state = STATE_IDLE;

	trace_tc58cxg_complete_collecting(s, s->cmd_in_progress, n, s->cur_addr);

	switch (s->cmd_in_progress) {
	case PPR4:
	case PPR:
	case QPP:
	case PP:
		s->state = STATE_PAGE_PROGRAM;
		break;
	case READ:
	case FAST_READ:
	case DOR:
	case QOR:
	case DIOR:
	case QIOR:
		s->state = STATE_READ;
		break;
	case ERASE_BLK:
		flash_erase(s, s->cur_addr);
		break;
	case READ4:
		load_page(s, s->cur_addr);
		break;
	case GETFEAT:
		s->data[0]= get_feat(s, (uint8_t)s->cur_addr);
		s->state = STATE_READING_DATA;
		break;
	case SETFEAT:
		set_feat(s, s->cur_addr, s->data[1]);
		break;
	case PE:
		flash_write(s, s->cur_addr);
		break;
	default:
		break;
	}
}

static int tc58cxg_cs(SSIPeripheral *ss, bool select)
{
	SPINANDFlashState *s = TC58CXG(ss);

	if (select) {
		if (s->state == STATE_COLLECTING_VAR_LEN_DATA) {
			complete_collecting_data(s);
		}
		s->len = 0;
		s->pos = 0;
		s->state = STATE_IDLE;
		s->needed_bytes = 0;
		flash_sync_dirty(s, -1, -1);
		s->data_read_loop = false;
	}

	trace_tc58cxg_select(s, select ? "de" : "");

	return 0;
}

static uint32_t tc58cxg_transfer8(SSIPeripheral *ss, uint32_t tx)
{
	SPINANDFlashState *s = TC58CXG(ss);
	uint32_t r = 0;

	trace_tc58cxg_transfer(s, s->state, s->len, s->needed_bytes, s->pos,
							s->cur_addr, (uint8_t)tx);

	switch (s->state) {

	case STATE_PAGE_PROGRAM:
		trace_tc58cxg_page_program(s, s->cur_addr, (uint8_t)tx);
		program_load8(s, (uint8_t)tx);
		if (s->cur_addr < s->page_size)
			s->cur_addr = (s->cur_addr + 1) & (s->page_size - 1);
		else
			s->cur_addr = (s->cur_addr + 1) & (s->page_size + s->oob_size - 1);
		break;

	case STATE_READ:
		if (s->cur_addr < s->page_size)
			s->cur_addr &= s->page_size - 1;
		else if (s->cur_addr < s->page_size + s->oob_size)
			s->cur_addr &= s->page_size + s->oob_size - 1;
		else
			break;

		r = s->cache[s->cur_addr];
		trace_tc58cxg_read_byte(s, s->cur_addr, (uint8_t)r);

		s->cur_addr++;

		// if (s->cur_addr < s->page_size)
		// 	s->cur_addr = (s->cur_addr + 1) & (s->page_size - 1);
		// else
		// 	s->cur_addr = (s->cur_addr + 1) & (s->page_size + s->oob_size - 1);
		break;

	case STATE_COLLECTING_DATA:
	case STATE_COLLECTING_VAR_LEN_DATA:
		if (s->len >= INTERNAL_DATA_BUFFER_SZ) {
			qemu_log_mask(LOG_GUEST_ERROR,
			                "TC58CXG: Write overrun internal data buffer. "
			                "SPI controller (QEMU emulator or guest driver) "
			                "is misbehaving\n");
			s->len = s->pos = 0;
			s->state = STATE_IDLE;
			break;
		}

		s->data[s->len] = (uint8_t)tx;
		s->len++;

		if (s->len == s->needed_bytes) {
			complete_collecting_data(s);
		}
		break;

	case STATE_READING_DATA:
		if (s->pos >= INTERNAL_DATA_BUFFER_SZ) {
			qemu_log_mask(LOG_GUEST_ERROR,
			                "TC58CXG: Read overrun internal data buffer. "
			                "SPI controller (QEMU emulator or guest driver) "
			                "is misbehaving\n");
			s->len = s->pos = 0;
			s->state = STATE_IDLE;
			break;
		}

		r = s->data[s->pos];
		trace_tc58cxg_read_data(s, s->pos, (uint8_t)r);
		s->pos++;
		if (s->pos == s->len) {
			s->pos = 0;
			if (!s->data_read_loop) {
				s->state = STATE_IDLE;
			}
		}
		break;

	default:
	case STATE_IDLE:
		decode_new_cmd(s, (uint8_t)tx);
		break;
	}

	return r;
}

static void tc58cxg_realize(SSIPeripheral *ss, Error **errp)
{
	SPINANDFlashState *s = TC58CXG(ss);
	TC58CXGClass *mc = TC58CXG_GET_CLASS(s);
	int ret;

	s->pi = mc->pi;

	s->page_size = s->pi->memorg.pagesize;
	s->oob_size = s->pi->memorg.oobsize;
	s->block_size = s->page_size * s->pi->memorg.pages_per_eraseblock;
	s->pages = s->pi->memorg.pages_per_eraseblock * s->pi->memorg.eraseblocks_per_lun;
	s->oobsize = s->oob_size * s->pages;
	s->size = s->block_size * s->pi->memorg.eraseblocks_per_lun;
	s->dirty_page = -1;
	s->dirty_block = -1;
	s->block_shift = ffs(s->pi->memorg.pages_per_eraseblock) - 1;
	s->lun_shift = (ffs(s->pi->memorg.eraseblocks_per_lun) - 1) +
	                        s->block_shift;

	s->cache = g_malloc(s->page_size + s->oob_size);
	s->oobbuf = g_malloc(s->oobsize);
	memset(s->oobbuf, 0xff, s->oobsize);

	g_assert(s->cache != NULL);
	g_assert(s->oobbuf != NULL);

	if (s->blk) {
		uint64_t perm = BLK_PERM_CONSISTENT_READ |
						(blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
		ret = blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
		if (ret < 0) {
			return;
		}

		trace_tc58cxg_binding(s);
		s->storage = blk_blockalign(s->blk, s->size);

		if (blk_pread(s->blk, 0, s->size, s->storage, 0) < 0) {
			error_setg(errp, "failed to read the initial flash content");
			return;
		}
	} else {
		trace_tc58cxg_binding_no_bdrv(s);
		s->storage = blk_blockalign(NULL, s->size);
		memset(s->storage, 0xFF, s->size);
	}

    // qdev_init_gpio_in_named(DEVICE(s),
    //                         tc58cxg_write_protect_pin_irq_handler, "WP#", 1);
}

static void tc58cxg_reset(DeviceState *d)
{
	SPINANDFlashState *s = TC58CXG(d);

	s->wp_level = true;
	s->status_register_write_disabled = false;
	s->block_protect0 = false;
	s->block_protect1 = false;
	s->block_protect2 = false;
	s->block_protect3 = false;
	s->top_bottom_bit = false;

	reset_memory(s);
}

static Property tc58cxg_properties[] = {
	DEFINE_PROP_UINT8("needed-bytes", SPINANDFlashState, needed_bytes, 0),
	DEFINE_PROP_DRIVE("drive", SPINANDFlashState, blk),
	DEFINE_PROP_END_OF_LIST(),
};

static void tc58cxg_class_init(ObjectClass *klass, void *data)
{
	DeviceClass *dc = DEVICE_CLASS(klass);
	SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
	TC58CXGClass *mc = TC58CXG_CLASS(klass);

	k->realize = tc58cxg_realize;
	k->transfer = tc58cxg_transfer8;
	k->set_cs = tc58cxg_cs;
	k->cs_polarity = SSI_CS_LOW;
	// dc->vmsd = &vmstate_tc58cxg;
	device_class_set_props(dc, tc58cxg_properties);
	dc->reset = tc58cxg_reset;
	mc->pi = data;
}

static const TypeInfo tc58cxg_info = {
	.name           = TYPE_TC58CXG,
	.parent         = TYPE_SSI_PERIPHERAL,
	.instance_size  = sizeof(SPINANDFlashState),
	.class_size     = sizeof(TC58CXGClass),
	.abstract       = true,
};

static void tc58cxg_register_types(void)
{
	int i;

	type_register_static(&tc58cxg_info);
	for (i = 0; i < ARRAY_SIZE(known_devices); ++i) {
		TypeInfo ti = {
			.name       = known_devices[i].model,
			.parent     = TYPE_TC58CXG,
			.class_init = tc58cxg_class_init,
			.class_data = (void *)&known_devices[i],
		};
		type_register(&ti);
	}
}

type_init(tc58cxg_register_types)