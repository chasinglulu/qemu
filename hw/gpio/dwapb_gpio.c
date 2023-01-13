/*
 * DW APB System-on-Chip general purpose input/output register definition
 *
 * Copyright 2023 xinlu.wang
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/gpio/dwapb_gpio.h"
#include "migration/vmstate.h"
#include "qemu/typedefs.h"
#include "trace.h"

static void update_state(DWAPBGPIOState *s)
{

    size_t i;
    bool prev_ival, inten, in_mask, int_pol;
    bool trigger_int = false;

    for(i = 0; i < s->ngpio; i++) {
        prev_ival = extract32(s->ext_porta, i, 1);
        inten = extract32(s->inten, i, 1);
        in_mask = extract32(s->intmask, i, 1);
        int_pol = extract32(s->intmask, i, 1);
        int_pol = extract32(s->int_polarity, i, 1);

        if (inten && prev_ival) {
            s->raw_intstatus = deposit32(s->raw_intstatus, i, 1, 1);
        }

        if (prev_ival && inten && !in_mask) {
            qemu_set_irq(s->output[i], int_pol? 1 : 0);
            s->intstatus = deposit32(s->intstatus, i, 1, 1);
            trigger_int = true;
            trace_dwapb_gpio_update_state(i, int_pol? 1 : 0);
        }
    }

    if (trigger_int && s->single_int) {
        qemu_set_irq(s->irq, int_pol? 1 : 0);
        trace_dwapb_gpio_update_state(i, int_pol? 1 : 0);
    }
}

static uint64_t dwapb_gpio_read(void *opaque, hwaddr offset, unsigned int size)
{
    DWAPBGPIOState *s = DWAPB_GPIO(opaque);
    uint64_t r = 0;

    switch (offset) {
    case DWAPB_GPIO_REG_PORTA_DR:
        r = s->porta_dr;
        break;

    case DWAPB_GPIO_REG_PORTA_DDR:
        r = s->porta_ddr;
        break;

    case DWAPB_GPIO_REG_PORTA_CTL:
        r = s->porta_ctl;
        break;

    case DWAPB_GPIO_REG_PORTB_DR:
        r = s->portb_dr;
        break;

    case DWAPB_GPIO_REG_PORTB_DDR:
        r = s->portb_ddr;
        break;

    case DWAPB_GPIO_REG_PORTB_CTL:
        r = s->portb_ctl;
        break;

    case DWAPB_GPIO_REG_PORTC_DR:
        r = s->portc_dr;
        break;

    case DWAPB_GPIO_REG_PORTC_DDR:
        r = s->portc_ddr;
        break;

    case DWAPB_GPIO_REG_PORTC_CTL:
        r = s->portc_ctl;
        break;

    case DWAPB_GPIO_REG_PORTD_DR:
        r = s->portd_dr;
        break;

    case DWAPB_GPIO_REG_INTEN:
        r = s->inten;
        break;

    case DWAPB_GPIO_REG_INTMASK:
        r = s->intmask;
        break;

    case DWAPB_GPIO_REG_INTTYPE_LEVEL:
        r = s->inttype_level;
        break;

    case DWAPB_GPIO_REG_INT_POLARITY:
        r = s->int_polarity;
        break;

    case DWAPB_GPIO_REG_INTSTATUS:
        r = s->intstatus;
        break;

    case DWAPB_GPIO_REG_RAW_INTSTATUS:
        r = s->raw_intstatus;
        break;

    case DWAPB_GPIO_REG_DEBOUNCE:
        r = s->debounce;
        break;

    /* Write-only register*/
    case DWAPB_GPIO_REG_PORTA_EOI:
        r = 0;
        break;

    case DWAPB_GPIO_REG_EXT_PORTA:
        r = s->ext_porta;
        break;

    case DWAPB_GPIO_REG_EXT_PORTB:
        r = s->ext_portb;
        break;

    case DWAPB_GPIO_REG_EXT_PORTC:
        r = s->ext_portc;
        break;

    case DWAPB_GPIO_REG_EXT_PORTD:
        r = s->ext_portd;
        break;

    case DWAPB_GPIO_REG_LS_SYNC:
        r = s->ls_sync;
        break;

    case DWAPB_GPIO_REG_ID_CODE:
        r = s->id_code;
        break;

    case DWAPB_GPIO_REG_INT_BOTHEDGE:
        r = s->int_bothedge;
        break;

    case DWAPB_GPIO_REG_VER_ID_CODE:
        r = s->ver_id_code;
        break;

    case DWAPB_GPIO_REG_CONFIG2:
        r = s->config2;
        break;

    case DWAPB_GPIO_REG_CONFIG1:
        r = s->config1;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    trace_dwapb_gpio_read(offset, r);

    return r;
}

