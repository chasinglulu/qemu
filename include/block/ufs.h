#ifndef BLOCK_UFS_H
#define BLOCK_UFS_H

#include "hw/registerfields.h"

#define __le16  uint16_t
#define __le32  uint32_t
#define __le64  uint64_t

typedef struct QEMU_PACKED UfsReg {
    uint32_t cap;
    uint32_t rsvd0;
    uint32_t ver;
    uint32_t rsvd1;
    uint32_t hcpid;
    uint32_t hcmid;
    uint32_t ahit;
    uint32_t rsvd2;
    uint32_t is;
    uint32_t ie;
    uint32_t rsvd3[2];
    uint32_t hcs;
    uint32_t hce;
    uint32_t uecpa;
    uint32_t uecdl;
    uint32_t uecn;
    uint32_t uect;
    uint32_t uecdme;
    uint32_t utriacr;
    uint32_t utrlba;
    uint32_t utrlbau;
    uint32_t utrldbr;
    uint32_t utrlclr;
    uint32_t utrlrsr;
    uint32_t utrlcnr;
    uint32_t rsvd4[2];
    uint32_t utmrlba;
    uint32_t utmrlbau;
    uint32_t utmrldbr;
    uint32_t utmrlclr;
    uint32_t utmrlrsr;
    uint32_t rsvd5[3];
    uint32_t uiccmd;
    uint32_t ucmdarg1;
    uint32_t ucmdarg2;
    uint32_t ucmdarg3;
    uint32_t rsvd6[4];
    uint32_t rsvd7[4];
    uint32_t rsvd8[16];
    uint32_t ccap;
} UfsReg;

enum UfsRegOfs {
    UFS_REG_CAP = offsetof(UfsReg, cap),
    UFS_REG_VER = offsetof(UfsReg, ver),
    UFS_REG_HCPID = offsetof(UfsReg, hcpid),
    UFS_REG_HCMID = offsetof(UfsReg, hcmid),
    UFS_REG_AHIT = offsetof(UfsReg, ahit),
    UFS_REG_IS = offsetof(UfsReg, is),
    UFS_REG_IE = offsetof(UfsReg, ie),
    UFS_REG_HCS = offsetof(UfsReg, hcs),
    UFS_REG_HCE = offsetof(UfsReg, hce),
    UFS_REG_UECPA = offsetof(UfsReg, uecpa),
    UFS_REG_UECDL = offsetof(UfsReg, uecdl),
    UFS_REG_UECN = offsetof(UfsReg, uecn),
    UFS_REG_UECT = offsetof(UfsReg, uect),
    UFS_REG_UECDME = offsetof(UfsReg, uecdme),
    UFS_REG_UTRIACR = offsetof(UfsReg, utriacr),
    UFS_REG_UTRLBA = offsetof(UfsReg, utrlba),
    UFS_REG_UTRLBAU = offsetof(UfsReg, utrlbau),
    UFS_REG_UTRLDBR = offsetof(UfsReg, utrldbr),
    UFS_REG_UTRLCLR = offsetof(UfsReg, utrlclr),
    UFS_REG_UTRLRSR = offsetof(UfsReg, utrlrsr),
    UFS_REG_UTRLCNR = offsetof(UfsReg, utrlcnr),
    UFS_REG_UTMRLBA = offsetof(UfsReg, utmrlba),
    UFS_REG_UTMRLBAU = offsetof(UfsReg, utmrlbau),
    UFS_REG_UTMRLDBR = offsetof(UfsReg, utmrldbr),
    UFS_REG_UTMRLCLR = offsetof(UfsReg, utmrlclr),
    UFS_REG_UTMRLRSR = offsetof(UfsReg, utmrlrsr),
    UFS_REG_UICCMD = offsetof(UfsReg, uiccmd),
    UFS_REG_UCMDARG1 = offsetof(UfsReg, ucmdarg1),
    UFS_REG_UCMDARG2 = offsetof(UfsReg, ucmdarg2),
    UFS_REG_UCMDARG3 = offsetof(UfsReg, ucmdarg3),
    UFS_REG_CCAP = offsetof(UfsReg, ccap),
};

enum UfsCapShift {
    CAP_NUTRS_SHIFT = 0,
    CAP_RTT_SHIFT = 8,
    CAP_NUTMRS_SHIFT = 16,
    CAP_AUTOH8_SHIFT = 23,
    CAP_64AS_SHIFT = 24,
    CAP_OODDS_SHIFT = 25,
    CAP_UICDMETMS_SHIFT = 26,
    CAP_CS_SHIFT = 28,
};

enum UfsCapMask {
    CAP_NUTRS_MASK = 0x1f,
    CAP_RTT_MASK = 0xff,
    CAP_NUTMRS_MASK = 0x7,
    CAP_AUTOH8_MASK = 0x1,
    CAP_64AS_MASK = 0x1,
    CAP_OODDS_MASK = 0x1,
    CAP_UICDMETMS_MASK = 0x1,
    CAP_CS_MASK = 0x1,
};

#define UFS_CAP_NUTRS(cap) (((cap) >> CAP_NUTRS_SHIFT) & CAP_NUTRS_MASK)
#define UFS_CAP_RTT(cap) (((cap) >> CAP_RTT_SHIFT) & CAP_RTT_MASK)
#define UFS_CAP_NUTMRS(cap) (((cap) >> CAP_NUTMRS_SHIFT) & CAP_NUTMRS_MASK)
#define UFS_CAP_AUTOH8(cap) (((cap) >> CAP_AUTOH8_SHIFT) & CAP_AUTOH8_MASK)
#define UFS_CAP_64AS(cap) (((cap) >> CAP_64AS_SHIFT) & CAP_64AS_MASK)
#define UFS_CAP_OODDS(cap) (((cap) >> CAP_OODDS_SHIFT) & CAP_OODDS_MASK)
#define UFS_CAP_UICDMETMS(cap) \
    (((cap) >> CAP_UICDMETMS_SHIFT) & CAP_UICDMETMS_MASK)
#define UFS_CAP_CS(cap) (((cap) >> CAP_CS_SHIFT) & CAP_CS_MASK)

#define UFS_CAP_SET_NUTRS(cap, val)                       \
    (cap = (cap & ~(CAP_NUTRS_MASK << CAP_NUTRS_SHIFT)) | \
           ((uint32_t)val & CAP_NUTRS_MASK) << CAP_NUTRS_SHIFT)
#define UFS_CAP_SET_RTT(cap, val)                     \
    (cap = (cap & ~(CAP_RTT_MASK << CAP_RTT_SHIFT)) | \
           ((uint32_t)val & CAP_RTT_MASK) << CAP_RTT_SHIFT)
#define UFS_CAP_SET_NUTMRS(cap, val)                        \
    (cap = (cap & ~(CAP_NUTMRS_MASK << CAP_NUTMRS_SHIFT)) | \
           ((uint32_t)val & CAP_NUTMRS_MASK) << CAP_NUTMRS_SHIFT)
#define UFS_CAP_SET_AUTOH8(cap, val)                        \
    (cap = (cap & ~(CAP_AUTOH8_MASK << CAP_AUTOH8_SHIFT)) | \
           ((uint32_t)val & CAP_AUTOH8_MASK) << CAP_AUTOH8_SHIFT)
#define UFS_CAP_SET_64AS(cap, val)                      \
    (cap = (cap & ~(CAP_64AS_MASK << CAP_64AS_SHIFT)) | \
           ((uint32_t)val & CAP_64AS_MASK) << CAP_64AS_SHIFT)
#define UFS_CAP_SET_OODDS(cap, val)                       \
    (cap = (cap & ~(CAP_OODDS_MASK << CAP_OODDS_SHIFT)) | \
           ((uint32_t)val & CAP_OODDS_MASK) << CAP_OODDS_SHIFT)
#define UFS_CAP_SET_UICDMETMS(cap, val)                           \
    (cap = (cap & ~(CAP_UICDMETMS_MASK << CAP_UICDMETMS_SHIFT)) | \
           ((uint32_t)val & CAP_UICDMETMS_MASK) << CAP_UICDMETMS_SHIFT)
#define UFS_CAP_SET_CS(cap, val)                    \
    (cap = (cap & ~(CAP_CS_MASK << CAP_CS_SHIFT)) | \
           ((uint32_t)val & CAP_CS_MASK) << CAP_CS_SHIFT)

