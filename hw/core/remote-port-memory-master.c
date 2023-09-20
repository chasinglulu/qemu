/*
 * QEMU remote port memory master.
 *
 * Copyright (c) 2014 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "trace.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port.h"
#include "hw/remote-port-device.h"
#include "hw/remote-port-memory-master.h"
#include "qemu/cutils.h"

#define  REMOTE_PORT_ERR_DEBUG
#ifndef REMOTE_PORT_ERR_DEBUG
#define REMOTE_PORT_DEBUG_LEVEL 0
#else
#define REMOTE_PORT_DEBUG_LEVEL 1
#endif

#define DB_PRINT_L(level, ...) do { \
    if (REMOTE_PORT_DEBUG_LEVEL > level) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0)

#define REMOTE_PORT_MEMORY_MASTER_PARENT_CLASS \
    object_class_get_parent( \
            object_class_by_name(TYPE_REMOTE_PORT_MEMORY_MASTER))

#define RP_MAX_ACCESS_SIZE 4096

MemTxResult rp_mm_access_with_def_attr(RemotePort *rp, uint32_t rp_dev,
                                       struct rp_peer_state *peer,
                                       hwaddr addr, uint64_t *buf, unsigned size,
                                       MemTxAttrs attr, bool rw,
                                       bool relative, uint64_t offset,
                                       uint32_t def_attr)
{
    RemotePortRespSlot *rsp_slot;
    RemotePortDynPkt *rsp;
    struct  {
        struct rp_pkt_busaccess_ext_base pkt;
        uint8_t reserved[RP_MAX_ACCESS_SIZE];
    } pay;
    uint8_t *data = rp_busaccess_tx_dataptr(peer, &pay.pkt);
    struct rp_encode_busaccess_in in = {0};
    int len;
    MemTxResult ret;

    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 " RW: %d\n",
               addr, *buf, rw);

    if (rw) {
        memcpy(data, buf, size);
    }

    addr += relative ? 0 : offset;

    in.cmd = rw ? RP_CMD_write : RP_CMD_read;
    in.id = rp_new_id(rp);
    in.dev = rp_dev;
    in.clk = rp_normalized_vmclk(rp);
    in.master_id = attr.requester_id;
    in.addr = addr;
    in.attr = def_attr;
    in.attr |= attr.secure ? RP_BUS_ATTR_SECURE : 0;
    in.size = size;
    in.stream_width = size;
    len = rp_encode_busaccess(peer, &pay.pkt, &in);
    len += rw ? size : 0;

    trace_remote_port_memory_master_tx_busaccess(rp_cmd_to_string(in.cmd),
        in.id, in.flags, in.dev, in.addr, in.size, in.attr);

    rp_rsp_mutex_lock(rp);
    rp_write(rp, (void *) &pay, len);

    rsp_slot = rp_dev_wait_resp(rp, in.dev, in.id);
    rsp = &rsp_slot->rsp;

    /* We dont support out of order answers yet.  */
    assert(rsp->pkt->hdr.id == in.id);

    switch (rp_get_busaccess_response(rsp->pkt)) {
    case RP_RESP_OK:
        ret = MEMTX_OK;
        break;
    case RP_RESP_ADDR_ERROR:
        ret = MEMTX_DECODE_ERROR;
        break;
    default:
        ret = MEMTX_ERROR;
        break;
    }

    if (!rw) {
        data = rp_busaccess_rx_dataptr(peer, &rsp->pkt->busaccess_ext_base);
        memcpy(buf, data, size);
    }

    trace_remote_port_memory_master_rx_busaccess(
        rp_cmd_to_string(rsp->pkt->hdr.cmd), rsp->pkt->hdr.id,
        rsp->pkt->hdr.flags, rsp->pkt->hdr.dev, rsp->pkt->busaccess.addr,
        rsp->pkt->busaccess.len, rsp->pkt->busaccess.attributes);

    rp_resp_slot_done(rp, rsp_slot);
    rp_rsp_mutex_unlock(rp);

    /*
     * For strongly ordered or transactions that don't allow Early Acking,
     * we need to drain the pending RP processing queue here. This is
     * because RP handles responses in parallel with normal requests so
     * they may get reordered. This becomes visible for example with reads
     * to read-to-clear registers that clear interrupts. Even though the
     * lowering of the interrupt-wires arrives to us before the read-resp,
     * we may handle the response before the wire update, resulting in
     * spurious interrupts.
     *
     * This has some room for optimization but for now we use the big hammer
     * and drain the entire qeueue.
     */
    rp_process(rp);

    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(rp);
    DB_PRINT_L(1, "\n");
    return ret;
}

