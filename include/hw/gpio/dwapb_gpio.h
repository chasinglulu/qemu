/*
 * DW APB System-on-Chip general purpose input/output register definition
 *
 * Copyright 2023 xinlu.wang
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef DWAPB_GPIO_H
#define DWAPB_GPIO_H

#include "hw/sysbus.h"
#include "qom/object.h"
#include <stdint.h>

#define TYPE_DWAPB_GPIO "dwapb.gpio"
typedef struct DWAPBGPIOState DWAPBGPIOState;
DECLARE_INSTANCE_CHECKER(DWAPBGPIOState, DWAPB_GPIO,
                         TYPE_DWAPB_GPIO)

#define DWAPB_GPIO_PINS 32

#define DWAPB_GPIO_SIZE 0x100   /* memory-mapped IO size*/

#define DWAPB_GPIO_REG_PORTA_DR     0x000
#define DWAPB_GPIO_REG_PORTA_DDR    0x004
#define DWAPB_GPIO_REG_PORTA_CTL    0x008
#define DWAPB_GPIO_REG_PORTB_DR     0x00C
#define DWAPB_GPIO_REG_PORTB_DDR    0x010
#define DWAPB_GPIO_REG_PORTB_CTL    0x014
#define DWAPB_GPIO_REG_PORTC_DR     0x018
#define DWAPB_GPIO_REG_PORTC_DDR    0x01C
#define DWAPB_GPIO_REG_PORTC_CTL    0x020
#define DWAPB_GPIO_REG_PORTD_DR     0x024
#define DWAPB_GPIO_REG_PORTD_DDR    0x028
#define DWAPB_GPIO_REG_PORTD_CTL    0x02C
#define DWAPB_GPIO_REG_INTEN        0x030
#define DWAPB_GPIO_REG_INTMASK      0x034
#define DWAPB_GPIO_REG_INTTYPE_LEVEL    0x038
#define DWAPB_GPIO_REG_INT_POLARITY     0x03C
#define DWAPB_GPIO_REG_INTSTATUS    0x040
#define DWAPB_GPIO_REG_RAW_INTSTATUS    0x44
#define DWAPB_GPIO_REG_DEBOUNCE     0x48
#define DWAPB_GPIO_REG_PORTA_EOI    0x4c
#define DWAPB_GPIO_REG_EXT_PORTA    0x50
#define DWAPB_GPIO_REG_EXT_PORTB    0x54
#define DWAPB_GPIO_REG_EXT_PORTC    0x58
#define DWAPB_GPIO_REG_EXT_PORTD    0x5c
#define DWAPB_GPIO_REG_LS_SYNC      0x60
#define DWAPB_GPIO_REG_ID_CODE      0x64
#define DWAPB_GPIO_REG_INT_BOTHEDGE 0x68
#define DWAPB_GPIO_REG_VER_ID_CODE  0x6c
#define DWAPB_GPIO_REG_CONFIG2      0x70
#define DWAPB_GPIO_REG_CONFIG1      0x74

struct DWAPBGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    qemu_irq irq;       /* single combined interrupt output signal*/
    qemu_irq output[DWAPB_GPIO_PINS];   /* individual interrupts output signal*/

    uint32_t porta_dr;
    uint32_t porta_ddr;
    uint32_t porta_ctl;
    uint32_t portb_dr;
    uint32_t portb_ddr;
    uint32_t portb_ctl;
    uint32_t portc_dr;
    uint32_t portc_ddr;
    uint32_t portc_ctl;
    uint32_t portd_dr;
    uint32_t portd_ddr;
    uint32_t portd_ctl;
    uint32_t inten;
    uint32_t intmask;
    uint32_t inttype_level;
    uint32_t int_polarity;
    uint32_t intstatus;
    uint32_t raw_intstatus;
    uint32_t debounce;
    uint32_t porta_eoi;
    uint32_t ext_porta;
    uint32_t ext_portb;
    uint32_t ext_portc;
    uint32_t ext_portd;
    uint32_t ls_sync;
    uint32_t id_code;
    uint32_t int_bothedge;
    uint32_t ver_id_code;
    uint32_t config2;
    uint32_t config1;

    /* config */
    uint32_t ngpio;
    bool single_int;
};

#endif /* DWAPB_GPIO_H */