enum UfsIsShift {
    IS_UTRCS_SHIFT = 0,
    IS_UDEPRI_SHIFT = 1,
    IS_UE_SHIFT = 2,
    IS_UTMS_SHIFT = 3,
    IS_UPMS_SHIFT = 4,
    IS_UHXS_SHIFT = 5,
    IS_UHES_SHIFT = 6,
    IS_ULLS_SHIFT = 7,
    IS_ULSS_SHIFT = 8,
    IS_UTMRCS_SHIFT = 9,
    IS_UCCS_SHIFT = 10,
    IS_DFES_SHIFT = 11,
    IS_UTPES_SHIFT = 12,
    IS_HCFES_SHIFT = 16,
    IS_SBFES_SHIFT = 17,
    IS_CEFES_SHIFT = 18,
};

enum UfsIsMask {
    IS_UTRCS_MASK = 0x1,
    IS_UDEPRI_MASK = 0x1,
    IS_UE_MASK = 0x1,
    IS_UTMS_MASK = 0x1,
    IS_UPMS_MASK = 0x1,
    IS_UHXS_MASK = 0x1,
    IS_UHES_MASK = 0x1,
    IS_ULLS_MASK = 0x1,
    IS_ULSS_MASK = 0x1,
    IS_UTMRCS_MASK = 0x1,
    IS_UCCS_MASK = 0x1,
    IS_DFES_MASK = 0x1,
    IS_UTPES_MASK = 0x1,
    IS_HCFES_MASK = 0x1,
    IS_SBFES_MASK = 0x1,
    IS_CEFES_MASK = 0x1,
};

#define UFS_INTR_MASK                                                        \
    ((1 << IS_CEFES_SHIFT) | (1 << IS_SBFES_SHIFT) | (1 << IS_HCFES_SHIFT) | \
     (1 << IS_UTPES_SHIFT) | (1 << IS_DFES_SHIFT) | (1 << IS_UCCS_SHIFT) |   \
     (1 << IS_UTMRCS_SHIFT) | (1 << IS_ULSS_SHIFT) | (1 << IS_ULLS_SHIFT) |  \
     (1 << IS_UHES_SHIFT) | (1 << IS_UHXS_SHIFT) | (1 << IS_UPMS_SHIFT) |    \
     (1 << IS_UTMS_SHIFT) | (1 << IS_UE_SHIFT) | (1 << IS_UDEPRI_SHIFT) |    \
     (1 << IS_UTRCS_SHIFT))

#define UFS_IS_CEFES(is) (((is) >> IS_CEFES_SHIFT))
#define UFS_IS_SBFES(is) (((is) >> IS_SBFES_SHIFT))
#define UFS_IS_HCFES(is) (((is) >> IS_HCFES_SHIFT))
#define UFS_IS_UTPES(is) (((is) >> IS_UTPES_SHIFT))
#define UFS_IS_DFES(is) (((is) >> IS_DFES_SHIFT))
#define UFS_IS_UCCS(is) (((is) >> IS_UCCS_SHIFT))
#define UFS_IS_UTMRCS(is) (((is) >> IS_UTMRCS_SHIFT))
#define UFS_IS_ULSS(is) (((is) >> IS_ULSS_SHIFT))
#define UFS_IS_ULLS(is) (((is) >> IS_ULLS_SHIFT))
#define UFS_IS_UHES(is) (((is) >> IS_UHES_SHIFT))
#define UFS_IS_UHXS(is) (((is) >> IS_UHXS_SHIFT))
#define UFS_IS_UPMS(is) (((is) >> IS_UPMS_SHIFT))
#define UFS_IS_UTMS(is) (((is) >> IS_UTMS_SHIFT))
#define UFS_IS_UE(is) (((is) >> IS_UE_SHIFT))
#define UFS_IS_UDEPRI(is) (((is) >> IS_UDEPRI_SHIFT))
#define UFS_IS_UTRCS(is) (((is) >> IS_UTRCS_SHIFT))

#define UFS_IS_SET_CEFES(is, val)                     \
    (is = (is & ~(IS_CEFES_MASK << IS_CEFES_SHIFT)) | \
          ((uint32_t)val & IS_CEFES_MASK) << IS_CEFES_SHIFT)
#define UFS_IS_SET_SBFES(is, val)                     \
    (is = (is & ~(IS_SBFES_MASK << IS_SBFES_SHIFT)) | \
          ((uint32_t)val & IS_SBFES_MASK) << IS_SBFES_SHIFT)
#define UFS_IS_SET_HCFES(is, val)                     \
    (is = (is & ~(IS_HCFES_MASK << IS_HCFES_SHIFT)) | \
          ((uint32_t)val & IS_HCFES_MASK) << IS_HCFES_SHIFT)
#define UFS_IS_SET_UTPES(is, val)                     \
    (is = (is & ~(IS_UTPES_MASK << IS_UTPES_SHIFT)) | \
          ((uint32_t)val & IS_UTPES_MASK) << IS_UTPES_SHIFT)
#define UFS_IS_SET_DFES(is, val)                    \
    (is = (is & ~(IS_DFES_MASK << IS_DFES_SHIFT)) | \
          ((uint32_t)val & IS_DFES_MASK) << IS_DFES_SHIFT)
#define UFS_IS_SET_UCCS(is, val)                    \
    (is = (is & ~(IS_UCCS_MASK << IS_UCCS_SHIFT)) | \
          ((uint32_t)val & IS_UCCS_MASK) << IS_UCCS_SHIFT)
#define UFS_IS_SET_UTMRCS(is, val)                      \
    (is = (is & ~(IS_UTMRCS_MASK << IS_UTMRCS_SHIFT)) | \
          ((uint32_t)val & IS_UTMRCS_MASK) << IS_UTMRCS_SHIFT)
#define UFS_IS_SET_ULSS(is, val)                    \
    (is = (is & ~(IS_ULSS_MASK << IS_ULSS_SHIFT)) | \
          ((uint32_t)val & IS_ULSS_MASK) << IS_ULSS_SHIFT)
#define UFS_IS_SET_ULLS(is, val)                    \
    (is = (is & ~(IS_ULLS_MASK << IS_ULLS_SHIFT)) | \
          ((uint32_t)val & IS_ULLS_MASK) << IS_ULLS_SHIFT)
#define UFS_IS_SET_UHES(is, val)                    \
    (is = (is & ~(IS_UHES_MASK << IS_UHES_SHIFT)) | \
          ((uint32_t)val & IS_UHES_MASK) << IS_UHES_SHIFT)
#define UFS_IS_SET_UHXS(is, val)                    \
    (is = (is & ~(IS_UHXS_MASK << IS_UHXS_SHIFT)) | \
          ((uint32_t)val & IS_UHXS_MASK) << IS_UHXS_SHIFT)
#define UFS_IS_SET_UPMS(is, val)                    \
    (is = (is & ~(IS_UPMS_MASK << IS_UPMS_SHIFT)) | \
          ((uint32_t)val & IS_UPMS_MASK) << IS_UPMS_SHIFT)
#define UFS_IS_SET_UTMS(is, val)                    \
    (is = (is & ~(IS_UTMS_MASK << IS_UTMS_SHIFT)) | \
          ((uint32_t)val & IS_UTMS_MASK) << IS_UTMS_SHIFT)
#define UFS_IS_SET_UE(is, val)                                               \
    (is = (is & ~(IS_UE_MASK << IS_UE_SHIFT)) | ((uint32_t)val & IS_UE_MASK) \
                                                    << IS_UE_SHIFT)
#define UFS_IS_SET_UDEPRI(is, val)                      \
    (is = (is & ~(IS_UDEPRI_MASK << IS_UDEPRI_SHIFT)) | \
          ((uint32_t)val & IS_UDEPRI_MASK) << IS_UDEPRI_SHIFT)
#define UFS_IS_SET_UTRCS(is, val)                     \
    (is = (is & ~(IS_UTRCS_MASK << IS_UTRCS_SHIFT)) | \
          ((uint32_t)val & IS_UTRCS_MASK) << IS_UTRCS_SHIFT)

enum UfsHceShift {
    HCE_HCE_SHIFT = 0,
    HCE_CGE_SHIFT = 1,
};

enum UfsHceMask {
    HCE_HCE_MASK = 0x1,
    HCE_CGE_MASK = 0x1,
};

