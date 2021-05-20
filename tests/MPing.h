// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#ifndef MPING_34e6d71726f2_H
#define MPING_34e6d71726f2_H

#include "msgr/Message.h"
#include "MessageId.h"

class MPing : public msgr::Message {
 public:
  MPing() : Message(TEST_MSG_PING) {}
private:
  ~MPing() {}

public:
  void decode_payload() { }
  void encode_payload(uint64_t features) { }
  const char *get_type_name() const { return "ping"; }
};

#endif
