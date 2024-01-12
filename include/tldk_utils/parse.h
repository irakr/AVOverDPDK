#ifndef __PARSE_H__
#define __PARSE_H__

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <nspk.h>

#define PARSE_LIST_DELIM "-"

union parse_val {
	uint64_t u64;
	struct {
		uint16_t family;
		union {
			struct in_addr addr4;
			struct in6_addr addr6;
		};
	} in;
	struct rte_ether_addr mac;
	rte_cpuset_t cpuset;
};

const char *
format_addr(const struct sockaddr_storage *sp, char buf[], size_t len);

int parse_netbe_arg(struct netbe_port *prt, const char *arg,
	rte_cpuset_t *pcpu);

int netbe_parse_dest(const char *fname, struct netbe_dest_prm *prm);

int netfe_parse_cfg(const char *fname, struct netfe_lcore_prm *lp);

int
parse_app_options(int argc, char **argv, struct netbe_cfg *cfg,
	struct tle_ctx_param *ctx_prm,
	char *fecfg_fname, char *becfg_fname);

#endif /* __PARSE_H__ */