#define UFS_HCE_HCE(hce) (((hce) >> HCE_HCE_SHIFT) & HCE_HCE_MASK)
#define UFS_HCE_CGE(hce) (((hce) >> HCE_CGE_SHIFT) & HCE_CGE_MASK)

#define UFS_HCE_SET_HCE(hce, val)                     \
    (hce = (hce & ~(HCE_HCE_MASK << HCE_HCE_SHIFT)) | \
           ((uint32_t)val & HCE_HCE_MASK) << HCE_HCE_SHIFT)
#define UFS_HCE_SET_CGE(hce, val)                     \
    (hce = (hce & ~(HCE_CGE_MASK << HCE_CGE_SHIFT)) | \
           ((uint32_t)val & HCE_CGE_MASK) << HCE_CGE_SHIFT)

enum UfsHcsShift {
    HCS_DP_SHIFT = 0,
    HCS_UTRLRDY_SHIFT = 1,
    HCS_UTMRLRDY_SHIFT = 2,
    HCS_UCRDY_SHIFT = 3,
    HCS_UPMCRS_SHIFT = 8,
};

enum UfsHcsMask {
    HCS_DP_MASK = 0x1,
    HCS_UTRLRDY_MASK = 0x1,
    HCS_UTMRLRDY_MASK = 0x1,
    HCS_UCRDY_MASK = 0x1,
    HCS_UPMCRS_MASK = 0x7,
};

#define UFS_HCS_DP(hcs) (((hcs) >> HCS_DP_SHIFT) & HCS_DP_MASK)
#define UFS_HCS_UTRLRDY(hcs) (((hcs) >> HCS_UTRLRDY_SHIFT) & HCS_UTRLRDY_MASK)
#define UFS_HCS_UTMRLRDY(hcs) \
    (((hcs) >> HCS_UTMRLRDY_SHIFT) & HCS_UTMRLRDY_MASK)
#define UFS_HCS_UCRDY(hcs) (((hcs) >> HCS_UCRDY_SHIFT) & HCS_UCRDY_MASK)
#define UFS_HCS_UPMCRS(hcs) (((hcs) >> HCS_UPMCRS_SHIFT) & HCS_UPMCRS_MASK)

#define UFS_HCS_SET_DP(hcs, val)                    \
    (hcs = (hcs & ~(HCS_DP_MASK << HCS_DP_SHIFT)) | \
           ((uint32_t)val & HCS_DP_MASK) << HCS_DP_SHIFT)
#define UFS_HCS_SET_UTRLRDY(hcs, val)                         \
    (hcs = (hcs & ~(HCS_UTRLRDY_MASK << HCS_UTRLRDY_SHIFT)) | \
           ((uint32_t)val & HCS_UTRLRDY_MASK) << HCS_UTRLRDY_SHIFT)
#define UFS_HCS_SET_UTMRLRDY(hcs, val)                          \
    (hcs = (hcs & ~(HCS_UTMRLRDY_MASK << HCS_UTMRLRDY_SHIFT)) | \
           ((uint32_t)val & HCS_UTMRLRDY_MASK) << HCS_UTMRLRDY_SHIFT)
#define UFS_HCS_SET_UCRDY(hcs, val)                       \
    (hcs = (hcs & ~(HCS_UCRDY_MASK << HCS_UCRDY_SHIFT)) | \
           ((uint32_t)val & HCS_UCRDY_MASK) << HCS_UCRDY_SHIFT)
#define UFS_HCS_SET_UPMCRS(hcs, val)                        \
    (hcs = (hcs & ~(HCS_UPMCRS_MASK << HCS_UPMCRS_SHIFT)) | \
           ((uint32_t)val & HCS_UPMCRS_MASK) << HCS_UPMCRS_SHIFT)

enum UfsUtrlbaShift {
    UTRLBA_UTRLBA_SHIFT = 9,
};

enum UfsUtrlbaMask {
    UTRLBA_UTRLBA_MASK = 0x7fffff,
};

enum UfsUtmrlbaShift {
    UTMRLBA_UTMRLBA_SHIFT = 9,
};

enum UfsUtmrlbaMask {
    UTMRLBA_UTMRLBA_MASK = 0x3fffff,
};

#define UFS_UPIU_HEADER_TRANSACTION_TYPE_SHIFT 24
#define UFS_UPIU_HEADER_TRANSACTION_TYPE_MASK 0xff
#define UFS_UPIU_HEADER_TRANSACTION_TYPE(dword0)                       \
    ((be32_to_cpu(dword0) >> UFS_UPIU_HEADER_TRANSACTION_TYPE_SHIFT) & \
     UFS_UPIU_HEADER_TRANSACTION_TYPE_MASK)

#define UFS_UPIU_HEADER_QUERY_FUNC_SHIFT 16
#define UFS_UPIU_HEADER_QUERY_FUNC_MASK 0xff
#define UFS_UPIU_HEADER_QUERY_FUNC(dword1)                       \
    ((be32_to_cpu(dword1) >> UFS_UPIU_HEADER_QUERY_FUNC_SHIFT) & \
     UFS_UPIU_HEADER_QUERY_FUNC_MASK)

#define UFS_UPIU_HEADER_DATA_SEGMENT_LENGTH_SHIFT 0
#define UFS_UPIU_HEADER_DATA_SEGMENT_LENGTH_MASK 0xffff
#define UFS_UPIU_HEADER_DATA_SEGMENT_LENGTH(dword2)                       \
    ((be32_to_cpu(dword2) >> UFS_UPIU_HEADER_DATA_SEGMENT_LENGTH_SHIFT) & \
     UFS_UPIU_HEADER_DATA_SEGMENT_LENGTH_MASK)

typedef struct QEMU_PACKED DeviceDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint8_t device;
    uint8_t device_class;
    uint8_t device_sub_class;
    uint8_t protocol;
    uint8_t number_lu;
    uint8_t number_wlu;
    uint8_t boot_enable;
    uint8_t descr_access_en;
    uint8_t init_power_mode;
    uint8_t high_priority_lun;
    uint8_t secure_removal_type;
    uint8_t security_lu;
    uint8_t background_ops_term_lat;
    uint8_t init_active_icc_level;
    uint16_t spec_version;
    uint16_t manufacture_date;
    uint8_t manufacturer_name;
    uint8_t product_name;
    uint8_t serial_number;
    uint8_t oem_id;
    uint16_t manufacturer_id;
    uint8_t ud_0_base_offset;
    uint8_t ud_config_p_length;
    uint8_t device_rtt_cap;
    uint16_t periodic_rtc_update;
    uint8_t ufs_features_support;
    uint8_t ffu_timeout;
    uint8_t queue_depth;
    uint16_t device_version;
    uint8_t num_secure_wp_area;
    uint32_t psa_max_data_size;
    uint8_t psa_state_timeout;
    uint8_t product_revision_level;
    uint8_t reserved[36];
    uint32_t extended_ufs_features_support;
    uint8_t write_booster_buffer_preserve_user_space_en;
    uint8_t write_booster_buffer_type;
    uint32_t num_shared_write_booster_buffer_alloc_units;
} DeviceDescriptor;

typedef struct QEMU_PACKED GeometryDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint8_t media_technology;
    uint8_t reserved;
    uint64_t total_raw_device_capacity;
    uint8_t max_number_lu;
    uint32_t segment_size;
    uint8_t allocation_unit_size;
    uint8_t min_addr_block_size;
    uint8_t optimal_read_block_size;
    uint8_t optimal_write_block_size;
    uint8_t max_in_buffer_size;
    uint8_t max_out_buffer_size;
    uint8_t rpmb_read_write_size;
    uint8_t dynamic_capacity_resource_policy;
    uint8_t data_ordering;
    uint8_t max_context_id_number;
    uint8_t sys_data_tag_unit_size;
    uint8_t sys_data_tag_res_size;
    uint8_t supported_sec_r_types;
    uint16_t supported_memory_types;
    uint32_t system_code_max_n_alloc_u;
    uint16_t system_code_cap_adj_fac;
    uint32_t non_persist_max_n_alloc_u;
    uint16_t non_persist_cap_adj_fac;
    uint32_t enhanced_1_max_n_alloc_u;
    uint16_t enhanced_1_cap_adj_fac;
    uint32_t enhanced_2_max_n_alloc_u;
    uint16_t enhanced_2_cap_adj_fac;
    uint32_t enhanced_3_max_n_alloc_u;
    uint16_t enhanced_3_cap_adj_fac;
    uint32_t enhanced_4_max_n_alloc_u;
    uint16_t enhanced_4_cap_adj_fac;
    uint32_t optimal_logical_block_size;
    uint8_t reserved2[7];
    uint32_t write_booster_buffer_max_n_alloc_units;
    uint8_t device_max_write_booster_l_us;
    uint8_t write_booster_buffer_cap_adj_fac;
    uint8_t supported_write_booster_buffer_user_space_reduction_types;
    uint8_t supported_write_booster_buffer_types;
} GeometryDescriptor;