MemTxResult rp_mm_access(RemotePort *rp, uint32_t rp_dev,
                         struct rp_peer_state *peer,
                         hwaddr addr, uint64_t *buf, unsigned size,
                         MemTxAttrs attr, bool rw,
                         bool relative, uint64_t offset)
{
    return rp_mm_access_with_def_attr(rp, rp_dev, peer, addr, buf, size, attr, rw, relative, offset, 0);
}

static MemTxResult rp_mmio_read(void *opaque, hwaddr addr,
                            uint64_t *data, unsigned size,
                            MemTxAttrs attrs)
{
    RemotePortMap *map = opaque;
    RemotePortMemoryMaster *s = map->parent;

    trace_rp_mmio_read(addr, size, s->map_offset);

    return rp_mm_access(s->rp, s->rp_dev, s->peer, addr, data, size, attrs, false, s->relative, map->offset);
}

static MemTxResult rp_mmio_write(void *opaque, hwaddr addr,
                             uint64_t value, unsigned size,
                             MemTxAttrs attrs)
{
    RemotePortMap *map = opaque;
    RemotePortMemoryMaster *s = map->parent;

    return rp_mm_access(s->rp, s->rp_dev, s->peer, addr, &value, size, attrs, true, s->relative, map->offset);
}

static const MemoryRegionOps rp_ops_template = {
    .read_with_attrs = rp_mmio_read,
    .write_with_attrs = rp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_memory_master_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemoryMaster *s = REMOTE_PORT_MEMORY_MASTER(dev);
    int i;

    /* Sanity check max access size.  */
    if (s->max_access_size > RP_MAX_ACCESS_SIZE) {
        error_setg(errp, "%s: max-access-size %d too large! MAX is %d",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size,
                   RP_MAX_ACCESS_SIZE);
        return;
    }

    if (s->max_access_size < 4) {
        error_setg(errp, "%s: max-access-size %d too small! MIN is 4",
                   TYPE_REMOTE_PORT_MEMORY_MASTER, s->max_access_size);
        return;
    }

    assert(s->rp);
    s->peer = rp_get_peer(s->rp);

    /* Create a single static region if configuration says so.  */
    if (s->map_num) {
        /* Initialize rp_ops from template.  */
        s->rp_ops = g_malloc(sizeof *s->rp_ops);
        memcpy(s->rp_ops, &rp_ops_template, sizeof *s->rp_ops);
        s->rp_ops->valid.max_access_size = s->max_access_size;
        s->rp_ops->impl.max_access_size = s->max_access_size;

        s->mmaps = g_new0(typeof(*s->mmaps), s->map_num);
        for (i = 0; i < s->map_num; ++i) {
            char *name = g_strdup_printf("rp-%d", i);

            s->mmaps[i].offset = s->map_offset;
            memory_region_init_io(&s->mmaps[i].iomem, OBJECT(dev), s->rp_ops,
                                  &s->mmaps[i], name, s->map_size);
            sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmaps[i].iomem);
            s->mmaps[i].parent = s;
            g_free(name);
        }
    }
}

static void rp_memory_master_init(Object *obj)
{
    RemotePortMemoryMaster *rpms = REMOTE_PORT_MEMORY_MASTER(obj);
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("map-num", RemotePortMemoryMaster, map_num, 0),
    DEFINE_PROP_UINT64("map-offset", RemotePortMemoryMaster, map_offset, 0),
    DEFINE_PROP_UINT64("map-size", RemotePortMemoryMaster, map_size, 0),
    DEFINE_PROP_UINT32("rp-chan0", RemotePortMemoryMaster, rp_dev, 0),
    DEFINE_PROP_BOOL("relative", RemotePortMemoryMaster, relative, false),
    DEFINE_PROP_UINT32("max-access-size", RemotePortMemoryMaster,
                       max_access_size, RP_MAX_ACCESS_SIZE),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_memory_master_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = rp_memory_master_realize;
    device_class_set_props(dc, rp_properties);
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_MASTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortMemoryMaster),
    .instance_init = rp_memory_master_init,
    .class_init    = rp_memory_master_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
