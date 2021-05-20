#include <string.h>

#include "msgr/MsgrConfig.h"

namespace msgr
{

MsgrConfig::MsgrConfig()
{
    clock_offset = 0;
    ms_async_affinity_cores = "";
    ms_async_op_threads = 2;
    ms_async_thread_stack_bytes = 0;
    ms_async_set_affinity = true;
    ms_async_max_recv_bytes = 1024 * 1024 * 4;
    ms_concurrent_socket = false;
    ms_bind_ipv6 = false;
    ms_bind_retry_count = 3;
    ms_bind_retry_delay = 5;
    ms_bind_port_min = 5800;
    ms_bind_port_max = 6300;
    ms_crc_header = true;
    ms_crc_data = false;
    ms_die_on_bad_msg = false;
    ms_die_on_old_message = false;
    ms_die_on_skipped_message = false;
    ms_die_on_unhandled_msg = false;
    ms_dispatch_throttle_bytes = 100 << 20;
    ms_dump_corrupt_message_level = 1;
    ms_dump_on_send = false;
    ms_initial_backoff = 0.2;
    ms_inject_internal_delays = 0;
    ms_inject_socket_failures = false;
    ms_max_backoff = 15.0;
    ms_tcp_keepalive = true;
    ms_tcp_nodelay = true;
    ms_tcp_prefetch_max_size = 4096;
    ms_tcp_read_timeout = 900;
    ms_tcp_rcvbuf = 0;
    msgrx_require_signatures = false;
    msgrx_cluster_require_signatures = false;

    memset(log_level, 0, sizeof(log_level));
}

}