#define UFS_GEOMETRY_CAPACITY_SHIFT 9

typedef struct QEMU_PACKED UnitDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint8_t unit_index;
    uint8_t lu_enable;
    uint8_t boot_lun_id;
    uint8_t lu_write_protect;
    uint8_t lu_queue_depth;
    uint8_t psa_sensitive;
    uint8_t memory_type;
    uint8_t data_reliability;
    uint8_t logical_block_size;
    uint64_t logical_block_count;
    uint32_t erase_block_size;
    uint8_t provisioning_type;
    uint64_t phy_mem_resource_count;
    uint16_t context_capabilities;
    uint8_t large_unit_granularity_m1;
    uint8_t reserved[6];
    uint32_t lu_num_write_booster_buffer_alloc_units;
} UnitDescriptor;

typedef struct QEMU_PACKED RpmbUnitDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint8_t unit_index;
    uint8_t lu_enable;
    uint8_t boot_lun_id;
    uint8_t lu_write_protect;
    uint8_t lu_queue_depth;
    uint8_t psa_sensitive;
    uint8_t memory_type;
    uint8_t reserved;
    uint8_t logical_block_size;
    uint64_t logical_block_count;
    uint32_t erase_block_size;
    uint8_t provisioning_type;
    uint64_t phy_mem_resource_count;
    uint8_t reserved2[3];
} RpmbUnitDescriptor;

typedef struct QEMU_PACKED PowerParametersDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint16_t active_icc_levels_vcc[16];
    uint16_t active_icc_levels_vccq[16];
    uint16_t active_icc_levels_vccq_2[16];
} PowerParametersDescriptor;

typedef struct QEMU_PACKED InterconnectDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint16_t bcd_unipro_version;
    uint16_t bcd_mphy_version;
} InterconnectDescriptor;

typedef struct QEMU_PACKED StringDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint16_t UC[126];
} StringDescriptor;

typedef struct QEMU_PACKED DeviceHealthDescriptor {
    uint8_t length;
    uint8_t descriptor_idn;
    uint8_t pre_eol_info;
    uint8_t device_life_time_est_a;
    uint8_t device_life_time_est_b;
    uint8_t vendor_prop_info[32];
    uint32_t refresh_total_count;
    uint32_t refresh_progress;
} DeviceHealthDescriptor;

typedef struct QEMU_PACKED Flags {
    uint8_t reserved;
    uint8_t device_init;
    uint8_t permanent_wp_en;
    uint8_t power_on_wp_en;
    uint8_t background_ops_en;
    uint8_t device_life_span_mode_en;
    uint8_t purge_enable;
    uint8_t refresh_enable;
    uint8_t phy_resource_removal;
    uint8_t busy_rtc;
    uint8_t reserved2;
    uint8_t permanently_disable_fw_update;
    uint8_t reserved3[2];
    uint8_t wb_en;
    uint8_t wb_buffer_flush_en;
    uint8_t wb_buffer_flush_during_hibernate;
    uint8_t reserved4[2];
} Flags;

typedef struct Attributes {
    uint8_t boot_lun_en;
    uint8_t reserved;
    uint8_t current_power_mode;
    uint8_t active_icc_level;
    uint8_t out_of_order_data_en;
    uint8_t background_op_status;
    uint8_t purge_status;
    uint8_t max_data_in_size;
    uint8_t max_data_out_size;
    uint32_t dyn_cap_needed;
    uint8_t ref_clk_freq;
    uint8_t config_descr_lock;
    uint8_t max_num_of_rtt;
    uint16_t exception_event_control;
    uint16_t exception_event_status;
    uint32_t seconds_passed;
    uint16_t context_conf;
    uint8_t device_ffu_status;
    uint8_t psa_state;
    uint32_t psa_data_size;
    uint8_t ref_clk_gating_wait_time;
    uint8_t device_case_rough_temperaure;
    uint8_t device_too_high_temp_boundary;
    uint8_t device_too_low_temp_boundary;
    uint8_t throttling_status;
    uint8_t wb_buffer_flush_status;
    uint8_t available_wb_buffer_size;
    uint8_t wb_buffer_life_time_est;
    uint32_t current_wb_buffer_size;
    uint8_t refresh_status;
    uint8_t refresh_freq;
    uint8_t refresh_unit;
    uint8_t refresh_method;
} Attributes;

#define UFS_TRANSACTION_SPECIFIC_FIELD_SIZE 20
#define UFS_MAX_QUERY_DATA_SIZE 256

/* Command response result code */
typedef enum CommandRespCode {
    COMMAND_RESULT_SUCESS = 0x00,
    COMMAND_RESULT_FAIL = 0x01,
} CommandRespCode;

enum {
    UFS_UPIU_FLAG_UNDERFLOW = 0x20,
    UFS_UPIU_FLAG_OVERFLOW = 0x40,
};

typedef struct QEMU_PACKED UtpUpiuHeader {
    uint8_t trans_type;
    uint8_t flags;
    uint8_t lun;
    uint8_t task_tag;
    uint8_t iid_cmd_set_type;
    uint8_t query_func;
    uint8_t response;
    uint8_t scsi_status;
    uint8_t ehs_len;
    uint8_t device_inf;
    uint16_t data_segment_length;
} UtpUpiuHeader;

/*
 * The code below is copied from the linux kernel
 * ("include/uapi/scsi/scsi_bsg_ufs.h") and modified to fit the qemu style.
 */

typedef struct QEMU_PACKED UtpUpiuQuery {
    uint8_t opcode;
    uint8_t idn;
    uint8_t index;
    uint8_t selector;
    uint16_t reserved_osf;
    uint16_t length;
    uint32_t value;
    uint32_t reserved[2];
    /* EHS length should be 0. We don't have to worry about EHS area. */
    uint8_t data[UFS_MAX_QUERY_DATA_SIZE];
} UtpUpiuQuery;

#define UFS_CDB_SIZE 16

/*
 * struct UtpUpiuCmd - Command UPIU structure
 * @data_transfer_len: Data Transfer Length DW-3
 * @cdb: Command Descriptor Block CDB DW-4 to DW-7
 */
typedef struct QEMU_PACKED UtpUpiuCmd {
    uint32_t exp_data_transfer_len;
    __uint8_t cdb[UFS_CDB_SIZE];
} UtpUpiuCmd;

/*
 * struct UtpUpiuReq - general upiu request structure
 * @header:UPIU header structure DW-0 to DW-2
 * @sc: fields structure for scsi command DW-3 to DW-7
 * @qr: fields structure for query request DW-3 to DW-7
 * @uc: use utp_upiu_query to host the 4 dwords of uic command
 */
typedef struct QEMU_PACKED UtpUpiuReq {
    UtpUpiuHeader header;
    union {
        UtpUpiuCmd sc;
        UtpUpiuQuery qr;
    };
} UtpUpiuReq;

/*
 * The code below is copied from the linux kernel ("include/ufs/ufshci.h") and
 * modified to fit the qemu style.
 */

enum {
    PWR_OK = 0x0,
    PWR_LOCAL = 0x01,
    PWR_REMOTE = 0x02,
    PWR_BUSY = 0x03,
    PWR_ERROR_CAP = 0x04,
    PWR_FATAL_ERROR = 0x05,
};

/* UIC Commands */
enum uic_cmd_dme {
    UIC_CMD_DME_GET = 0x01,
    UIC_CMD_DME_SET = 0x02,
    UIC_CMD_DME_PEER_GET = 0x03,
    UIC_CMD_DME_PEER_SET = 0x04,
    UIC_CMD_DME_POWERON = 0x10,
    UIC_CMD_DME_POWEROFF = 0x11,
    UIC_CMD_DME_ENABLE = 0x12,
    UIC_CMD_DME_RESET = 0x14,
    UIC_CMD_DME_END_PT_RST = 0x15,
    UIC_CMD_DME_LINK_STARTUP = 0x16,
    UIC_CMD_DME_HIBER_ENTER = 0x17,
    UIC_CMD_DME_HIBER_EXIT = 0x18,
    UIC_CMD_DME_TEST_MODE = 0x1A,
};

