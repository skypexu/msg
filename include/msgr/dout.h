#ifndef MSGR_DOUT_4d91275c7a6e_H
#define MSGR_DOUT_4d91275c7a6e_H

#include <memory>
#include <sstream>
#include <ostream>

#include "MsgrContext.h"
#include "MsgrConfig.h"

#define dout_prefix *dout_

#define dout_impl(ctx, sub, v)                                      \
  do {                                                              \
    if ((ctx)->conf_->log_level[sub] >= (v)) {                      \
        if (0) {                                                    \
            char __array[((v >= -1) && (v <= 200)) ? 0 : -1] __attribute__((unused)); \
        }                                                           \
        std::unique_ptr<msgr::log::Entry> dout_e_ptr_(ctx->create_log_entry(v, sub)); \
        msgr::log::Entry *dout_e_ = dout_e_ptr_.get();              \
        dout_e_->m_file = __FILE__;                                 \
        dout_e_->m_line = __LINE__;                                 \
        ::msgr::MsgrContext *dout_ctx_ = (ctx);                     \
        std::ostream dout_os_(&dout_e_->m_streambuf);               \
        std::ostream *dout_ = &dout_os_;

#define dendl std::flush;                                           \
        dout_e_ptr_.release();                                      \
        dout_ctx_->submit_entry(dout_e_);                           \
    }                                                               \
  } while (0)

#define lsubdout(ctx, sub, v)  dout_impl(ctx, msgr_subsys_##sub, v) dout_prefix 
#define ldout(ctx, v)  dout_impl(ctx, dout_subsys, v) dout_prefix
#define lderr(ctx)  dout_impl(ctx, msgr_subsys_, -1) dout_prefix

#define lgeneric_subdout(ctx, sub, v) dout_impl(ctx, msgr_subsys_##sub, v) *dout_
#define lgeneric_dout(ctx, v) dout_impl(ctx, msgr_subsys_, v) *dout_           
#define lgeneric_derr(ctx) dout_impl(ctx, msgr_subsys_, -1) *dout_

namespace msgr {
    void dout_emergency(const char * const str);
    void dout_emergency(const std::string &str);
}

#endif //MSGR_DOUT_4d91275c7a6e_H
