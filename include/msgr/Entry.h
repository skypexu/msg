// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#ifndef MSGR_LOG_ENTRY_3aed78e203d0_H
#define MSGR_LOG_ENTRY_3aed78e203d0_H

#include <pthread.h>
#include <string>

#include "utime.h"
#include "PrebufferedStreambuf.h"

#define MSGR_LOG_ENTRY_PREALLOC 80

namespace msgr {
namespace log {

struct Entry {
  utime_t m_stamp;
  pthread_t m_thread;
  pid_t m_tid;
  short m_prio, m_subsys;
  const char *m_file;
  int m_line;
  Entry *m_next;

  char m_static_buf[MSGR_LOG_ENTRY_PREALLOC];
  PrebufferedStreambuf m_streambuf;

  Entry()
    : m_thread(0), m_prio(0), m_subsys(0), m_file(0), m_line(-1),
      m_next(NULL),
      m_streambuf(m_static_buf, sizeof(m_static_buf))
  {}
  Entry(utime_t s, pthread_t t, short pr, short sub,
	const char *msg = NULL)
    : m_stamp(s), m_thread(t), m_prio(pr), m_subsys(sub), m_file(0),
      m_line(-1), m_next(NULL),
      m_streambuf(m_static_buf, sizeof(m_static_buf))
  {
    if (msg) {
      std::ostream os(&m_streambuf);
      os << msg;
    }
  }

  void set_str(const std::string &s) {
    std::ostream os(&m_streambuf);
    os << s;
  }

  std::string get_str() const {
    return m_streambuf.get_str();
  }
};

}
}

#endif