/* UIC Config result code / Generic error code */
enum {
    UIC_CMD_RESULT_SUCCESS = 0x00,
    UIC_CMD_RESULT_INVALID_ATTR = 0x01,
    UIC_CMD_RESULT_FAILURE = 0x01,
    UIC_CMD_RESULT_INVALID_ATTR_VALUE = 0x02,
    UIC_CMD_RESULT_READ_ONLY_ATTR = 0x03,
    UIC_CMD_RESULT_WRITE_ONLY_ATTR = 0x04,
    UIC_CMD_RESULT_BAD_INDEX = 0x05,
    UIC_CMD_RESULT_LOCKED_ATTR = 0x06,
    UIC_CMD_RESULT_BAD_TEST_FEATURE_INDEX = 0x07,
    UIC_CMD_RESULT_PEER_COMM_FAILURE = 0x08,
    UIC_CMD_RESULT_BUSY = 0x09,
    UIC_CMD_RESULT_DME_FAILURE = 0x0A,
};

#define MASK_UIC_COMMAND_RESULT 0xFF

/*
 * Request Descriptor Definitions
 */

/* Transfer request command type */
enum {
    UTP_CMD_TYPE_SCSI = 0x0,
    UTP_CMD_TYPE_UFS = 0x1,
    UTP_CMD_TYPE_DEV_MANAGE = 0x2,
};

/* To accommodate UFS2.0 required Command type */
enum {
    UTP_CMD_TYPE_UFS_STORAGE = 0x1,
};

enum {
    UTP_SCSI_COMMAND = 0x00000000,
    UTP_NATIVE_UFS_COMMAND = 0x10000000,
    UTP_DEVICE_MANAGEMENT_FUNCTION = 0x20000000,
    UTP_REQ_DESC_INT_CMD = 0x01000000,
    UTP_REQ_DESC_CRYPTO_ENABLE_CMD = 0x00800000,
};

/* UTP Transfer Request Data Direction (DD) */
enum {
    UTP_NO_DATA_TRANSFER = 0x00000000,
    UTP_HOST_TO_DEVICE = 0x02000000,
    UTP_DEVICE_TO_HOST = 0x04000000,
};

/* Overall command status values */
enum UtpOcsCodes {
    OCS_SUCCESS = 0x0,
    OCS_INVALID_CMD_TABLE_ATTR = 0x1,
    OCS_INVALID_PRDT_ATTR = 0x2,
    OCS_MISMATCH_DATA_BUF_SIZE = 0x3,
    OCS_MISMATCH_RESP_UPIU_SIZE = 0x4,
    OCS_PEER_COMM_FAILURE = 0x5,
    OCS_ABORTED = 0x6,
    OCS_FATAL_ERROR = 0x7,
    OCS_DEVICE_FATAL_ERROR = 0x8,
    OCS_INVALID_CRYPTO_CONFIG = 0x9,
    OCS_GENERAL_CRYPTO_ERROR = 0xa,
    OCS_INVALID_COMMAND_STATUS = 0xf,
};

enum {
    MASK_OCS = 0x0F,
};

/*
 * struct UfshcdSgEntry - UFSHCI PRD Entry
 * @addr: Physical address; DW-0 and DW-1.
 * @reserved: Reserved for future use DW-2
 * @size: size of physical segment DW-3
 */
typedef struct QEMU_PACKED UfshcdSgEntry {
    __le64 addr;
    __le32 reserved;
    __le32 size;
    /*
     * followed by variant-specific fields if
     * CONFIG_SCSI_UFS_VARIABLE_SG_ENTRY_SIZE has been defined.
     */
} UfshcdSgEntry;

/*
 * struct RequestDescHeader - Descriptor Header common to both UTRD and UTMRD
 * @dword0: Descriptor Header DW0
 * @dword1: Descriptor Header DW1
 * @dword2: Descriptor Header DW2
 * @dword3: Descriptor Header DW3
 */
typedef struct QEMU_PACKED RequestDescHeader {
    __le32 dword_0;
    __le32 dword_1;
    __le32 dword_2;
    __le32 dword_3;
} RequestDescHeader;

/*
 * struct UtpTransferReqDesc - UTP Transfer Request Descriptor (UTRD)
 * @header: UTRD header DW-0 to DW-3
 * @command_desc_base_addr_lo: UCD base address low DW-4
 * @command_desc_base_addr_hi: UCD base address high DW-5
 * @response_upiu_length: response UPIU length DW-6
 * @response_upiu_offset: response UPIU offset DW-6
 * @prd_table_length: Physical region descriptor length DW-7
 * @prd_table_offset: Physical region descriptor offset DW-7
 */
typedef struct QEMU_PACKED UtpTransferReqDesc {
    /* DW 0-3 */
    RequestDescHeader header;

    /* DW 4-5*/
    __le32 command_desc_base_addr_lo;
    __le32 command_desc_base_addr_hi;

    /* DW 6 */
    __le16 response_upiu_length;
    __le16 response_upiu_offset;

    /* DW 7 */
    __le16 prd_table_length;
    __le16 prd_table_offset;
} UtpTransferReqDesc;

/*
 * The code below is copied from the linux kernel ("include/ufs/ufs.h") and
 * modified to fit the qemu style.
 */

#define GENERAL_UPIU_REQUEST_SIZE (sizeof(UtpUpiuReq))
#define QUERY_DESC_MAX_SIZE 255
#define QUERY_DESC_MIN_SIZE 2
#define QUERY_DESC_HDR_SIZE 2
#define QUERY_OSF_SIZE (GENERAL_UPIU_REQUEST_SIZE - (sizeof(UtpUpiuHeader)))
#define UFS_SENSE_SIZE 18

#define UPIU_HEADER_DWORD(byte3, byte2, byte1, byte0) \
    cpu_to_be32((byte3 << 24) | (byte2 << 16) | (byte1 << 8) | (byte0))
/*
 * UFS device may have standard LUs and LUN id could be from 0x00 to
 * 0x7F. Standard LUs use "Peripheral Device Addressing Format".
 * UFS device may also have the Well Known LUs (also referred as W-LU)
 * which again could be from 0x00 to 0x7F. For W-LUs, device only use
 * the "Extended Addressing Format" which means the W-LUNs would be
 * from 0xc100 (SCSI_W_LUN_BASE) onwards.
 * This means max. LUN number reported from UFS device could be 0xC17F.
 */
#define UFS_UPIU_MAX_UNIT_NUM_ID 0x7F
#define UFS_UPIU_WLUN_ID (1 << 7)

/* WriteBooster buffer is available only for the logical unit from 0 to 7 */
#define UFS_UPIU_MAX_WB_LUN_ID 8

/*
 * WriteBooster buffer lifetime has a limit setted by vendor.
 * If it is over the limit, WriteBooster feature will be disabled.
 */
#define UFS_WB_EXCEED_LIFETIME 0x0B

/*
 * In UFS Spec, the Extra Header Segment (EHS) starts from byte 32 in UPIU
 * request/response packet
 */
#define EHS_OFFSET_IN_RESPONSE 32

/* Well known logical unit id in LUN field of UPIU */
enum {
    UFS_UPIU_REPORT_LUNS_WLUN = 0x81,
    UFS_UPIU_UFS_DEVICE_WLUN = 0xD0,
    UFS_UPIU_BOOT_WLUN = 0xB0,
    UFS_UPIU_RPMB_WLUN = 0xC4,
};

/*
 * UFS Protocol Information Unit related definitions
 */

/* Task management functions */
enum {
    UFS_ABORT_TASK = 0x01,
    UFS_ABORT_TASK_SET = 0x02,
    UFS_CLEAR_TASK_SET = 0x04,
    UFS_LOGICAL_RESET = 0x08,
    UFS_QUERY_TASK = 0x80,
    UFS_QUERY_TASK_SET = 0x81,
};

/* UTP UPIU Transaction Codes Initiator to Target */
enum {
    UPIU_TRANSACTION_NOP_OUT = 0x00,
    UPIU_TRANSACTION_COMMAND = 0x01,
    UPIU_TRANSACTION_DATA_OUT = 0x02,
    UPIU_TRANSACTION_TASK_REQ = 0x04,
    UPIU_TRANSACTION_QUERY_REQ = 0x16,
};

