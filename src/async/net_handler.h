// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2014 UnitedStack <haomai@unitedstack.com>
 *
 * Author: Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef MSGR_NET_HANDLE_H
#define MSGR_NET_HANDLE_H

#include "msgr/MsgrConfig.h"
#include "msgr/msg_types.h"

namespace msgr {

class MsgrContext;
class Messenger;

class NetHandler {
  private:
    int create_socket(int domain, bool reuse_addr=false);
    int generic_connect(const entity_addr_t& addr, bool nonblock);

    MsgrContext *cct;
  public:
    NetHandler(MsgrContext *c): cct(c) {}
    int set_nonblock(int sd);
    void set_socket_options(int sd, int prio = -1);
    int connect(const entity_addr_t &addr);
    int nonblock_connect(const entity_addr_t &addr);
};

}

#endif
