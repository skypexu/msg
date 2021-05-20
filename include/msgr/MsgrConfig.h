#ifndef MSGR_CONFIG_6f6fa3b7ccd14d58bbf121f49b546fae_H
#define MSGR_CONFIG_6f6fa3b7ccd14d58bbf121f49b546fae_H

#include <string>

namespace msgr
{

#define msgr_subsys_     0
#define msgr_subsys_ms   1
#define msgr_subsys_refs 2
#define msgr_subsys_throttle 3
#define msgr_subsys_max  10

class MsgrConfig {
public:
    MsgrConfig();

    int     clock_offset;
    std::string ms_async_affinity_cores;
    int     ms_async_op_threads;
    int     ms_async_thread_stack_bytes;
    int     ms_async_set_affinity;
    unsigned ms_async_max_recv_bytes;
    bool    ms_concurrent_socket;
    bool    ms_bind_ipv6;
    int     ms_bind_retry_count;
    int     ms_bind_retry_delay;
    int     ms_bind_port_min;
    int     ms_bind_port_max;
    bool    ms_crc_header;
    bool    ms_crc_data;
    bool    ms_die_on_bad_msg;
    bool    ms_die_on_old_message;
    bool    ms_die_on_skipped_message;
    bool    ms_die_on_unhandled_msg;
    int     ms_dispatch_throttle_bytes;
    int     ms_dump_corrupt_message_level;
    bool    ms_dump_on_send;
    double  ms_initial_backoff;
    double  ms_inject_internal_delays;
    int     ms_inject_socket_failures;
    double  ms_max_backoff;
    bool    ms_tcp_keepalive;
    bool    ms_tcp_nodelay;
    int     ms_tcp_prefetch_max_size;
    double  ms_tcp_read_timeout;
    int     ms_tcp_rcvbuf;
    bool    msgrx_require_signatures;
    bool    msgrx_cluster_require_signatures;
    bool    msgrx_service_require_signatures;

    int     log_level[msgr_subsys_max];
};

typedef MsgrConfig md_config_t;

}

#endif // MSGR_CONFIG_6f6fa3b7ccd14d58bbf121f49b546fae_H