/* UTP UPIU Transaction Codes Target to Initiator */
enum {
    UPIU_TRANSACTION_NOP_IN = 0x20,
    UPIU_TRANSACTION_RESPONSE = 0x21,
    UPIU_TRANSACTION_DATA_IN = 0x22,
    UPIU_TRANSACTION_TASK_RSP = 0x24,
    UPIU_TRANSACTION_READY_XFER = 0x31,
    UPIU_TRANSACTION_QUERY_RSP = 0x36,
    UPIU_TRANSACTION_REJECT_UPIU = 0x3F,
};

/* UPIU Read/Write flags */
enum {
    UPIU_CMD_FLAGS_NONE = 0x00,
    UPIU_CMD_FLAGS_WRITE = 0x20,
    UPIU_CMD_FLAGS_READ = 0x40,
};

/* UPIU Task Attributes */
enum {
    UPIU_TASK_ATTR_SIMPLE = 0x00,
    UPIU_TASK_ATTR_ORDERED = 0x01,
    UPIU_TASK_ATTR_HEADQ = 0x02,
    UPIU_TASK_ATTR_ACA = 0x03,
};

/* UPIU Query request function */
enum {
    UPIU_QUERY_FUNC_STANDARD_READ_REQUEST = 0x01,
    UPIU_QUERY_FUNC_STANDARD_WRITE_REQUEST = 0x81,
};

/* Flag idn for Query Requests*/
enum flag_idn {
    QUERY_FLAG_IDN_FDEVICEINIT = 0x01,
    QUERY_FLAG_IDN_PERMANENT_WPE = 0x02,
    QUERY_FLAG_IDN_PWR_ON_WPE = 0x03,
    QUERY_FLAG_IDN_BKOPS_EN = 0x04,
    QUERY_FLAG_IDN_LIFE_SPAN_MODE_ENABLE = 0x05,
    QUERY_FLAG_IDN_PURGE_ENABLE = 0x06,
    QUERY_FLAG_IDN_REFRESH_ENABLE = 0x07,
    QUERY_FLAG_IDN_FPHYRESOURCEREMOVAL = 0x08,
    QUERY_FLAG_IDN_BUSY_RTC = 0x09,
    QUERY_FLAG_IDN_RESERVED3 = 0x0A,
    QUERY_FLAG_IDN_PERMANENTLY_DISABLE_FW_UPDATE = 0x0B,
    QUERY_FLAG_IDN_WB_EN = 0x0E,
    QUERY_FLAG_IDN_WB_BUFF_FLUSH_EN = 0x0F,
    QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8 = 0x10,
    QUERY_FLAG_IDN_HPB_RESET = 0x11,
    QUERY_FLAG_IDN_HPB_EN = 0x12,
    QUERY_FLAG_IDN_COUNT,
};

/* Attribute idn for Query requests */
enum attr_idn {
    QUERY_ATTR_IDN_BOOT_LU_EN = 0x00,
    QUERY_ATTR_IDN_MAX_HPB_SINGLE_CMD = 0x01,
    QUERY_ATTR_IDN_POWER_MODE = 0x02,
    QUERY_ATTR_IDN_ACTIVE_ICC_LVL = 0x03,
    QUERY_ATTR_IDN_OOO_DATA_EN = 0x04,
    QUERY_ATTR_IDN_BKOPS_STATUS = 0x05,
    QUERY_ATTR_IDN_PURGE_STATUS = 0x06,
    QUERY_ATTR_IDN_MAX_DATA_IN = 0x07,
    QUERY_ATTR_IDN_MAX_DATA_OUT = 0x08,
    QUERY_ATTR_IDN_DYN_CAP_NEEDED = 0x09,
    QUERY_ATTR_IDN_REF_CLK_FREQ = 0x0A,
    QUERY_ATTR_IDN_CONF_DESC_LOCK = 0x0B,
    QUERY_ATTR_IDN_MAX_NUM_OF_RTT = 0x0C,
    QUERY_ATTR_IDN_EE_CONTROL = 0x0D,
    QUERY_ATTR_IDN_EE_STATUS = 0x0E,
    QUERY_ATTR_IDN_SECONDS_PASSED = 0x0F,
    QUERY_ATTR_IDN_CNTX_CONF = 0x10,
    QUERY_ATTR_IDN_CORR_PRG_BLK_NUM = 0x11,
    QUERY_ATTR_IDN_RESERVED2 = 0x12,
    QUERY_ATTR_IDN_RESERVED3 = 0x13,
    QUERY_ATTR_IDN_FFU_STATUS = 0x14,
    QUERY_ATTR_IDN_PSA_STATE = 0x15,
    QUERY_ATTR_IDN_PSA_DATA_SIZE = 0x16,
    QUERY_ATTR_IDN_REF_CLK_GATING_WAIT_TIME = 0x17,
    QUERY_ATTR_IDN_CASE_ROUGH_TEMP = 0x18,
    QUERY_ATTR_IDN_HIGH_TEMP_BOUND = 0x19,
    QUERY_ATTR_IDN_LOW_TEMP_BOUND = 0x1A,
    QUERY_ATTR_IDN_THROTTLING_STATUS = 0x1B,
    QUERY_ATTR_IDN_WB_FLUSH_STATUS = 0x1C,
    QUERY_ATTR_IDN_AVAIL_WB_BUFF_SIZE = 0x1D,
    QUERY_ATTR_IDN_WB_BUFF_LIFE_TIME_EST = 0x1E,
    QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE = 0x1F,
    QUERY_ATTR_IDN_REFRESH_STATUS = 0x2C,
    QUERY_ATTR_IDN_REFRESH_FREQ = 0x2D,
    QUERY_ATTR_IDN_REFRESH_UNIT = 0x2E,
    QUERY_ATTR_IDN_COUNT,
};

/* Descriptor idn for Query requests */
enum desc_idn {
    QUERY_DESC_IDN_DEVICE = 0x0,
    QUERY_DESC_IDN_CONFIGURATION = 0x1,
    QUERY_DESC_IDN_UNIT = 0x2,
    QUERY_DESC_IDN_RFU_0 = 0x3,
    QUERY_DESC_IDN_INTERCONNECT = 0x4,
    QUERY_DESC_IDN_STRING = 0x5,
    QUERY_DESC_IDN_RFU_1 = 0x6,
    QUERY_DESC_IDN_GEOMETRY = 0x7,
    QUERY_DESC_IDN_POWER = 0x8,
    QUERY_DESC_IDN_HEALTH = 0x9,
    QUERY_DESC_IDN_MAX,
};

enum desc_header_offset {
    QUERY_DESC_LENGTH_OFFSET = 0x00,
    QUERY_DESC_DESC_TYPE_OFFSET = 0x01,
};

/* Unit descriptor parameters offsets in bytes*/
enum unit_desc_param {
    UNIT_DESC_PARAM_LEN = 0x0,
    UNIT_DESC_PARAM_TYPE = 0x1,
    UNIT_DESC_PARAM_UNIT_INDEX = 0x2,
    UNIT_DESC_PARAM_LU_ENABLE = 0x3,
    UNIT_DESC_PARAM_BOOT_LUN_ID = 0x4,
    UNIT_DESC_PARAM_LU_WR_PROTECT = 0x5,
    UNIT_DESC_PARAM_LU_Q_DEPTH = 0x6,
    UNIT_DESC_PARAM_PSA_SENSITIVE = 0x7,
    UNIT_DESC_PARAM_MEM_TYPE = 0x8,
    UNIT_DESC_PARAM_DATA_RELIABILITY = 0x9,
    UNIT_DESC_PARAM_LOGICAL_BLK_SIZE = 0xA,
    UNIT_DESC_PARAM_LOGICAL_BLK_COUNT = 0xB,
    UNIT_DESC_PARAM_ERASE_BLK_SIZE = 0x13,
    UNIT_DESC_PARAM_PROVISIONING_TYPE = 0x17,
    UNIT_DESC_PARAM_PHY_MEM_RSRC_CNT = 0x18,
    UNIT_DESC_PARAM_CTX_CAPABILITIES = 0x20,
    UNIT_DESC_PARAM_LARGE_UNIT_SIZE_M1 = 0x22,
    UNIT_DESC_PARAM_HPB_LU_MAX_ACTIVE_RGNS = 0x23,
    UNIT_DESC_PARAM_HPB_PIN_RGN_START_OFF = 0x25,
    UNIT_DESC_PARAM_HPB_NUM_PIN_RGNS = 0x27,
    UNIT_DESC_PARAM_WB_BUF_ALLOC_UNITS = 0x29,
};

