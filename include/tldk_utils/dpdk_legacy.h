#ifndef DPDK_LEGACY_H_
#define DPDK_LEGACY_H_

#include <rte_version.h>

#if RTE_VERSION_NUM(17, 5, 0, 0) <= RTE_VERSION
typedef uint32_t dpdk_lpm6_idx_t;
#else
typedef uint8_t dpdk_lpm6_idx_t;
#endif

#if RTE_VERSION_NUM(17, 11, 0, 0) <= RTE_VERSION
typedef uint16_t dpdk_port_t;
#else
typedef uint8_t dpdk_port_t;
#endif

#endif /* DPDK_LEGACY_H_ */