static void dwapb_gpio_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned int size)
{
    DWAPBGPIOState *s = DWAPB_GPIO(opaque);

    trace_dwapb_gpio_write(offset, value);

    switch (offset) {

    case DWAPB_GPIO_REG_PORTA_DR:
        s->porta_dr = value;
        break;

    case DWAPB_GPIO_REG_PORTA_DDR:
        s->porta_ddr = value;
        break;

    /* not support hardware controll mode*/
    case DWAPB_GPIO_REG_PORTA_CTL:
        //s->porta_ctl = value;
        break;

    case DWAPB_GPIO_REG_PORTB_DR:
        s->portb_dr = value;
        break;

    case DWAPB_GPIO_REG_PORTB_DDR:
        s->portb_ddr = value;
        break;

    case DWAPB_GPIO_REG_PORTB_CTL:
        //s->portb_ctl = value;
        break;

    case DWAPB_GPIO_REG_PORTC_DR:
        s->portc_dr = value;
        break;

    case DWAPB_GPIO_REG_PORTC_DDR:
        s->portc_ddr = value;
        break;

    case DWAPB_GPIO_REG_PORTC_CTL:
        //s->portc_ctl = value;
        break;

    case DWAPB_GPIO_REG_PORTD_DR:
        s->portd_dr = value;
        break;

    case DWAPB_GPIO_REG_PORTD_DDR:
        s->portd_ddr = value;
        break;

    case DWAPB_GPIO_REG_PORTD_CTL:
        //s->portd_ctl = value;
        break;

    case DWAPB_GPIO_REG_INTEN:
        s->inten = value;
        break;

    case DWAPB_GPIO_REG_INTMASK:
        s->intmask = value;
        break;

    /* not support edge-sensitive interrupt */
    case DWAPB_GPIO_REG_INTTYPE_LEVEL:
        if (value > 0)
            qemu_log("not support edge-sensitive interrupt\n");
        //s->inttype_level = value;
        break;

    case DWAPB_GPIO_REG_INT_POLARITY:
        s->int_polarity = value;
        break;

    /* Read-only register */
    case DWAPB_GPIO_REG_INTSTATUS:
        //s->intstatus = value;
        break;

    case DWAPB_GPIO_REG_RAW_INTSTATUS:
        //s->raw_intstatus = value;
        break;

    case DWAPB_GPIO_REG_DEBOUNCE:
        s->debounce = value;
        break;

    case DWAPB_GPIO_REG_PORTA_EOI:
        s->porta_eoi = value;
        break;

    case DWAPB_GPIO_REG_EXT_PORTA:
        //s->ext_porta = value;
        break;

    case DWAPB_GPIO_REG_EXT_PORTB:
        //s->ext_portb = value;
        break;

    case DWAPB_GPIO_REG_EXT_PORTC:
        //s->ext_portc = value;
        break;

    case DWAPB_GPIO_REG_EXT_PORTD:
        //s->ext_portd = value;
        break;

    case DWAPB_GPIO_REG_LS_SYNC:
        s->ls_sync = value;
        break;

    case DWAPB_GPIO_REG_ID_CODE:
        //s->id_code = value;
        break;

    case DWAPB_GPIO_REG_INT_BOTHEDGE:
        s->int_bothedge = value;
        break;

    case DWAPB_GPIO_REG_VER_ID_CODE:
        //s->ver_id_code = value;
        break;

    case DWAPB_GPIO_REG_CONFIG2:
        //s->config2 = value;
        break;

    case DWAPB_GPIO_REG_CONFIG1:
        //s->config2 = value;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
    }

    update_state(s);
}

