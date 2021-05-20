#ifndef MSGRCONTEXT_9b6af6bf41974cf4a7c8194cd498fa4a_H
#define MSGRCONTEXT_9b6af6bf41974cf4a7c8194cd498fa4a_H

#include <stdarg.h>
#include <map>

#include "atomic.h"
#include "Spinlock.h"
#include "Entry.h"

namespace msgr
{

class MsgrConfig;
class MsgrContext {

public:
    typedef void (*log_cb_t)(void *arg, log::Entry *e);

    class AssociatedSingletonObject {
    public:
        virtual ~AssociatedSingletonObject() {}
    };

    MsgrContext();
    ~MsgrContext();

    MsgrContext *get() {
        nref_.inc();
        return this;
    }

    void put() {
        if (nref_.dec() == 0)
            delete this;
    }

    log::Entry *create_log_entry(int v, int sub);

    void submit_entry(log::Entry *entry);

    /**
     * @brief Set log callback.
     */
    void set_log_cb(log_cb_t cb) {
        log_cb_ = cb;
    }

    log_cb_t get_log_cb() const {
        return log_cb_;
    }

    void *get_log_cb_arg() const {
        return log_cb_arg_;
    }

    void set_log_cb_arg(void *arg) {
        log_cb_arg_ = arg;
    }

    template<typename T>
    void lookup_or_create_singleton_object(T*& p, const std::string &name) {
        associated_objs_lock_.lock();
        if (!associated_objs_.count(name)) {
            p = new T(this);
            associated_objs_[name] = reinterpret_cast<AssociatedSingletonObject*>(p);
        } else {
            p = reinterpret_cast<T*>(associated_objs_[name]);
        }
        associated_objs_lock_.unlock();
    }

    MsgrConfig *conf_;

private:
    atomic_t nref_;
    int log_level_;
    log_cb_t log_cb_;
    void *log_cb_arg_;

    Spinlock associated_objs_lock_;
    std::map<std::string, AssociatedSingletonObject*> associated_objs_;
};

}

#endif // MSGR_CONTEXT_9b6af6bf41974cf4a7c8194cd498fa4a_H
