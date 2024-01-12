#ifndef TCP_H_
#define TCP_H_

#define	TCP_MAX_PROCESS	0x20

#include <nspk.h>

void
netfe_stream_term_tcp(struct netfe_lcore *fe, struct netfe_stream *fes);

void
netfe_stream_close_tcp(struct netfe_lcore *fe, struct netfe_stream *fes);

/*
 * helper function: opens IPv4 and IPv6 streams for selected port.
 */
struct netfe_stream *
netfe_stream_open_tcp(struct netfe_lcore *fe, struct netfe_sprm *sprm,
	uint32_t lcore, uint16_t op, uint32_t bidx, uint8_t server_mode);

int
netfe_lcore_init_tcp(const struct netfe_lcore_prm *prm);

struct netfe_stream *
netfe_create_fwd_stream(struct netfe_lcore *fe, struct netfe_stream *fes,
	uint32_t lcore, uint32_t bidx);

int
netfe_fwd_tcp(uint32_t lcore, struct netfe_stream *fes);

void
netfe_new_conn_tcp(struct netfe_lcore *fe, uint32_t lcore,
	struct netfe_stream *fes);

void
netfe_lcore_tcp_req(void);

void
netfe_lcore_tcp_rst(void);

int
netfe_rxtx_process_tcp(__rte_unused uint32_t lcore, struct netfe_stream *fes);

int
netfe_tx_process_tcp(uint32_t lcore, struct netfe_stream *fes);

void
netfe_lcore_tcp(void);

void
netfe_lcore_fini_tcp(void);

void
netbe_lcore_tcp(void);

int
lcore_main_tcp(void *arg);

#endif /* TCP_H_ */