static const MemoryRegionOps gpio_ops = {
    .read =  dwapb_gpio_read,
    .write = dwapb_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void dwapb_gpio_set(void *opaque, int line, int value)
{
    DWAPBGPIOState *s = DWAPB_GPIO(opaque);
    bool inten, int_pol;

    trace_dwapb_gpio_set(line, value);

    assert(line >= 0 && line < DWAPB_GPIO_PINS);

    inten = extract32(s->inten, line, 1);
    int_pol = extract32(s->int_polarity, line, 1);

    if (inten) {
        s->ext_porta = deposit32(s->ext_porta, line, 1, int_pol? !!value: !value);
    }

    update_state(s);
}

static void dwapb_gpio_reset(DeviceState *dev)
{
    DWAPBGPIOState *s = DWAPB_GPIO(dev);

    s->porta_dr = 0xaaaaaaaa;
    s->porta_ddr = 0;
    s->porta_ctl = 0;
    s->portb_dr = 0;
    s->portb_ddr = 0;
    s->portb_ctl = 0;
    s->portc_dr = 0;
    s->portc_ddr = 0;
    s->portc_ctl = 0;
    s->portd_dr = 0;
    s->portd_ddr = 0;
    s->portd_ctl = 0;
    s->inten = 0;
    s->intmask = 0;
    s->inttype_level = 0;
    s->int_polarity = 0;
    s->intstatus = 0;
    s->raw_intstatus = 0;
    s->debounce = 0;
    s->porta_eoi = 0;
    s->ext_porta = 0;
    s->ext_portb = 0;
    s->ext_portc = 0;
    s->ext_portd = 0;
    s->ls_sync = 0;
    s->id_code = 0x12345678;
    s->int_bothedge = 0;
    s->ver_id_code = 0x3231342a;
    s->config2 = 0;
    s->config1 = 0;
}

static const VMStateDescription vmstate_dwapb_gpio = {
    .name = TYPE_DWAPB_GPIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(porta_dr,        DWAPBGPIOState),
        VMSTATE_UINT32(porta_ddr,       DWAPBGPIOState),
        VMSTATE_UINT32(porta_ctl,       DWAPBGPIOState),
        VMSTATE_UINT32(portb_dr,        DWAPBGPIOState),
        VMSTATE_UINT32(portb_ddr,       DWAPBGPIOState),
        VMSTATE_UINT32(portb_ctl,       DWAPBGPIOState),
        VMSTATE_UINT32(portc_dr,        DWAPBGPIOState),
        VMSTATE_UINT32(portc_ddr,       DWAPBGPIOState),
        VMSTATE_UINT32(portc_ctl,       DWAPBGPIOState),
        VMSTATE_UINT32(portd_dr,        DWAPBGPIOState),
        VMSTATE_UINT32(portd_ddr,       DWAPBGPIOState),
        VMSTATE_UINT32(portd_ctl,       DWAPBGPIOState),
        VMSTATE_UINT32(inten,           DWAPBGPIOState),
        VMSTATE_UINT32(intmask,         DWAPBGPIOState),
        VMSTATE_UINT32(inttype_level,   DWAPBGPIOState),
        VMSTATE_UINT32(int_polarity,    DWAPBGPIOState),
        VMSTATE_UINT32(intstatus,       DWAPBGPIOState),
        VMSTATE_UINT32(raw_intstatus,   DWAPBGPIOState),
        VMSTATE_UINT32(debounce,        DWAPBGPIOState),
        VMSTATE_UINT32(porta_eoi,       DWAPBGPIOState),
        VMSTATE_UINT32(ext_porta,       DWAPBGPIOState),
        VMSTATE_UINT32(ext_portb,       DWAPBGPIOState),
        VMSTATE_UINT32(ext_portc,       DWAPBGPIOState),
        VMSTATE_UINT32(ext_portd,       DWAPBGPIOState),
        VMSTATE_UINT32(ls_sync,         DWAPBGPIOState),
        VMSTATE_UINT32(id_code,         DWAPBGPIOState),
        VMSTATE_UINT32(int_bothedge,    DWAPBGPIOState),
        VMSTATE_UINT32(ver_id_code,     DWAPBGPIOState),
        VMSTATE_UINT32(config2,         DWAPBGPIOState),
        VMSTATE_UINT32(config1,         DWAPBGPIOState),
        VMSTATE_END_OF_LIST()
    }
};

static Property dwapb_gpio_properties[] = {
    DEFINE_PROP_UINT32("ngpio", DWAPBGPIOState, ngpio, DWAPB_GPIO_PINS),
    DEFINE_PROP_BOOL("single_int", DWAPBGPIOState, single_int, true),
    DEFINE_PROP_END_OF_LIST(),
};

static void dwapb_gpio_realize(DeviceState *dev, Error **errp)
{
    DWAPBGPIOState *s = DWAPB_GPIO(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &gpio_ops, s,
            TYPE_DWAPB_GPIO, DWAPB_GPIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    qdev_init_gpio_in(DEVICE(s), dwapb_gpio_set, s->ngpio);
    qdev_init_gpio_out(DEVICE(s), s->output, s->ngpio);
}

static void dwapb_gpio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, dwapb_gpio_properties);
    dc->vmsd = &vmstate_dwapb_gpio;
    dc->realize = dwapb_gpio_realize;
    dc->reset = dwapb_gpio_reset;
    dc->desc = "DWAPB GPIO";
}

static const TypeInfo dwapb_gpio_info = {
    .name = TYPE_DWAPB_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DWAPBGPIOState),
    .class_init = dwapb_gpio_class_init
};

static void dwapb_gpio_register_types(void)
{
    type_register_static(&dwapb_gpio_info);
}

type_init(dwapb_gpio_register_types)