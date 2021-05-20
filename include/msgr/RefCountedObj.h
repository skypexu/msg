#ifndef REFCOUNTEDOBJ_356733763ebe4d059854d0dc2b92595a_H
#define REFCOUNTEDOBJ_356733763ebe4d059854d0dc2b92595a_H

#include "msgr_assert.h"
#include "atomic.h"
#include "dout.h"
#include "MsgrContext.h"

namespace msgr {

struct RefCountedObject {
private:
    atomic_t nref_;
    MsgrContext *ctx_;
public:
    RefCountedObject(MsgrContext* ctx = 0, int n=1) : nref_(n), ctx_(ctx) {}
    virtual ~RefCountedObject()
    {
        assert(nref_.read() == 0);
    }

    RefCountedObject *get()
    {
        int v = nref_.inc();
        if (ctx_)
            lsubdout(ctx_, refs, 1) << "RefCountedObject::get " << this << " "
                << (v - 1) << " -> " << v << dendl;
        return this;
    }

    void put()
    {
        MsgrContext *local_pct = ctx_;
        int v = nref_.dec();
        if (v == 0)
            delete this;
        if (local_pct)
            lsubdout(local_pct, refs, 1)  << "RefCountedObject::put " << this
                << " " << (v + 1) << " -> " << v << dendl;
    }

    void set_context(MsgrContext *c) {
        ctx_ = c;
    }

    long get_nref() {
        return nref_.read();
    }
};

void intrusive_ptr_add_ref(RefCountedObject *p);
void intrusive_ptr_release(RefCountedObject *p);

} // namespace msgr
#endif // REFCOUNTEDOBJ_356733763ebe4d059854d0dc2b92595a_H