/* RPMB Unit descriptor parameters offsets in bytes*/
enum rpmb_unit_desc_param {
    RPMB_UNIT_DESC_PARAM_LEN = 0x0,
    RPMB_UNIT_DESC_PARAM_TYPE = 0x1,
    RPMB_UNIT_DESC_PARAM_UNIT_INDEX = 0x2,
    RPMB_UNIT_DESC_PARAM_LU_ENABLE = 0x3,
    RPMB_UNIT_DESC_PARAM_BOOT_LUN_ID = 0x4,
    RPMB_UNIT_DESC_PARAM_LU_WR_PROTECT = 0x5,
    RPMB_UNIT_DESC_PARAM_LU_Q_DEPTH = 0x6,
    RPMB_UNIT_DESC_PARAM_PSA_SENSITIVE = 0x7,
    RPMB_UNIT_DESC_PARAM_MEM_TYPE = 0x8,
    RPMB_UNIT_DESC_PARAM_REGION_EN = 0x9,
    RPMB_UNIT_DESC_PARAM_LOGICAL_BLK_SIZE = 0xA,
    RPMB_UNIT_DESC_PARAM_LOGICAL_BLK_COUNT = 0xB,
    RPMB_UNIT_DESC_PARAM_REGION0_SIZE = 0x13,
    RPMB_UNIT_DESC_PARAM_REGION1_SIZE = 0x14,
    RPMB_UNIT_DESC_PARAM_REGION2_SIZE = 0x15,
    RPMB_UNIT_DESC_PARAM_REGION3_SIZE = 0x16,
    RPMB_UNIT_DESC_PARAM_PROVISIONING_TYPE = 0x17,
    RPMB_UNIT_DESC_PARAM_PHY_MEM_RSRC_CNT = 0x18,
};

/* Device descriptor parameters offsets in bytes*/
enum device_desc_param {
    DEVICE_DESC_PARAM_LEN = 0x0,
    DEVICE_DESC_PARAM_TYPE = 0x1,
    DEVICE_DESC_PARAM_DEVICE_TYPE = 0x2,
    DEVICE_DESC_PARAM_DEVICE_CLASS = 0x3,
    DEVICE_DESC_PARAM_DEVICE_SUB_CLASS = 0x4,
    DEVICE_DESC_PARAM_PRTCL = 0x5,
    DEVICE_DESC_PARAM_NUM_LU = 0x6,
    DEVICE_DESC_PARAM_NUM_WLU = 0x7,
    DEVICE_DESC_PARAM_BOOT_ENBL = 0x8,
    DEVICE_DESC_PARAM_DESC_ACCSS_ENBL = 0x9,
    DEVICE_DESC_PARAM_INIT_PWR_MODE = 0xA,
    DEVICE_DESC_PARAM_HIGH_PR_LUN = 0xB,
    DEVICE_DESC_PARAM_SEC_RMV_TYPE = 0xC,
    DEVICE_DESC_PARAM_SEC_LU = 0xD,
    DEVICE_DESC_PARAM_BKOP_TERM_LT = 0xE,
    DEVICE_DESC_PARAM_ACTVE_ICC_LVL = 0xF,
    DEVICE_DESC_PARAM_SPEC_VER = 0x10,
    DEVICE_DESC_PARAM_MANF_DATE = 0x12,
    DEVICE_DESC_PARAM_MANF_NAME = 0x14,
    DEVICE_DESC_PARAM_PRDCT_NAME = 0x15,
    DEVICE_DESC_PARAM_SN = 0x16,
    DEVICE_DESC_PARAM_OEM_ID = 0x17,
    DEVICE_DESC_PARAM_MANF_ID = 0x18,
    DEVICE_DESC_PARAM_UD_OFFSET = 0x1A,
    DEVICE_DESC_PARAM_UD_LEN = 0x1B,
    DEVICE_DESC_PARAM_RTT_CAP = 0x1C,
    DEVICE_DESC_PARAM_FRQ_RTC = 0x1D,
    DEVICE_DESC_PARAM_UFS_FEAT = 0x1F,
    DEVICE_DESC_PARAM_FFU_TMT = 0x20,
    DEVICE_DESC_PARAM_Q_DPTH = 0x21,
    DEVICE_DESC_PARAM_DEV_VER = 0x22,
    DEVICE_DESC_PARAM_NUM_SEC_WPA = 0x24,
    DEVICE_DESC_PARAM_PSA_MAX_DATA = 0x25,
    DEVICE_DESC_PARAM_PSA_TMT = 0x29,
    DEVICE_DESC_PARAM_PRDCT_REV = 0x2A,
    DEVICE_DESC_PARAM_HPB_VER = 0x40,
    DEVICE_DESC_PARAM_HPB_CONTROL = 0x42,
    DEVICE_DESC_PARAM_EXT_UFS_FEATURE_SUP = 0x4F,
    DEVICE_DESC_PARAM_WB_PRESRV_USRSPC_EN = 0x53,
    DEVICE_DESC_PARAM_WB_TYPE = 0x54,
    DEVICE_DESC_PARAM_WB_SHARED_ALLOC_UNITS = 0x55,
};

/* Interconnect descriptor parameters offsets in bytes*/
enum interconnect_desc_param {
    INTERCONNECT_DESC_PARAM_LEN = 0x0,
    INTERCONNECT_DESC_PARAM_TYPE = 0x1,
    INTERCONNECT_DESC_PARAM_UNIPRO_VER = 0x2,
    INTERCONNECT_DESC_PARAM_MPHY_VER = 0x4,
};

/* Geometry descriptor parameters offsets in bytes*/
enum geometry_desc_param {
    GEOMETRY_DESC_PARAM_LEN = 0x0,
    GEOMETRY_DESC_PARAM_TYPE = 0x1,
    GEOMETRY_DESC_PARAM_DEV_CAP = 0x4,
    GEOMETRY_DESC_PARAM_MAX_NUM_LUN = 0xC,
    GEOMETRY_DESC_PARAM_SEG_SIZE = 0xD,
    GEOMETRY_DESC_PARAM_ALLOC_UNIT_SIZE = 0x11,
    GEOMETRY_DESC_PARAM_MIN_BLK_SIZE = 0x12,
    GEOMETRY_DESC_PARAM_OPT_RD_BLK_SIZE = 0x13,
    GEOMETRY_DESC_PARAM_OPT_WR_BLK_SIZE = 0x14,
    GEOMETRY_DESC_PARAM_MAX_IN_BUF_SIZE = 0x15,
    GEOMETRY_DESC_PARAM_MAX_OUT_BUF_SIZE = 0x16,
    GEOMETRY_DESC_PARAM_RPMB_RW_SIZE = 0x17,
    GEOMETRY_DESC_PARAM_DYN_CAP_RSRC_PLC = 0x18,
    GEOMETRY_DESC_PARAM_DATA_ORDER = 0x19,
    GEOMETRY_DESC_PARAM_MAX_NUM_CTX = 0x1A,
    GEOMETRY_DESC_PARAM_TAG_UNIT_SIZE = 0x1B,
    GEOMETRY_DESC_PARAM_TAG_RSRC_SIZE = 0x1C,
    GEOMETRY_DESC_PARAM_SEC_RM_TYPES = 0x1D,
    GEOMETRY_DESC_PARAM_MEM_TYPES = 0x1E,
    GEOMETRY_DESC_PARAM_SCM_MAX_NUM_UNITS = 0x20,
    GEOMETRY_DESC_PARAM_SCM_CAP_ADJ_FCTR = 0x24,
    GEOMETRY_DESC_PARAM_NPM_MAX_NUM_UNITS = 0x26,
    GEOMETRY_DESC_PARAM_NPM_CAP_ADJ_FCTR = 0x2A,
    GEOMETRY_DESC_PARAM_ENM1_MAX_NUM_UNITS = 0x2C,
    GEOMETRY_DESC_PARAM_ENM1_CAP_ADJ_FCTR = 0x30,
    GEOMETRY_DESC_PARAM_ENM2_MAX_NUM_UNITS = 0x32,
    GEOMETRY_DESC_PARAM_ENM2_CAP_ADJ_FCTR = 0x36,
    GEOMETRY_DESC_PARAM_ENM3_MAX_NUM_UNITS = 0x38,
    GEOMETRY_DESC_PARAM_ENM3_CAP_ADJ_FCTR = 0x3C,
    GEOMETRY_DESC_PARAM_ENM4_MAX_NUM_UNITS = 0x3E,
    GEOMETRY_DESC_PARAM_ENM4_CAP_ADJ_FCTR = 0x42,
    GEOMETRY_DESC_PARAM_OPT_LOG_BLK_SIZE = 0x44,
    GEOMETRY_DESC_PARAM_HPB_REGION_SIZE = 0x48,
    GEOMETRY_DESC_PARAM_HPB_NUMBER_LU = 0x49,
    GEOMETRY_DESC_PARAM_HPB_SUBREGION_SIZE = 0x4A,
    GEOMETRY_DESC_PARAM_HPB_MAX_ACTIVE_REGS = 0x4B,
    GEOMETRY_DESC_PARAM_WB_MAX_ALLOC_UNITS = 0x4F,
    GEOMETRY_DESC_PARAM_WB_MAX_WB_LUNS = 0x53,
    GEOMETRY_DESC_PARAM_WB_BUFF_CAP_ADJ = 0x54,
    GEOMETRY_DESC_PARAM_WB_SUP_RED_TYPE = 0x55,
    GEOMETRY_DESC_PARAM_WB_SUP_WB_TYPE = 0x56,
};

