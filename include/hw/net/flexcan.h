#ifndef FLEXCAN_H
#define FLEXCAN_H

#include "hw/sysbus.h"

enum FlexCANRegisters {
    FLEXCAN_NUM = 0x9E0 / sizeof(uint32_t) + 1,
};

typedef struct FlexCANState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t     regs[FLEXCAN_NUM];
} FlexCANState;

#define TYPE_FLEXCAN "flexcan"
#define FLEXCAN(obj) OBJECT_CHECK(FlexCANState, (obj), TYPE_FLEXCAN)

#endif /* IMX_FLEXCAN_H */
