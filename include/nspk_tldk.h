#pragma once

int nspk_tldk_udp_stream_new(UDPTldkContext *udp_ctx);

int nspk_tldk_udp_stream_send(UDPTldkContext *udp_ctx, void *data, int dlen);

int nspk_tldk_udp_stream_recv(UDPTldkContext *udp_ctx, void *data, int *dlen);

int nspk_tldk_udp_stream_delete(UDPTldkContext *udp_ctx);