/* Health descriptor parameters offsets in bytes*/
enum health_desc_param {
    HEALTH_DESC_PARAM_LEN = 0x0,
    HEALTH_DESC_PARAM_TYPE = 0x1,
    HEALTH_DESC_PARAM_EOL_INFO = 0x2,
    HEALTH_DESC_PARAM_LIFE_TIME_EST_A = 0x3,
    HEALTH_DESC_PARAM_LIFE_TIME_EST_B = 0x4,
};

/* WriteBooster buffer mode */
enum {
    WB_BUF_MODE_LU_DEDICATED = 0x0,
    WB_BUF_MODE_SHARED = 0x1,
};

/*
 * Logical Unit Write Protect
 * 00h: LU not write protected
 * 01h: LU write protected when fPowerOnWPEn =1
 * 02h: LU permanently write protected when fPermanentWPEn =1
 */
enum ufs_lu_wp_type {
    UFS_LU_NO_WP = 0x00,
    UFS_LU_POWER_ON_WP = 0x01,
    UFS_LU_PERM_WP = 0x02,
};

/* UTP QUERY Transaction Specific Fields OpCode */
enum query_opcode {
    UPIU_QUERY_OPCODE_NOP = 0x0,
    UPIU_QUERY_OPCODE_READ_DESC = 0x1,
    UPIU_QUERY_OPCODE_WRITE_DESC = 0x2,
    UPIU_QUERY_OPCODE_READ_ATTR = 0x3,
    UPIU_QUERY_OPCODE_WRITE_ATTR = 0x4,
    UPIU_QUERY_OPCODE_READ_FLAG = 0x5,
    UPIU_QUERY_OPCODE_SET_FLAG = 0x6,
    UPIU_QUERY_OPCODE_CLEAR_FLAG = 0x7,
    UPIU_QUERY_OPCODE_TOGGLE_FLAG = 0x8,
};

/* Query response result code */
typedef enum QueryRespCode {
    QUERY_RESULT_SUCCESS = 0x00,
    QUERY_RESULT_NOT_READABLE = 0xF6,
    QUERY_RESULT_NOT_WRITEABLE = 0xF7,
    QUERY_RESULT_ALREADY_WRITTEN = 0xF8,
    QUERY_RESULT_INVALID_LENGTH = 0xF9,
    QUERY_RESULT_INVALID_VALUE = 0xFA,
    QUERY_RESULT_INVALID_SELECTOR = 0xFB,
    QUERY_RESULT_INVALID_INDEX = 0xFC,
    QUERY_RESULT_INVALID_IDN = 0xFD,
    QUERY_RESULT_INVALID_OPCODE = 0xFE,
    QUERY_RESULT_GENERAL_FAILURE = 0xFF,
} QueryRespCode;

/* UTP Transfer Request Command Type (CT) */
enum {
    UPIU_COMMAND_SET_TYPE_SCSI = 0x0,
    UPIU_COMMAND_SET_TYPE_UFS = 0x1,
    UPIU_COMMAND_SET_TYPE_QUERY = 0x2,
};

/* Task management service response */
enum {
    UPIU_TASK_MANAGEMENT_FUNC_COMPL = 0x00,
    UPIU_TASK_MANAGEMENT_FUNC_NOT_SUPPORTED = 0x04,
    UPIU_TASK_MANAGEMENT_FUNC_SUCCEEDED = 0x08,
    UPIU_TASK_MANAGEMENT_FUNC_FAILED = 0x05,
    UPIU_INCORRECT_LOGICAL_UNIT_NO = 0x09,
};

/* UFS device power modes */
enum ufs_dev_pwr_mode {
    UFS_ACTIVE_PWR_MODE = 1,
    UFS_SLEEP_PWR_MODE = 2,
    UFS_POWERDOWN_PWR_MODE = 3,
    UFS_DEEPSLEEP_PWR_MODE = 4,
};

/*
 * struct UtpCmdRsp - Response UPIU structure
 * @residual_transfer_count: Residual transfer count DW-3
 * @reserved: Reserved double words DW-4 to DW-7
 * @sense_data_len: Sense data length DW-8 U16
 * @sense_data: Sense data field DW-8 to DW-12
 */
typedef struct QEMU_PACKED UtpCmdRsp {
    uint32_t residual_transfer_count;
    uint32_t reserved[4];
    uint16_t sense_data_len;
    uint8_t sense_data[UFS_SENSE_SIZE];
} UtpCmdRsp;

/*
 * struct UtpUpiuRsp - general upiu response structure
 * @header: UPIU header structure DW-0 to DW-2
 * @sr: fields structure for scsi command DW-3 to DW-12
 * @qr: fields structure for query request DW-3 to DW-7
 */
typedef struct QEMU_PACKED UtpUpiuRsp {
    UtpUpiuHeader header;
    union {
        UtpCmdRsp sr;
        UtpUpiuQuery qr;
    };
} UtpUpiuRsp;

static inline void _ufs_check_size(void)
{
    QEMU_BUILD_BUG_ON(sizeof(UfsReg) != 0x104);
    QEMU_BUILD_BUG_ON(sizeof(DeviceDescriptor) != 89);
    QEMU_BUILD_BUG_ON(sizeof(GeometryDescriptor) != 87);
    QEMU_BUILD_BUG_ON(sizeof(UnitDescriptor) != 45);
    QEMU_BUILD_BUG_ON(sizeof(RpmbUnitDescriptor) != 35);
    QEMU_BUILD_BUG_ON(sizeof(PowerParametersDescriptor) != 98);
    QEMU_BUILD_BUG_ON(sizeof(InterconnectDescriptor) != 6);
    QEMU_BUILD_BUG_ON(sizeof(StringDescriptor) != 254);
    QEMU_BUILD_BUG_ON(sizeof(DeviceHealthDescriptor) != 45);
    QEMU_BUILD_BUG_ON(sizeof(Flags) != 0x13);
    QEMU_BUILD_BUG_ON(sizeof(UtpUpiuHeader) != 12);
    QEMU_BUILD_BUG_ON(sizeof(UtpUpiuQuery) != 276);
    QEMU_BUILD_BUG_ON(sizeof(UtpUpiuCmd) != 20);
    QEMU_BUILD_BUG_ON(sizeof(UtpUpiuReq) != 288);
    QEMU_BUILD_BUG_ON(sizeof(UfshcdSgEntry) != 16);
    QEMU_BUILD_BUG_ON(sizeof(RequestDescHeader) != 16);
    QEMU_BUILD_BUG_ON(sizeof(UtpTransferReqDesc) != 32);
    QEMU_BUILD_BUG_ON(sizeof(UtpCmdRsp) != 40);
    QEMU_BUILD_BUG_ON(sizeof(UtpUpiuRsp) != 288);
}
#endif
