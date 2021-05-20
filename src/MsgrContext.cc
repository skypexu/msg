#include <stdio.h>
#include <sys/types.h>

#include "msgr/MsgrContext.h"
#include "msgr/MsgrConfig.h"
#include "msgr/io_priority.h"
#include "msgr/Clock.h"

namespace msgr
{

static void do_log(void *arg, log::Entry *e)
{
    char tm_buf[128];

    e->m_stamp.sprintf(tm_buf, sizeof(tm_buf));
    flockfile(stdout);
    fputs(tm_buf, stdout);
    fputs(" ", stdout);
    fprintf(stdout, "%6d %3d ", e->m_tid, e->m_prio);
    puts(e->m_streambuf.get_str().c_str());
    fflush(stdout);
    funlockfile(stdout);
    delete e;
}

MsgrContext::MsgrContext()
: nref_(1), log_cb_(do_log), log_cb_arg_(NULL)
{
    conf_ = new MsgrConfig;
}

MsgrContext::~MsgrContext()
{
    delete conf_;
}

log::Entry *MsgrContext::create_log_entry(int v, int sub)
{
    static __thread pid_t tid = -1;
    log::Entry *e = new log::Entry();
    e->m_prio = v;
    e->m_subsys = sub;
    e->m_thread = pthread_self();
    if (tid == -1)
        tid = msgr_gettid();
    e->m_tid = tid;
    e->m_stamp = msgr_clock_now(this);
    return e;
}

void
MsgrContext::submit_entry(log::Entry *e)
{
    log_cb_(log_cb_arg_, e); 
}

} //namespace msgr
