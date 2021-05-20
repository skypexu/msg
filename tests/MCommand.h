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

#ifndef MCOMMAND_8884fb8506db_H
#define MCOMMAND_8884fb8506db_H

#include <vector>

#include "msgr/uuid.h"
#include "msgr/Message.h"
#include "MessageId.h"

class MCommand : public msgr::Message {
 public:
  std::vector<std::string> cmd;
  msgr::uuid_d fsid;

  MCommand()
    : Message(TEST_MSG_COMMAND) {}
  MCommand(const msgr::uuid_d &f)
    : Message(TEST_MSG_COMMAND),
      fsid(f)
      { }

private:
  ~MCommand() {}

public:  
  const char *get_type_name() const { return "command"; }
  void print(std::ostream& o) const {
    o << "command(tid " << get_tid() << ": ";
    for (unsigned i=0; i<cmd.size(); i++) {
      if (i) o << ' ';
      o << cmd[i];
    }
    o << ")";
  }
  
  void encode_payload(uint64_t features) {
    msgr::encode(cmd, payload);
  }
  void decode_payload() {
    msgr::bufferlist::iterator p = payload.begin();
    msgr::decode(cmd, p);
  }
};

#endif // MCOMMAND_8884fb8506db_H
