// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Msgr - a messenger
 *
 * Heavily modified to use libev
 * Xu Yifeng <yifeng.xyf@alibaba-inc.com>
 *
 */
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

#include <sys/types.h>
#include <sys/socket.h>
#include <malloc.h>
#include <unistd.h>
#include <algorithm>

#include "msgr/cpp_errno.h"
#include "msgr/dout.h"
#include "msgr/BufferAllocator.h"

#include "AsyncMessenger.h"
#include "AsyncConnection.h"

using namespace std;

// Constant to limit starting sequence number to 2^31.  Nothing special about it, just a big number.  PLR
#define SEQ_MASK  0x7fffffff 
#define STATE_BUFFER_SIZE 4096

#define dout_subsys msgr_subsys_ms
#undef dout_prefix
#define dout_prefix _conn_prefix(dout_)
namespace msgr {

ostream& AsyncConnection::_conn_prefix(std::ostream *dout) {
  return  *dout << "-- " << async_msgr->get_myinst().addr << " >> " << peer_addr << " conn(" << this
                << " sd=" << sd << " :" << port
                << " s=" << get_state_name(state)
                << " pgs=" << peer_global_seq
                << " cs=" << connect_seq
                << " l=" << policy.lossy
                << ").";
}

void AsyncConnection::C_stop::do_request(int id)
{
  Mutex::Locker locker(conn_->async_msgr->lock);
  Mutex::Locker locker2(conn_->lock);
  conn_->_stop();
}

void AsyncConnection::C_release::do_request(int id)
{
  conn_->_release();
}

const int AsyncConnection::TCP_PREFETCH_MIN_SIZE = 512;

AsyncConnection::AsyncConnection(MsgrContext *cct, AsyncMessenger *m, EventCenter *c)
  : Connection(cct, m), async_msgr(m), global_seq(0), connect_seq(0), peer_global_seq(0),
    out_seq(0), in_seq(0), in_seq_acked(0), state(STATE_NONE), state_after_send(0), sd(-1),
    port(-1), lock("AsyncConnection::lock"), keepalive(false), keepalive_ack(false), recv_buf(NULL),
    recv_max_prefetch(std::max(msgr->cct->conf_->ms_tcp_prefetch_max_size, TCP_PREFETCH_MIN_SIZE)),
    recv_start(0), recv_end(0), 
    is_reset_from_peer(false), got_bad_auth(false), authorizer(NULL), replacing(0),
    stopping(0),
    state_buffer(NULL), state_offset(0), net(cct), center(c),
    replacing_sd(-1), replacing_reset_session(false), in_q(&(m->dispatch_queue)), queue_reset(false),
    conn_id(m->dispatch_queue.get_id()), sending(false), want_send(false), handle_write_delayed(false)
{
  // increase refcount for EventCenter referencing us
  this->get();

  ev_process.set(*center);
  ev_process.set<AsyncConnection, &AsyncConnection::handle_process>(this);

  ev_replace.set(*center);
  ev_replace.set<AsyncConnection, &AsyncConnection::handle_replace>(this);

  ev_read.set(*center);
  ev_read.set<AsyncConnection, &AsyncConnection::process>(this);

  ev_write.set(*center);
  ev_write.set<AsyncConnection, &AsyncConnection::handle_write>(this);

  memset(msgvec, 0, sizeof(msgvec));

  recv_buf = (char *)valloc(recv_max_prefetch);
  state_buffer = (char *)memalign(64, STATE_BUFFER_SIZE);
}

AsyncConnection::~AsyncConnection()
{
  assert(!ev_process.is_active());
  assert(!ev_read.is_active());
  assert(!ev_write.is_active());
  assert(!ev_replace.is_active());

  assert(out_q.empty());
  assert(sent.empty());
  delete authorizer;
  if (recv_buf)
    free(recv_buf);
  if (state_buffer)
    free(state_buffer);
}

/* return -1 means `fd` occurs error or closed, it should be closed
 * return 0 means EAGAIN or EINTR */
int AsyncConnection::read_bulk(int fd, char *buf, uint32_t len)
{
  int nread = ::read(fd, buf, len);
  if (nread == -1) {
    if (errno == EAGAIN || errno == EINTR) {
      nread = 0;
    } else {
      int err = errno;
      ldout(async_msgr->cct, 1) << __func__ << " reading from fd=" << fd
                          << " : "<< strerror(err) << dendl;
      return -1;
    }
  } else if (nread == 0) {
    ldout(async_msgr->cct, 1) << __func__ << " peer close file descriptor "
                              << fd << dendl;
    return -1;
  }
  return nread;
}

// return the length of msg needed to be sent,
// < 0 means error occured
int AsyncConnection::do_sendmsg(struct msghdr &msg, uint32_t len, bool more)
{
  while (len > 0) {
    int r = ::sendmsg(sd, &msg, MSG_NOSIGNAL | (more ? MSG_MORE : 0));

    if (r == 0) {
      ldout(async_msgr->cct, 10) << __func__ << " sendmsg got r==0!" << dendl;
    } else if (r < 0) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN) {
        break;
      } else {
        int err = errno;
        ldout(async_msgr->cct, 1) << __func__ << " sendmsg error: " << cpp_strerror(err) << dendl;
        return r;
      }
    }

    len -= r;
    if (len == 0)
      break;

    // hrmph. drain r bytes from the front of our message.
    ldout(async_msgr->cct, 20) << __func__ << " short write did " << r << ", still have " << len << dendl;
    while (r > 0) {
      if (msg.msg_iov[0].iov_len <= (size_t)r) {
        // drain this whole item
        r -= msg.msg_iov[0].iov_len;
        msg.msg_iov++;
        msg.msg_iovlen--;
      } else {
        msg.msg_iov[0].iov_base = (char *)msg.msg_iov[0].iov_base + r;
        msg.msg_iov[0].iov_len -= r;
        break;
      }
    }
  }
  return len;
}

// return the remaining bytes, it may larger than the length of ptr
// else return < 0 means error
int AsyncConnection::_try_send(bufferlist &send_bl, bool send, bool allow_unlock)
{
  int r;
  assert(lock.is_locked_by_me());

  if (send_bl.length()) {
    if (outcoming_bl.length())
      outcoming_bl.claim_append(send_bl);
    else
      outcoming_bl.swap(send_bl);
  }

  if (async_msgr->cct->conf_->ms_inject_socket_failures && sd >= 0) {
    if ((rand() % async_msgr->cct->conf_->ms_inject_socket_failures) == 0) {
      ldout(async_msgr->cct, 0) << __func__ << " injecting socket failure" << dendl;
      ::shutdown(sd, SHUT_RDWR);
    }
  }

  if (!send)
    goto set_action;

  MSGR_STATIC_ASSERT(sizeof(msgvec) / sizeof(msgvec[0]) <= IOV_MAX);

  while (!outcoming_bl.buffers().empty()) {
    list<bufferptr>::const_iterator pb = outcoming_bl.buffers().begin();
    if (pb->can_zero_copy()) {
      if (allow_unlock)
        lock.Unlock();
      r = pb->zero_copy_to_fd(this->sd, NULL);
      if (allow_unlock)
        lock.Lock();
      if (r < 0) {
        if (r == -EAGAIN)
          goto set_action;
        if (r == -EINTR)
          continue;

        return r;
      }

      // r >= 0
      if (pb->consumed() == pb->length()) {
        outcoming_bl.splice(0, pb->length(), NULL);
        continue;
      }

      ldout(async_msgr->cct, 5) << __func__ << " remaining "
           << pb->length() - pb->consumed()
           << " needed to be sent, creating event for writing"
           << dendl;
      break;
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iovlen = 0;
    msg.msg_iov = msgvec;
    uint32_t msglen = 0;
    bool more = true;
    for (;;) {
      msgvec[msg.msg_iovlen].iov_base = (void*)(pb->c_str());
      msgvec[msg.msg_iovlen].iov_len = pb->length();
      ++msg.msg_iovlen;
      msglen += pb->length();
      ++pb;
      if (pb == outcoming_bl.buffers().end()) {
        more = false;
        break;
      }
      if (msg.msg_iovlen == (sizeof(msgvec) / sizeof(msgvec[0])))
        break;
    }

    if (allow_unlock)
      lock.Unlock();
    r = do_sendmsg(msg, msglen, more);
    if (allow_unlock)
      lock.Lock();

    assert(lock.is_locked_by_me());

    if (r < 0)
      return r;

    // "r" is the remaining length
    uint32_t sent = msglen - r;

    // trim already sent for outcoming_bl
    if (sent)
      outcoming_bl.splice(0, sent, NULL);

    if (r > 0) {
      ldout(async_msgr->cct, 5) << __func__ << " remaining " << r
                          << " needed to be sent, creating event for writing"
                          << dendl;
      break;
    }
    // only "r" == 0 continue

    ldout(async_msgr->cct, 20) << __func__ << " sent bytes " << sent
        << " remaining bytes " << outcoming_bl.length() << dendl;
  }

set_action:
  assert(lock.is_locked_by_me());

  if (outcoming_bl.length()) {
    // If there is still data, schedule write-watcher
    start_write();
  } else if (ev_write.is_active()) {
    // Unschedule write-watcher if there is no data
    Mutex::Locker locker(center->lock);
    ev_write.stop();
  }

  return outcoming_bl.length();
}

// Because this func will be called multi times to populate
// the needed buffer, so the passed in bufferptr must be the same.
// Normally, only "read_message" will pass existing bufferptr in
//
// And it will uses readahead method to reduce small read overhead,
// "recv_buf" is used to store read buffer
//
// return the remaining bytes, 0 means this buffer is finished
// else return < 0 means error
int AsyncConnection::read_until(uint32_t len, char *p)
{
  assert(len);
  ldout(async_msgr->cct, 20) << __func__ << " len is " << len << " state_offset is "
                             << state_offset << dendl;

  if (async_msgr->cct->conf_->ms_inject_socket_failures && sd >= 0) {
    if ((rand() % async_msgr->cct->conf_->ms_inject_socket_failures) == 0) {
      ldout(async_msgr->cct, 0) << __func__ << " injecting socket failure" << dendl;
      ::shutdown(sd, SHUT_RDWR);
    }
  }

  int r = 0;
  uint32_t left = len - state_offset;
  if (recv_end > recv_start) {
    assert(state_offset == 0);
    uint32_t to_read = std::min(recv_end - recv_start, left);
    memcpy(p, recv_buf+recv_start, to_read);
    recv_start += to_read;
    left -= to_read;
    ldout(async_msgr->cct, 20) << __func__ << " got " << to_read << " in buffer"
                               << " left is " << left << " buffer still has "
                               << recv_end - recv_start << dendl;
    if (left == 0) {
      state_offset = 0;
      return 0;
    }
    state_offset += to_read;
  }

  assert(recv_end == recv_start);
  recv_end = recv_start = 0;
  /* nothing left in the prefetch buffer */
  if (len > recv_max_prefetch) {
    /* this was a large read, we don't prefetch for these */
    do {
      r = read_bulk(sd, p+state_offset, left);
      ldout(async_msgr->cct, 20) << __func__ << " read_bulk left is " << left << " got " << r << dendl;
      if (r < 0) {
        ldout(async_msgr->cct, 1) << __func__ << " read failed, state is " << get_state_name(state) << dendl;
        return -1;
      } else if (r == static_cast<int>(left)) {
        state_offset = 0;
        return 0;
      }
      state_offset += r;
      left -= r;
    } while (r > 0);
  } else {
    do {
      r = read_bulk(sd, recv_buf+recv_end, recv_max_prefetch-recv_end);
      ldout(async_msgr->cct, 20) << __func__ << " read_bulk recv_end is " << recv_end
                                 << " left is " << left << " got " << r << dendl;
      if (r < 0) {
        ldout(async_msgr->cct, 1) << __func__ << " read failed, state is " << get_state_name(state) << dendl;
        return -1;
      }
      recv_end += r;
      if (r >= static_cast<int>(left)) {
        recv_start = len - state_offset;
        memcpy(p+state_offset, recv_buf, recv_start);
        state_offset = 0;
        return 0;
      }
      left -= r;
    } while (r > 0);
    memcpy(p+state_offset, recv_buf, recv_end-recv_start);
    state_offset += (recv_end - recv_start);
    recv_end = recv_start = 0;
  }
  ldout(async_msgr->cct, 20) << __func__ << " need len " << len << " remaining "
                             << len - state_offset << " bytes, state is "
                             << get_state_name(state) << dendl;
  return len - state_offset;
}

void AsyncConnection::process()
{
  center->lock.Unlock();
  lock.Lock();
  do_process();
  assert(!center->lock.is_locked_by_me());
  assert(lock.is_locked_by_me());
  center->lock.Lock();
  kick_write();
  assert(center->lock.is_locked_by_me());
  lock.Unlock();
}

void AsyncConnection::wait_sending()
{
  assert(lock.is_locked_by_me());
  assert(!center->lock.is_locked_by_me());

  while (sending) {
    want_send = true;
    cond.Wait(lock);
  }
}

void AsyncConnection::do_process()
{
  assert(!center->lock.is_locked_by_me());
  assert(lock.is_locked_by_me());

  unsigned total_recv_bytes = 0;
  int r = 0;
  int prev_state = state;

  do {
    ldout(async_msgr->cct, 20) << __func__ << " state is " << get_state_name(state)
                               << ", prev state is " << get_state_name(prev_state) << dendl;
    prev_state = state;
    switch (state) {
      case STATE_OPEN:
        {
          char tag = -1;
          r = read_until(sizeof(tag), &tag);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read tag failed, state is "
                                      << get_state_name(state) << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          if (tag == MSGR_MSGR_TAG_KEEPALIVE2) {
            state = STATE_OPEN_KEEPALIVE2;
          } else if (tag == MSGR_MSGR_TAG_KEEPALIVE2_ACK) {
            state = STATE_OPEN_KEEPALIVE2_ACK;
          } else if (tag == MSGR_MSGR_TAG_ACK) {
            state = STATE_OPEN_TAG_ACK;
          } else if (tag == MSGR_MSGR_TAG_MSG) {
            state = STATE_OPEN_MESSAGE_HEADER;
          } else if (tag == MSGR_MSGR_TAG_CLOSE) {
            state = STATE_OPEN_TAG_CLOSE;
          } else {
            ldout(async_msgr->cct, 0) << __func__ << " bad tag " << (int)tag << dendl;
            goto fail;
          }

          break;
        }

      case STATE_OPEN_KEEPALIVE2:
        {
          msgr_timespec *t;
          r = read_until(sizeof(*t), state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read keeplive timespec failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          ldout(async_msgr->cct, 30) << __func__ << " got KEEPALIVE2 tag ..." << dendl;
          t = (msgr_timespec*)state_buffer;
          keepalive_ack = true;
          keepalive_ack_stamp = utime_t(*t);
          start_write();
          state = STATE_OPEN;
          break;
        }

      case STATE_OPEN_KEEPALIVE2_ACK:
        {
          msgr_timespec *t;
          r = read_until(sizeof(*t), state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read keeplive timespec failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          t = (msgr_timespec*)state_buffer;
          last_keepalive_ack = utime_t(*t);
          ldout(async_msgr->cct, 20) << __func__ << " got KEEPALIVE_ACK" << dendl;
          state = STATE_OPEN;
          break;
        }

      case STATE_OPEN_TAG_ACK:
        {
          msgr_le64 *seq;
          r = read_until(sizeof(*seq), state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read ack seq failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          seq = (msgr_le64*)state_buffer;
          ldout(async_msgr->cct, 20) << __func__ << " got ACK" << dendl;
          handle_ack(*seq);
          state = STATE_OPEN;
          break;
        }

      case STATE_OPEN_MESSAGE_HEADER:
        {
          ldout(async_msgr->cct, 20) << __func__ << " begin MSG" << dendl;
          msgr_msg_header header;
          uint32_t header_crc;
          int len;
          len = sizeof(header);

          r = read_until(len, state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read message header failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          ldout(async_msgr->cct, 20) << __func__ << " got MSG header" << dendl;

          header = *((msgr_msg_header*)state_buffer);
          if (msgr->crcflags & MSG_CRC_HEADER)
            header_crc = msgr_crc32c(0, (unsigned char *)&header,
                                       sizeof(header) - sizeof(header.crc));

          ldout(async_msgr->cct, 20) << __func__ << " got envelope type=" << header.type
                              << " seq" <<  (uint64_t)header.seq
                              << " src " << entity_name_t(header.src)
                              << " front=" << header.front_len
                              << " data=" << header.data_len
                              << " off " << header.data_off << dendl;

          // verify header crc
          if (msgr->crcflags & MSG_CRC_HEADER && header_crc != header.crc) {
            ldout(async_msgr->cct,0) << __func__ << " reader got bad header crc "
                                     << header_crc << " != " << header.crc << dendl;
            goto fail;
          }

          // Reset state
          data_buf.clear();
          front.clear();
          middle.clear();
          data.clear();
          recv_stamp = msgr_clock_now(async_msgr->cct);
          current_header = header;
          state = STATE_OPEN_MESSAGE_THROTTLE_MESSAGE;
          break;
        }

      case STATE_OPEN_MESSAGE_THROTTLE_MESSAGE:
        {
          if (policy.throttler_messages) {
            ldout(async_msgr->cct,10) << __func__ << " wants " << 1 << " message from policy throttler "
                                << policy.throttler_messages->get_current() << "/"
                                << policy.throttler_messages->get_max() << dendl;
            Mutex::Unlocker ul(lock);
            // XXX  FIXME: may block
            policy.throttler_messages->get();
          }

          state = STATE_OPEN_MESSAGE_THROTTLE_BYTES;
          break;
        }

      case STATE_OPEN_MESSAGE_THROTTLE_BYTES:
        {
          uint64_t message_size = current_header.front_len + current_header.middle_len + current_header.data_len;
          if (message_size) {
            if (policy.throttler_bytes) {
              ldout(async_msgr->cct,10) << __func__ << " wants " << message_size << " bytes from policy throttler "
                  << policy.throttler_bytes->get_current() << "/"
                  << policy.throttler_bytes->get_max() << dendl;
              Mutex::Unlocker ul(lock);
              // XXX FIXME: may block
              policy.throttler_bytes->get(message_size);
            }
          }

          throttle_stamp = msgr_clock_now(msgr->cct);
          state = STATE_OPEN_MESSAGE_READ_FRONT;
          break;
        }

      case STATE_OPEN_MESSAGE_READ_FRONT:
        {
          // read front
          int front_len = current_header.front_len;
          if (front_len) {
            if (!front.length()) {
              bufferptr ptr;
              async_msgr->get_buffer_allocator()->allocate_msg_payload(current_header.type, front_len, ptr); // XXX check return code
              front.push_back(ptr);
            }
            {
              Mutex::Unlocker unlock_tmp(lock);
              r = read_until(front_len, front.c_str());
            }
            if (r < 0) {
              ldout(async_msgr->cct, 1) << __func__ << " read message front failed" << dendl;
              goto fail;
            } else if (r > 0) {
              break;
            }

            ldout(async_msgr->cct, 20) << __func__ << " got front " << front.length() << dendl;
          }
          state = STATE_OPEN_MESSAGE_READ_MIDDLE;
          break;
        }

      case STATE_OPEN_MESSAGE_READ_MIDDLE:
        {
          // read middle
          int middle_len = current_header.middle_len;
          if (middle_len) {
            if (!middle.length()) {
              bufferptr ptr;                                                    
              async_msgr->get_buffer_allocator()->allocate_msg_middle(current_header.type, middle_len, ptr); // XXX check return code
              middle.push_back(ptr);
            }
            {
              Mutex::Unlocker unlock_tmp(lock);
              r = read_until(middle_len, middle.c_str());
            }
            if (r < 0) {
              ldout(async_msgr->cct, 1) << __func__ << " read message middle failed" << dendl;
              goto fail;
            } else if (r > 0) {
              break;
            }
            ldout(async_msgr->cct, 20) << __func__ << " got middle " << middle.length() << dendl;
          }

          state = STATE_OPEN_MESSAGE_READ_DATA_PREPARE;
          break;
        }

      case STATE_OPEN_MESSAGE_READ_DATA_PREPARE:
        {
          // read data
          uint32_t data_len = le32_to_cpu(current_header.data_len);
          int data_off = le32_to_cpu(current_header.data_off);
          if (data_len) {
            // get a buffer
            map<msgr_tid_t,pair<bufferlist,int> >::iterator p = rx_buffers.find(current_header.tid);
            if (p != rx_buffers.end()) {
              ldout(async_msgr->cct,10) << __func__ << " seleting rx buffer v " << p->second.second
                                  << " at offset " << data_off
                                  << " len " << p->second.first.length() << dendl;
              data_buf = p->second.first;
              // make sure it's big enough
              if (data_buf.length() < data_len)
                data_buf.push_back(buffer::create(data_len - data_buf.length()));
              data_blp = data_buf.begin();
            } else {
              ldout(async_msgr->cct,20) << __func__ << " allocating new rx buffer at offset " << data_off << dendl;
              async_msgr->get_buffer_allocator()->allocate_msg_data(current_header.type, data_off, data_len, data_buf); // XXX check return code
              data_blp = data_buf.begin();
            }
          }

          msg_left = data_len;
          state = STATE_OPEN_MESSAGE_READ_DATA;
          break;
        }

      case STATE_OPEN_MESSAGE_READ_DATA:
        {
          while (msg_left > 0) {
            bufferptr bp = data_blp.get_current_ptr();
            uint32_t read = std::min(bp.length(), msg_left);
            {
              Mutex::Unlocker unlock_tmp(lock);
              r = read_until(read, bp.c_str());
              // XXX check stopping.read()
            }
            if (r < 0) {
              ldout(async_msgr->cct, 1) << __func__ << " read data error " << dendl;
              goto fail;
            } else if (r > 0) {
              break;
            }

            data_blp.advance(read);
            data.append(bp, 0, read);
            msg_left -= read;
          }

          if (msg_left == 0)
            state = STATE_OPEN_MESSAGE_READ_FOOTER;

          break;
        }

      case STATE_OPEN_MESSAGE_READ_FOOTER:
        {
          int len;

          // footer
          len = sizeof(current_footer);
          r = read_until(len, state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read footer data error " << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          current_footer = *((msgr_msg_footer*)state_buffer);
          int aborted = (current_footer.flags & MSGR_MSG_FOOTER_COMPLETE) == 0;
          ldout(async_msgr->cct, 10) << __func__ << " aborted = " << aborted << dendl;
          if (aborted) {
            ldout(async_msgr->cct, 0) << __func__ << " got " << front.length() << " + " << middle.length() << " + " << data.length()
              << " byte message.. ABORTED" << dendl;
            goto fail;
          }

          state = STATE_OPEN_MESSAGE_THROTTLE_DISPATCH_SIZE;
          break;
        }

    case STATE_OPEN_MESSAGE_THROTTLE_DISPATCH_SIZE:
        {
          //uint64_t message_size = current_header.front_len + current_header.middle_len + current_header.data_len;
          // throttle total bytes waiting for dispatch.  do this _after_ the
          // policy throttle, as this one does not deadlock (unless dispatch
          // blocks indefinitely, which it shouldn't).  in contrast, the
          // policy throttle carries for the lifetime of the message.
          //ldout(msgr->cct,10) << "reader wants " << message_size << " from dispatch throttler "
          //  << msgr->dispatch_throttler.get_current() << "/"
          //  << msgr->dispatch_throttler.get_max() << dendl;
          //msgr->dispatch_throttler.get(message_size);
          state = STATE_OPEN_MESSAGE_DISPATCH;
          break;
        }

    case STATE_OPEN_MESSAGE_DISPATCH:
        {
          uint64_t message_size = current_header.front_len + current_header.middle_len + current_header.data_len;

          total_recv_bytes += message_size;

          ldout(async_msgr->cct, 20) << __func__ << " got " << front.length() << " + " << middle.length()
                              << " + " << data.length() << " byte message" << dendl;
          Message *message = decode_message(async_msgr->cct, async_msgr->crcflags, current_header, current_footer, front, middle, data);
          if (!message) {
            ldout(async_msgr->cct, 1) << __func__ << " decode message failed, type= " << current_header.type << dendl;
            async_msgr->dispatch_throttle_release(message_size);
            goto fail;
          }

          state = STATE_OPEN;
#if 0
          //
          //  Check the signature if one should be present.  A zero return indicates success. PLR
          //

          if (session_security.get() == NULL) {
            ldout(async_msgr->cct, 10) << __func__ << " no session security set" << dendl;
          } else {
            if (session_security->check_message_signature(message)) {
              ldout(async_msgr->cct, 0) << __func__ << "Signature check failed" << dendl;
              async_msgr->dispatch_throttle_release(message_size);
              message->put();
              goto fail;
            }
          }
#endif
          message->set_byte_throttler(policy.throttler_bytes);
          message->set_message_throttler(policy.throttler_messages);

          // store reservation size in message, so we don't get confused
          // by messages entering the dispatch queue through other paths.
          message->set_dispatch_throttle_size(message_size);

          message->set_recv_stamp(recv_stamp);
          message->set_throttle_stamp(throttle_stamp);
          message->set_recv_complete_stamp(msgr_clock_now(async_msgr->cct));

          // check received seq#.  if it is old, drop the message.  
          // note that incoming messages may skip ahead.  this is convenient for the client
          // side queueing because messages can't be renumbered, but the (kernel) client will
          // occasionally pull a message out of the sent queue to send elsewhere.  in that case
          // it doesn't matter if we "got" it or not.
          if (message->get_seq() <= in_seq) {
            ldout(async_msgr->cct,0) << __func__ << " got old message "
                    << message->get_seq() << " <= " << in_seq << " " << message << " " << *message
                    << ", discarding" << dendl;
            async_msgr->dispatch_throttle_release(message_size);
            message->put();
            if (async_msgr->cct->conf_->ms_die_on_old_message) {
              msgr_assert(false, ("old msgs despite reconnect_seq feature"));
            }
            break;
          }
          if (message->get_seq() > in_seq + 1) {
            ldout(async_msgr->cct, 0) << __func__ << " missed message?  skipped from seq "
                                      << in_seq << " to " << message->get_seq() << dendl;
            if (async_msgr->cct->conf_->ms_die_on_skipped_message)
              msgr_assert(false, ("skipped incoming seq"));
          }

          message->set_connection(this);

          // note last received message.
          in_seq = message->get_seq();
          ldout(async_msgr->cct, 10) << __func__ << " got message " << message->get_seq()
                               << " " << message << " " << *message << dendl;

          if (in_seq > in_seq_acked) {
            // if send_message always successfully send, it may have no
            // opportunity to send seq ack.
            // if we lag too much (10 is experience value), activate
            // write event immediately without an epoll.
            start_write(in_seq > in_seq_acked + 10);
          }

          {
            Mutex::Unlocker unlcker(lock);
            in_q->fast_preprocess(message);
            if (in_q->can_fast_dispatch(message)) {
              in_q->fast_dispatch(message);
              // XXX check stopping.read()
            } else {
              in_q->enqueue(message, message->get_priority(), conn_id);
            }
          }

          // avoid starving other connections
          if (total_recv_bytes > async_msgr->cct->conf_->ms_async_max_recv_bytes) {
            // some bytes is still left in receiving buffer, we must process
            // it later, otherwise we could lose chance to be woken up.
            if (recv_start != recv_end)
                schedule_timer(ev_process, 0);
            return;
          }

          break;
        }

      case STATE_OPEN_TAG_CLOSE:
        {
          ldout(async_msgr->cct, 20) << __func__ << " got CLOSE" << dendl;
          wait_sending();
          if (1 == replacing) {
            do_replace();
            break;
          } else {
            Mutex::Unlocker ul(lock);
            Mutex::Locker l1(async_msgr->lock);
            Mutex::Locker l2(lock);
            _stop();
            return ;
          }
        }

      case STATE_STANDBY:
        {
          ldout(async_msgr->cct, 20) << __func__ << " enter STANDY" << dendl;
          break;
        }

      case STATE_CLOSED:
        {
          ldout(async_msgr->cct, 20) << __func__ << " socket closed" << dendl;
          break;
        }

      case STATE_WAIT:
        {
          ldout(async_msgr->cct, 20) << __func__ << " enter wait state" << dendl;
          state = STATE_CONNECTING_SEND_CONNECT_MSG;
          goto fail;
          //break;
        }

      default:
        {
          if (_process_connection() < 0)
            goto fail;
          break;
        }
    }

    continue;

fail:
    // clean up state internal variables and states
    if (prev_state >= STATE_CONNECTING_SEND_CONNECT_MSG &&
        prev_state <= STATE_CONNECTING_READY) {
      delete authorizer;
      authorizer = NULL;
      got_bad_auth = false;
    }

    wait_sending();

    if (1 == replacing) {
        do_replace();
    } else {
        fault();
        break;
    }
  } while (prev_state != state && !stopping.read());
}

int AsyncConnection::_process_connection()
{
  int r = 0;

  switch(state) {
    case STATE_WAIT_SEND:
      {
        Mutex::Locker locker(center->lock);
        if (!outcoming_bl.length()) {
          assert(state_after_send);
          state = state_after_send;
          state_after_send = 0;
          ev_read.start();
        } else {
          ev_read.stop(); // XXX lazy stop
        }
        break;
      }

    case STATE_CONNECTING:
      {
        assert(!policy.server);

        // reset connect state variables
        got_bad_auth = false;
        delete authorizer;
        authorizer = NULL;
        memset(&connect_msg, 0, sizeof(connect_msg));
        memset(&connect_reply, 0, sizeof(connect_reply));

        global_seq = async_msgr->get_global_seq();
        // close old socket.  this is safe because we stopped the reader thread above.
        if (sd >= 0) {
          Mutex::Locker locker(center->lock);
          ev_read.stop();
          ev_write.stop();
          close_socket(sd);
        }

        sd = net.nonblock_connect(get_peer_addr());
        if (sd < 0) {
          goto fail;
        }

        net.set_socket_options(sd, async_msgr->get_socket_priority());

        {
          Mutex::Locker locker(center->lock);
          ev_read.stop();
          ev_write.stop();
          ev_read.set(sd, ev::READ);
          ev_read.start();

          ev_write.set(sd, ev::WRITE); // Not started
        }

        state = STATE_CONNECTING_WAIT_BANNER;
        break;
      }

    case STATE_CONNECTING_WAIT_BANNER:
      {
        r = read_until(strlen(MSGR_BANNER), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read banner failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }

        if (memcmp(state_buffer, MSGR_BANNER, strlen(MSGR_BANNER))) {
          ldout(async_msgr->cct, 0) << __func__ << " connect protocol error (bad banner) on peer "
                              << get_peer_addr() << dendl;
          goto fail;
        }

        ldout(async_msgr->cct, 10) << __func__ << " get banner, ready to send banner" << dendl;

        bufferlist bl;
        bl.append(state_buffer, strlen(MSGR_BANNER));
        r = _try_send(bl);
        if (r == 0) {
          state = STATE_CONNECTING_WAIT_IDENTIFY_PEER;
          ldout(async_msgr->cct, 10) << __func__ << " connect write banner done: "
                               << get_peer_addr() << dendl;
        } else if (r > 0) {
          // Should disable ev_read, but lazily doing it, see 'case STAT_WAIT_SEND:'.
          state = STATE_WAIT_SEND;
          state_after_send = STATE_CONNECTING_WAIT_IDENTIFY_PEER;
          ldout(async_msgr->cct, 10) << __func__ << " connect wait for write banner: "
                               << get_peer_addr() << dendl;
        } else {
          goto fail;
        }
        break;
      }

    case STATE_CONNECTING_WAIT_IDENTIFY_PEER:
      {
        entity_addr_t paddr, peer_addr_for_me;
        bufferlist myaddrbl;

        r = read_until(sizeof(paddr)*2, state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read identify peeraddr failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }

        bufferlist bl;
        bl.append(state_buffer, sizeof(paddr)*2);
        bufferlist::iterator p = bl.begin();
        try {
          msgr::decode(paddr, p);
          msgr::decode(peer_addr_for_me, p);
        } catch (const buffer::error& e) {
          lderr(async_msgr->cct) << __func__ <<  " decode peer addr failed " << dendl;
          goto fail;
        }
        ldout(async_msgr->cct, 20) << __func__ <<  " connect read peer addr "
                             << paddr << " on socket " << sd << dendl;
        if (peer_addr != paddr) {
          if (paddr.is_blank_ip() && peer_addr.get_port() == paddr.get_port() &&
              peer_addr.get_nonce() == paddr.get_nonce()) {
            ldout(async_msgr->cct, 0) << __func__ <<  " connect claims to be " << paddr
                                << " not " << peer_addr
                                << " - presumably this is the same node!" << dendl;
          } else {
            ldout(async_msgr->cct, 0) << __func__ << " connect claims to be "
                                << paddr << " not " << peer_addr << " - wrong node!" << dendl;
            goto fail;
          }
        }

        ldout(async_msgr->cct, 20) << __func__ << " connect peer addr for me is " << peer_addr_for_me << dendl;

        lock.Unlock();
        async_msgr->learned_addr(peer_addr_for_me);
        lock.Lock();

        msgr::encode(async_msgr->get_myaddr(), myaddrbl);
        r = _try_send(myaddrbl);
        if (r == 0) {
          state = STATE_CONNECTING_SEND_CONNECT_MSG;
          ldout(async_msgr->cct, 10) << __func__ << " connect sent my addr "
              << async_msgr->get_myaddr() << dendl;
        } else if (r > 0) {
          state = STATE_WAIT_SEND;
          state_after_send = STATE_CONNECTING_SEND_CONNECT_MSG;
          ldout(async_msgr->cct, 10) << __func__ << " connect send my addr done: "
              << async_msgr->get_myaddr() << dendl;
        } else {
          int err = errno;
          ldout(async_msgr->cct, 2) << __func__ << " connect couldn't write my addr, "
              << cpp_strerror(err) << dendl;
          goto fail;
        }

        break;
      }

    case STATE_CONNECTING_SEND_CONNECT_MSG:
      {
        if (!got_bad_auth) {
          delete authorizer;
          authorizer = async_msgr->get_authorizer(peer_type, false);
        }
        bufferlist bl;

        connect_msg.features = policy.features_supported;
        connect_msg.host_type = async_msgr->get_myinst().name.type();
        connect_msg.global_seq = global_seq;
        connect_msg.connect_seq = connect_seq;
        connect_msg.protocol_version = async_msgr->get_proto_version(peer_type, true);
        connect_msg.authorizer_protocol = authorizer ? authorizer->protocol : 0;
        connect_msg.authorizer_len = authorizer ? authorizer->bl.length() : 0;
        if (authorizer)
          ldout(async_msgr->cct, 10) << __func__ <<  "connect_msg.authorizer_len="
              << connect_msg.authorizer_len << " protocol="
              << connect_msg.authorizer_protocol << dendl;
        connect_msg.flags = 0;
        if (policy.lossy)
          connect_msg.flags |= MSGR_MSG_CONNECT_LOSSY;  // this is fyi, actually, server decides!
        bl.append((char*)&connect_msg, sizeof(connect_msg));
        if (authorizer) {
          bl.append(authorizer->bl.c_str(), authorizer->bl.length());
        }
        ldout(async_msgr->cct, 10) << __func__ << " connect sending gseq=" << global_seq << " cseq="
            << connect_seq << " proto=" << connect_msg.protocol_version << dendl;

        r = _try_send(bl);
        if (r == 0) {
          state = STATE_CONNECTING_WAIT_CONNECT_REPLY;
          ldout(async_msgr->cct,20) << __func__ << " connect wrote (self +) cseq, waiting for reply" << dendl;
        } else if (r > 0) {
          state = STATE_WAIT_SEND;
          state_after_send = STATE_CONNECTING_WAIT_CONNECT_REPLY;
          ldout(async_msgr->cct, 10) << __func__ << " continue send reply " << dendl;
        } else {
          int err = errno;
          ldout(async_msgr->cct, 2) << __func__ << " connect couldn't send reply "
              << cpp_strerror(err) << dendl;
          goto fail;
        }

        break;
      }

    case STATE_CONNECTING_WAIT_CONNECT_REPLY:
      {
        r = read_until(sizeof(connect_reply), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read connect reply failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }

        connect_reply = *((msgr_msg_connect_reply*)state_buffer);
        connect_reply.features = msgr_sanitize_features(connect_reply.features);

        ldout(async_msgr->cct, 20) << __func__ << " connect got reply tag " << (int)connect_reply.tag
                             << " connect_seq " << connect_reply.connect_seq << " global_seq "
                             << connect_reply.global_seq << " proto " << connect_reply.protocol_version
                             << " flags " << (int)connect_reply.flags << " features "
                             << connect_reply.features << dendl;
        state = STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH;

        break;
      }

    case STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH:
      {
        bufferlist authorizer_reply;
        if (connect_reply.authorizer_len) {
          ldout(async_msgr->cct, 10) << __func__ << " reply.authorizer_len=" << connect_reply.authorizer_len << dendl;
          if (connect_reply.authorizer_len > STATE_BUFFER_SIZE) {
            lderr(async_msgr->cct) << __func__ << " reply.authorizer_len=" << connect_reply.authorizer_len << "is too large, abort" << dendl;
            goto fail;
          }
          r = read_until(connect_reply.authorizer_len, state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read connect reply authorizer failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }

          authorizer_reply.append(state_buffer, connect_reply.authorizer_len);
          bufferlist::iterator iter = authorizer_reply.begin();
          if (authorizer && !authorizer->verify_reply(iter)) {
            ldout(async_msgr->cct, 0) << __func__ << " failed verifying authorize reply" << dendl;
            goto fail;
          }
        }
        r = handle_connect_reply(connect_msg, connect_reply);
        if (r < 0)
          goto fail;

        // state must be changed!
        assert(state != STATE_CONNECTING_WAIT_CONNECT_REPLY_AUTH);
        break;
      }

    case STATE_CONNECTING_WAIT_ACK_SEQ:
      {
        msgr_le64 *le_seq_p;
        uint64_t newly_acked_seq = 0;
        bufferlist bl;

        r = read_until(sizeof(*le_seq_p), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read connect ack seq failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }
        le_seq_p = reinterpret_cast<msgr_le64 *>(state_buffer);
        newly_acked_seq = *le_seq_p;
        ldout(async_msgr->cct, 2) << __func__ << " got newly_acked_seq " << newly_acked_seq
                            << " vs out_seq " << out_seq << dendl;
#if 1
        discard_requeued_up_to(newly_acked_seq);
#else
        while (newly_acked_seq > out_seq) {
          Message *m = _get_next_outgoing();
          if (!m) {
            ldout(async_msgr->cct, 0) << __func__ << " newly_acked_seq: " << 
               newly_acked_seq << " out_seq: " << out_seq << dendl; 
            assert(m);
          }
          ldout(async_msgr->cct, 2) << __func__ << " discarding previously sent " << m->get_seq()
                              << " " << *m << dendl;
          assert(m->get_seq() <= newly_acked_seq);
          m->put();
          ++out_seq;
        }
#endif
        msgr_le64 le_in_seq;
        le_in_seq = in_seq;
        bl.append((char*)&le_in_seq, sizeof(le_in_seq));
        r = _try_send(bl);
        if (r == 0) {
          state = STATE_CONNECTING_READY;
          ldout(async_msgr->cct, 10) << __func__ << " send in_seq done " << dendl;
        } else if (r > 0) {
          state_after_send = STATE_CONNECTING_READY;
          state = STATE_WAIT_SEND;
          ldout(async_msgr->cct, 10) << __func__ << " continue send in_seq " << dendl;
        } else {
          goto fail;
        }
        break;
      }

    case STATE_CONNECTING_READY:
      {
        // hooray!
        peer_global_seq = connect_reply.global_seq;
        policy.lossy = connect_reply.flags & MSGR_MSG_CONNECT_LOSSY;
        state = STATE_OPEN;
        connect_seq += 1;
        assert(connect_seq == connect_reply.connect_seq);
        backoff = utime_t();
        set_features((uint64_t)connect_reply.features & (uint64_t)connect_msg.features);
        ldout(async_msgr->cct, 10) << __func__ << " connect success " << connect_seq
                                   << ", lossy = " << policy.lossy << ", features "
                                   << get_features() << dendl;

#if 0
        // If we have an authorizer, get a new AuthSessionHandler to deal with ongoing security of the
        // connection.  PLR
        if (authorizer != NULL) {
          session_security.reset(
              get_auth_session_handler(async_msgr->cct,
                                       authorizer->protocol,
                                       authorizer->session_key,
                                       get_features()));
        } else {
          // We have no authorizer, so we shouldn't be applying security to messages in this AsyncConnection.  PLR
          session_security.reset();
        }
#endif
        queue_reset = true;
        {
            Mutex::Unlocker unlocker(lock);
            in_q->queue_connect(this);
            async_msgr->ms_deliver_handle_fast_connect(this);
        }
        break;
      }

    case STATE_ACCEPTING:
      {
        bufferlist bl;

        if (net.set_nonblock(sd) < 0)
          goto fail;

        net.set_socket_options(sd);

        bl.append(MSGR_BANNER, strlen(MSGR_BANNER));

        msgr::encode(async_msgr->get_myaddr(), bl);
        port = async_msgr->get_myaddr().get_port();
        // and peer's socket addr (they might not know their ip)
        socklen_t len = sizeof(socket_addr.ss_addr());
        r = ::getpeername(sd, (sockaddr*)&socket_addr.ss_addr(), &len);
        if (r < 0) {
          int err = errno;
          ldout(async_msgr->cct, 0) << __func__ << " failed to getpeername "
                              << cpp_strerror(err) << dendl;
          goto fail;
        }
        msgr::encode(socket_addr, bl);
        ldout(async_msgr->cct, 1) << __func__ << " sd=" << sd << " " << socket_addr << dendl;

        r = _try_send(bl);
        if (r == 0) {
          state = STATE_ACCEPTING_WAIT_BANNER_ADDR;
          ldout(async_msgr->cct, 10) << __func__ << " write banner and addr done: "
            << get_peer_addr() << dendl;
        } else if (r > 0) {
          state = STATE_WAIT_SEND;
          state_after_send = STATE_ACCEPTING_WAIT_BANNER_ADDR;
          ldout(async_msgr->cct, 10) << __func__ << " wait for write banner and addr: "
                              << get_peer_addr() << dendl;
        } else {
          goto fail;
        }

        break;
      }
    case STATE_ACCEPTING_WAIT_BANNER_ADDR:
      {
        bufferlist addr_bl;
        entity_addr_t peer_addr;

        r = read_until(strlen(MSGR_BANNER) + sizeof(peer_addr), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read peer banner and addr failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }

        if (memcmp(state_buffer, MSGR_BANNER, strlen(MSGR_BANNER))) {
          ldout(async_msgr->cct, 1) << __func__ << " accept peer sent bad banner '" << state_buffer
                                    << "' (should be '" << MSGR_BANNER << "')" << dendl;
          goto fail;
        }

        addr_bl.append(state_buffer+strlen(MSGR_BANNER), sizeof(peer_addr));
        {
          bufferlist::iterator ti = addr_bl.begin();
          msgr::decode(peer_addr, ti);
        }

        ldout(async_msgr->cct, 10) << __func__ << " accept peer addr is " << peer_addr << dendl;
        if (peer_addr.is_blank_ip()) {
          // peer apparently doesn't know what ip they have; figure it out for them.
          int port = peer_addr.get_port();
          peer_addr.addr = socket_addr.addr;
          peer_addr.set_port(port);
          ldout(async_msgr->cct, 0) << __func__ << " accept peer addr is really " << peer_addr
                             << " (socket is " << socket_addr << ")" << dendl;
        }
        set_peer_addr(peer_addr);  // so that connection_state gets set up
        state = STATE_ACCEPTING_WAIT_CONNECT_MSG;
        break;
      }

    case STATE_ACCEPTING_WAIT_CONNECT_MSG:
      {
        r = read_until(sizeof(connect_msg), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read connect msg failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }

        connect_msg = *((msgr_msg_connect*)state_buffer);
        // sanitize features
        connect_msg.features = msgr_sanitize_features(connect_msg.features);
        state = STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH;
        break;
      }

    case STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH:
      {
        bufferlist authorizer_bl, authorizer_reply;

        if (connect_msg.authorizer_len) {
          r = read_until(connect_msg.authorizer_len, state_buffer);
          if (r < 0) {
            ldout(async_msgr->cct, 1) << __func__ << " read connect msg failed" << dendl;
            goto fail;
          } else if (r > 0) {
            break;
          }
          authorizer_bl.append(state_buffer, connect_msg.authorizer_len);
        }

        ldout(async_msgr->cct, 20) << __func__ << " accept got peer connect_seq "
                             << connect_msg.connect_seq << " global_seq "
                             << connect_msg.global_seq << dendl;
        set_peer_type(connect_msg.host_type);
        policy = async_msgr->get_policy(connect_msg.host_type);
        ldout(async_msgr->cct, 10) << __func__ << " accept of host_type " << connect_msg.host_type
                                   << ", policy.lossy=" << policy.lossy << " policy.server="
                                   << policy.server << " policy.standby=" << policy.standby
                                   << " policy.resetcheck=" << policy.resetcheck << dendl;

        r = handle_connect_msg(connect_msg, authorizer_bl, authorizer_reply);
        if (r < 0)
          goto fail;

        // state is changed by "handle_connect_msg"
        assert(state != STATE_ACCEPTING_WAIT_CONNECT_MSG_AUTH);
        break;
      }

    case STATE_ACCEPTING_WAIT_SEQ:
      {
        msgr_le64 *seq;
        uint64_t newly_acked_seq;
        r = read_until(sizeof(*seq), state_buffer);
        if (r < 0) {
          ldout(async_msgr->cct, 1) << __func__ << " read ack seq failed" << dendl;
          goto fail;
        } else if (r > 0) {
          break;
        }
        seq = reinterpret_cast<msgr_le64 *>(state_buffer);
        newly_acked_seq = *seq;
        ldout(async_msgr->cct, 2) << __func__ << " accept get newly_acked_seq " << newly_acked_seq << dendl;
        discard_requeued_up_to(newly_acked_seq);
        state = STATE_ACCEPTING_READY;
        break;
      }

    case STATE_ACCEPTING_READY:
      {
        ldout(async_msgr->cct, 20) << __func__ << " accept done" << dendl;
        state = STATE_OPEN;
        memset(&connect_msg, 0, sizeof(connect_msg));
        break;
      }

    default:
      {
        lderr(async_msgr->cct) << __func__ << " bad state: " << get_state_name(state) << dendl;
        assert(0);
      }
  }

  return 0;

fail:
  return -1;
}

int AsyncConnection::handle_connect_reply(msgr_msg_connect &connect, msgr_msg_connect_reply &reply)
{
  uint64_t feat_missing;

  switch(reply.tag) {
  case MSGR_MSGR_TAG_FEATURES:
    ldout(async_msgr->cct, 0) << __func__ << " connect protocol feature mismatch, my "
                        << std::hex << connect.features << " < peer "
                        << reply.features << " missing "
                        << (reply.features & ~policy.features_supported)
                        << std::dec << dendl;
    goto fail;

  case MSGR_MSGR_TAG_BADPROTOVER:
    ldout(async_msgr->cct, 0) << __func__ << " connect protocol version mismatch, my "
                        << connect.protocol_version << " != " << reply.protocol_version
                        << dendl;
    goto fail;

  case MSGR_MSGR_TAG_BADAUTHORIZER:
    ldout(async_msgr->cct,0) << __func__ << " connect got BADAUTHORIZER" << dendl;
    if (got_bad_auth)
      goto fail;
    got_bad_auth = true;
    delete authorizer;
    authorizer = async_msgr->get_authorizer(peer_type, true);  // try harder
    state = STATE_CONNECTING_SEND_CONNECT_MSG;
    return 0;

  case MSGR_MSGR_TAG_RESETSESSION:
    ldout(async_msgr->cct, 0) << __func__ << "connect got RESETSESSION" << dendl;
    was_session_reset();
    state = STATE_CONNECTING_SEND_CONNECT_MSG;
    return 0;

  case MSGR_MSGR_TAG_RETRY_GLOBAL:
    global_seq = async_msgr->get_global_seq(reply.global_seq);
    ldout(async_msgr->cct, 10) << __func__ << " connect got RETRY_GLOBAL "
                         << reply.global_seq << " chose new "
                         << global_seq << dendl;
    state = STATE_CONNECTING_SEND_CONNECT_MSG;
    return 0;

  case MSGR_MSGR_TAG_RETRY_SESSION:
    assert(reply.connect_seq > connect_seq);
    connect_seq = reply.connect_seq;
    ldout(async_msgr->cct, 10) << __func__ << " connect got RETRY_SESSION "
                         << connect_seq << " -> "
                         << reply.connect_seq << dendl;
    state = STATE_CONNECTING_SEND_CONNECT_MSG;
    return 0;

  case MSGR_MSGR_TAG_WAIT:
    ldout(async_msgr->cct, 3) << __func__ << " connect got WAIT (connection race)" << dendl;
    state = STATE_WAIT;
    return 0;

  case MSGR_MSGR_TAG_SEQ:
    ldout(async_msgr->cct, 10) << __func__ << " got MSGR_MSGR_TAG_SEQ, reading acked_seq and writing in_seq" << dendl;
    feat_missing = policy.features_required & ~(uint64_t)connect_reply.features;
    if (feat_missing) {
      ldout(async_msgr->cct, 1) << __func__ << " missing required features " << std::hex
          << feat_missing << std::dec << dendl;
      goto fail;
    }
    state = STATE_CONNECTING_WAIT_ACK_SEQ;
    return 0;

  case MSGR_MSGR_TAG_READY:
    ldout(async_msgr->cct, 10) << __func__ << " got MSGR_MSGR_TAG_READY " << dendl;
    feat_missing = policy.features_required & ~(uint64_t)connect_reply.features;
    if (feat_missing) {
      ldout(async_msgr->cct, 1) << __func__ << " missing required features " << std::hex
          << feat_missing << std::dec << dendl;
      goto fail;
    }
    state = STATE_CONNECTING_READY;
    return 0;

  default:
    ldout(async_msgr->cct, 0) << __func__ << " unknown reply.tag: " << reply.tag << dendl;
    goto fail;
  }

  return 0;

 fail:
  return -1;
}

int AsyncConnection::_reply_accept(char tag, msgr_msg_connect &connect,
  msgr_msg_connect_reply &reply, bufferlist authorizer_reply)
{
  bufferlist reply_bl;
  reply.tag = tag;
  reply.features = ((uint64_t)connect.features & policy.features_supported) | policy.features_required;
  reply.authorizer_len = authorizer_reply.length();
  reply_bl.append((char*)&reply, sizeof(reply));
  if (reply.authorizer_len) {
    reply_bl.append(authorizer_reply.c_str(), authorizer_reply.length());
  }
  int r = _try_send(reply_bl);
  if (r < 0)
    return -1;

  state = STATE_ACCEPTING_WAIT_CONNECT_MSG;
  return 0;
}

int AsyncConnection::handle_connect_msg(msgr_msg_connect &connect, bufferlist &authorizer_bl,
                                        bufferlist &authorizer_reply)
{
  int r = 0;
  msgr_msg_connect_reply reply;
  bufferlist reply_bl;
  bool reset_from_peer = false;
  bool reset_session = false;

  memset(&reply, 0, sizeof(reply));
  reply.protocol_version = async_msgr->get_proto_version(peer_type, false);

  // mismatch?
  ldout(async_msgr->cct, 10) << __func__ << " accept my proto " << reply.protocol_version
                      << ", their proto " << connect.protocol_version << dendl;
  if (connect.protocol_version != reply.protocol_version) {
    return _reply_accept(MSGR_MSGR_TAG_BADPROTOVER, connect, reply, authorizer_reply);
  }
  // require signatures for msgrx?
  if (connect.authorizer_protocol == MSGR_AUTH_MSGRX) {
    if (peer_type == MSGR_ENTITY_TYPE_OSD ||
        peer_type == MSGR_ENTITY_TYPE_MDS) {
      if (async_msgr->cct->conf_->msgrx_require_signatures ||
          async_msgr->cct->conf_->msgrx_cluster_require_signatures) {
        ldout(async_msgr->cct, 10) << __func__ << " using msgrx, requiring MSG_AUTH feature bit for cluster" << dendl;
        policy.features_required |= MSGR_FEATURE_MSG_AUTH;
      }
    } else {
      if (async_msgr->cct->conf_->msgrx_require_signatures ||
          async_msgr->cct->conf_->msgrx_service_require_signatures) {
        ldout(async_msgr->cct, 10) << __func__ << " using msgrx, requiring MSG_AUTH feature bit for service" << dendl;
        policy.features_required |= MSGR_FEATURE_MSG_AUTH;
      }
    }
  }
  uint64_t feat_missing = policy.features_required & ~(uint64_t)connect.features;
  if (feat_missing) {
    ldout(async_msgr->cct, 1) << __func__ << " peer missing required features "
                        << std::hex << feat_missing << std::dec << dendl;
    return _reply_accept(MSGR_MSGR_TAG_FEATURES, connect, reply, authorizer_reply);
  }

  bool authorizer_valid;
  if (!async_msgr->verify_authorizer(this, peer_type, connect.authorizer_protocol, authorizer_bl,
                               authorizer_reply, authorizer_valid, session_key) || !authorizer_valid) {
    ldout(async_msgr->cct,0) << __func__ << ": got bad authorizer" << dendl;
    session_security.reset();
    return _reply_accept(MSGR_MSGR_TAG_BADAUTHORIZER, connect, reply, authorizer_reply);
  }

  // We've verified the authorizer for this AsyncConnection, so set up the session security structure.  PLR
  ldout(async_msgr->cct, 10) << __func__ << " accept setting up session_security." << dendl;

  // existing?
  lock.Unlock();
  async_msgr->lock.Lock();
  lock.Lock();

  AsyncConnectionRef existing = async_msgr->_lookup_conn(peer_addr);

  if (async_msgr->cct->conf_->ms_inject_internal_delays) {
    ldout(msgr->cct, 10) << __func__ << " sleep for "
                         << async_msgr->cct->conf_->ms_inject_internal_delays << dendl;
    utime_t t;
    t.set_from_double(async_msgr->cct->conf_->ms_inject_internal_delays);
    t.sleep();
  }

  if (existing == this)
    existing = NULL;
  if (existing) {
    existing->lock.Lock(); // existing->lock.Lock(true);
    if (connect.global_seq < existing->peer_global_seq) {
      ldout(async_msgr->cct, 10) << __func__ << " accept existing " << existing
                           << ".gseq " << existing->peer_global_seq << " > "
                           << connect.global_seq << ", RETRY_GLOBAL" << dendl;
      reply.global_seq = existing->peer_global_seq;  // so we can send it below..
      existing->lock.Unlock();
      async_msgr->lock.Unlock();
      return _reply_accept(MSGR_MSGR_TAG_RETRY_GLOBAL, connect, reply, authorizer_reply);
    } else {
      ldout(async_msgr->cct, 10) << __func__ << " accept existing " << existing
                           << ".gseq " << existing->peer_global_seq
                           << " <= " << connect.global_seq << ", looks ok" << dendl;
    }

    if (existing->policy.lossy) {
      ldout(async_msgr->cct, 0) << __func__ << " accept replacing existing (lossy) channel (new one lossy="
                          << policy.lossy << ")" << dendl;
      reset_session = true;
      goto replace;
    }

    ldout(async_msgr->cct, 0) << __func__ << " accept connect_seq " << connect.connect_seq
                              << " vs existing csq=" << existing->connect_seq << " state="
                              << get_state_name(existing->state) << dendl;

    if (connect.connect_seq == 0 && existing->connect_seq > 0) {
      ldout(async_msgr->cct,0) << __func__ << " accept peer reset, then tried to connect to us, replacing" << dendl;
      // this is a hard reset from peer
      reset_from_peer = true;
      if (policy.resetcheck)
        reset_session = true;
      goto replace;
    }

    if (connect.connect_seq < existing->connect_seq) {
      // old attempt, or we sent READY but they didn't get it.
      ldout(async_msgr->cct, 10) << __func__ << " accept existing " << existing << ".cseq "
                           << existing->connect_seq << " > " << connect.connect_seq
                           << ", RETRY_SESSION" << dendl;
      reply.connect_seq = existing->connect_seq + 1;
      existing->lock.Unlock();
      async_msgr->lock.Unlock();
      return _reply_accept(MSGR_MSGR_TAG_RETRY_SESSION, connect, reply, authorizer_reply);
    }

    if (connect.connect_seq == existing->connect_seq) {
      // if the existing connection successfully opened, and/or
      // subsequently went to standby, then the peer should bump
      // their connect_seq and retry: this is not a connection race
      // we need to resolve here.
      if (existing->state == STATE_OPEN ||
          existing->state == STATE_STANDBY) {
        ldout(async_msgr->cct, 10) << __func__ << " accept connection race, existing " << existing
                             << ".cseq " << existing->connect_seq << " == "
                             << connect.connect_seq << ", OPEN|STANDBY, RETRY_SESSION" << dendl;
        reply.connect_seq = existing->connect_seq + 1;
        existing->lock.Unlock();
        async_msgr->lock.Unlock();
        return _reply_accept(MSGR_MSGR_TAG_RETRY_SESSION, connect, reply, authorizer_reply);
      }

      // connection race?
      if (peer_addr < async_msgr->get_myaddr() || existing->policy.server) {
        // incoming wins
        ldout(async_msgr->cct, 10) << __func__ << " accept connection race, existing " << existing
                             << ".cseq " << existing->connect_seq << " == " << connect.connect_seq
                             << ", or we are server, replacing my attempt" << dendl;
        goto replace;
      } else {
        // our existing outgoing wins
        ldout(async_msgr->cct, 10) << __func__ << "accept connection race, existing "
                            << existing << ".cseq " << existing->connect_seq
                            << " == " << connect.connect_seq << ", sending WAIT" << dendl;
        assert(peer_addr > async_msgr->get_myaddr());
        // make sure our outgoing connection will follow through
        existing->lock.Unlock();
        async_msgr->lock.Unlock();
        existing->send_keepalive();
        return _reply_accept(MSGR_MSGR_TAG_WAIT, connect, reply, authorizer_reply);
      }
    }

    assert(connect.connect_seq > existing->connect_seq);
    assert(connect.global_seq >= existing->peer_global_seq);
    if (policy.resetcheck &&   // RESETSESSION only used by servers; peers do not reset each other
        existing->connect_seq == 0) {
      ldout(async_msgr->cct, 0) << __func__ << " accept we reset (peer sent cseq "
                          << connect.connect_seq << ", " << existing << ".cseq = "
                          << existing->connect_seq << "), sending RESETSESSION" << dendl;
      existing->lock.Unlock();
      async_msgr->lock.Unlock();
      return _reply_accept(MSGR_MSGR_TAG_RESETSESSION, connect, reply, authorizer_reply);
    }

    // reconnect
    ldout(async_msgr->cct, 10) << __func__ << " accept peer sent cseq " << connect.connect_seq
                         << " > " << existing->connect_seq << dendl;
    goto replace;
  } // !existing
  else if (!replacing && connect.connect_seq > 0) {
    // for a new session, connect_seq must start from 0, otherwise we reset them.
    ldout(async_msgr->cct, 0) << __func__ << " accept we reset (peer sent cseq "
                        << connect.connect_seq << "), sending RESETSESSION" << dendl;
    async_msgr->lock.Unlock();
    return _reply_accept(MSGR_MSGR_TAG_RESETSESSION, connect, reply, authorizer_reply);
  } else {
    // new session
    ldout(async_msgr->cct, 10) << __func__ << " accept new session" << dendl;
    goto open;
  }
  assert(0);

 replace:
  assert(existing->lock.is_locked_by_me());                                      
  assert(lock.is_locked_by_me());    
  assert(async_msgr->lock.is_locked_by_me());

  ldout(async_msgr->cct, 10) << __func__ << " accept replacing " << existing << dendl;

  // There is no possible that existing connection will acquire this lock
  // existing->lock.Lock();

  if (existing->replacing || existing->state == STATE_CLOSED ||
      existing->stopping.read() || existing->state == STATE_OPEN_TAG_CLOSE) {
    ldout(async_msgr->cct, 10) << __func__ << " existing racing replace or mark_down happened while replacing."
                              << " state=" << get_state_name(existing->state) << dendl;
    reply.connect_seq = connect.connect_seq + 1;
    existing->lock.Unlock();
    async_msgr->lock.Unlock();
    r = _reply_accept(MSGR_MSGR_TAG_RETRY_SESSION, connect, reply, authorizer_reply);
    if (r < 0)
      goto fail;
    return 0;
  }

  existing->replacing = 1;  // stage 1
  existing->replacing_reset_session = reset_session;
  existing->replacing_sd = sd;
  this->sd = -1; //hand off
  existing->is_reset_from_peer = reset_from_peer;
  existing->replacing_connect = connect;
  existing->replacing_authorizer_reply = authorizer_reply;
  existing->replacing_buf.resize(recv_end - recv_start);
  if (recv_end > recv_start)
    memcpy(&existing->replacing_buf[0], recv_buf, recv_end - recv_start);

  existing->schedule_timer(existing->ev_replace, 0);

  this->_stop(); //stop myself

  existing->lock.Unlock();
  async_msgr->lock.Unlock();
  return 0;

 open:

  assert(lock.is_locked_by_me());
  assert(async_msgr->lock.is_locked_by_me());

  connect_seq = connect.connect_seq + 1;
  peer_global_seq = connect.global_seq;
  ldout(async_msgr->cct, 10) << __func__ << " accept success, connect_seq = "
                             << connect_seq << " in_seq=" << in_seq << ", sending READY" << dendl;

  int next_state;

  if (replacing && !is_reset_from_peer) {
    reply.tag = MSGR_MSGR_TAG_SEQ;
    next_state = STATE_ACCEPTING_WAIT_SEQ;
  } else {
    // if it is a hard reset from peer, we don't need a round-trip to negotiate in/out sequence
    reply.tag = MSGR_MSGR_TAG_READY;
    next_state = STATE_ACCEPTING_READY;
    discard_requeued_up_to(0);
    is_reset_from_peer = false;
    in_seq = 0;
    in_seq_acked = 0;
  }

  // send READY reply
  reply.features = policy.features_supported;
  reply.global_seq = async_msgr->get_global_seq();
  reply.connect_seq = connect_seq;
  reply.flags = 0;
  reply.authorizer_len = authorizer_reply.length();
  if (policy.lossy)
    reply.flags = reply.flags | MSGR_MSG_CONNECT_LOSSY;

  set_features((uint64_t)reply.features & (uint64_t)connect.features);
  ldout(async_msgr->cct, 10) << __func__ << " accept features " << get_features() << dendl;

#if 0
  session_security.reset(
      get_auth_session_handler(async_msgr->cct, connect.authorizer_protocol,
                               session_key, get_features()));
#endif

  reply_bl.append((char*)&reply, sizeof(reply));

  if (reply.authorizer_len)
    reply_bl.append(authorizer_reply.c_str(), authorizer_reply.length());

  if (reply.tag == MSGR_MSGR_TAG_SEQ) {
    msgr_le64 seq;
    seq = in_seq;
    reply_bl.append((char*)&seq, sizeof(seq));
  }

  lock.Unlock();

  // Because "replacing" will prevent other connections preempt this addr,
  // it's safe that here we don't acquire Connection's lock
  r = async_msgr->_accept_conn(this);

  async_msgr->lock.Unlock();

  lock.Lock();
  if (r < 0) {
    ldout(async_msgr->cct, 0) << __func__ << " existing race replacing process for addr=" << peer_addr
                              << " just fail later one(this)" << replacing << dendl;
    goto fail_registered;
  }

  // notify
  if ((!replacing || replacing == 2) && ! queue_reset) {
     queue_reset = true;
     Mutex::Unlocker unlocker(lock);
     in_q->queue_accept(this);
     async_msgr->ms_deliver_handle_fast_accept(this);
     // XXX check stopping.read()
  }

  r = _try_send(reply_bl);
  if (r < 0)
    goto fail_registered;

  if (r == 0) {
    state = next_state;
    ldout(async_msgr->cct, 2) << __func__ << " accept write reply msg done" << dendl;
  } else {
    state = STATE_WAIT_SEND;
    state_after_send = next_state;
  }
  replacing = 0;

  assert(lock.is_locked_by_me());
  assert(!async_msgr->lock.is_locked_by_me());

  return 0;

 fail_registered:
  ldout(async_msgr->cct, 10) << __func__ << " accept fault after register" << dendl;

  if (async_msgr->cct->conf_->ms_inject_internal_delays) {
    ldout(async_msgr->cct, 10) << __func__ << " sleep for "
                               << async_msgr->cct->conf_->ms_inject_internal_delays
                               << dendl;
    utime_t t;
    t.set_from_double(async_msgr->cct->conf_->ms_inject_internal_delays);
    t.sleep();
  }

 fail:
  ldout(async_msgr->cct, 10) << __func__ << " failed to accept." << dendl;

  replacing = false;

  assert(lock.is_locked_by_me());
  assert(!async_msgr->lock.is_locked_by_me());

  return -1;
}

void AsyncConnection::handle_replace()
{
  ev_replace.stop();
  Mutex::Unlocker ul(center->lock);

  Mutex::Locker l1(lock);

  if (replacing == 1)
    do_replace();
}

void AsyncConnection::do_replace()
{
  assert(lock.is_locked_by_me());

  msgr_assert(replacing == 1, ("do_replace with replacing=%d", replacing));

  msgr_msg_connect_reply reply;

  release_throttle(state);
 
  if (replacing_reset_session) {
    replacing_reset_session = false;
    was_session_reset();
  }

  {
    Mutex::Locker l2(center->lock);

    ev_read.stop();
    ev_write.stop();
    close_socket(sd);

    assert(replacing_sd >= 0);

    sd = replacing_sd;
    replacing_sd = -1;
    ev_read.set(sd, ev::READ);
    ev_read.start();
    ev_write.set(sd, ev::WRITE);
  }

  handle_write_delayed = false;
  recv_end = recv_start = 0;
  state_offset = 0;
  memcpy(recv_buf, &replacing_buf[0], replacing_buf.size());
  recv_end = replacing_buf.size();
  replacing_buf.resize(0);
  if (recv_end != recv_start) // have data to process
    schedule_timer(ev_process, 0);

  // Clean up output buffer
  outcoming_bl.clear();
  requeue_sent();

  replacing = 2; //enter stage 2

  // To simplify things, we let peer retry. This is a bit lazy.
  state = STATE_ACCEPTING_WAIT_CONNECT_MSG;

  reply.connect_seq = replacing_connect.connect_seq + 1;
  if (_reply_accept(MSGR_MSGR_TAG_RETRY_SESSION, replacing_connect, reply,
    replacing_authorizer_reply) < 0) {
    replacing = 0;
    fault();
  }
  replacing_authorizer_reply.clear();
}

void AsyncConnection::_connect()
{
  ldout(async_msgr->cct, 10) << __func__ << " csq=" << connect_seq << dendl;

  assert(lock.is_locked_by_me());
  state = STATE_CONNECTING;
  schedule_timer(ev_process, 0);
}

/* Called by acceptor */
void AsyncConnection::accept(int incoming)
{
  ldout(async_msgr->cct, 10) << __func__ << " sd=" << incoming << dendl;
  assert(sd < 0);

  Mutex::Locker locker1(lock);
  Mutex::Locker locker2(center->lock);

  sd = incoming;
  state = STATE_ACCEPTING;
  ev_read.set(sd, ev::READ);
  ev_read.start();
  ev_write.set(sd, ev::WRITE); // Not started
  schedule_timer_locked(ev_process, 0);
}

int AsyncConnection::send_message(Message *m)
{
  ldout(async_msgr->cct, 10) << __func__ << dendl;

  m->get_header().src = async_msgr->get_myname();
  if (!m->get_priority())
    m->set_priority(async_msgr->get_default_send_priority());

  Mutex::Locker l(lock);
  if (stopping.read()) {
    /* This guards the mark_down() request in current thread */
    m->put();
    ldout(async_msgr->cct, 10) << __func__ << " connection closed."
                                 << " Drop message " << m << dendl;
  } else if (!is_queued() && !sending && state >= STATE_OPEN && state < STATE_OPEN_TAG_CLOSE
        && (async_msgr->cct->conf_->ms_concurrent_socket ||
        state != STATE_OPEN_MESSAGE_READ_DATA)) {
    // directly send data in current thread
    ldout(async_msgr->cct, 10) << __func__ << " try send msg " << m << dendl;
    sending = true;
    int r = _send(m);
    sending = false;
    if (want_send)  {
      want_send = false;
      cond.Signal();
    }
    if (r < 0 || handle_write_delayed) {
      if (r < 0)
        ldout(async_msgr->cct, 1) << __func__ << " send msg failed" << dendl;
      handle_write_delayed = false;
      // we want to handle fault within internal thread
      start_write();
    }
  } else if (state == STATE_CLOSED) {
    ldout(async_msgr->cct, 10) << __func__ << " connection closed."
                                 << " Drop message " << m << dendl;
    m->put();
  } else {
    out_q[m->get_priority()].push_back(m);
    if (!sending) {
      if (state == STATE_STANDBY && !policy.server) {
        ldout(async_msgr->cct, 10) << __func__ << " state is " << get_state_name(state)
                                 << " policy.server is false" << dendl;
        _connect();
      } else {
        ldout(async_msgr->cct, 10) << __func__ << " state is " << get_state_name(state)
            << "start_write" << dendl;
        start_write();
      }
    }
  }
  return 0;
}

void AsyncConnection::requeue_sent()
{
  if (sent.empty())
    return;

  deque<Message*>& rq = out_q[MSGR_MSG_PRIO_HIGHEST];
  while (!sent.empty()) {
    Message *m = sent.back();
    sent.pop_back();
    ldout(async_msgr->cct, 10) << __func__ << " " << *m << " for resend seq " << out_seq
                         << " (" << m->get_seq() << ")" << dendl;
    rq.push_front(m);
    out_seq--;
  }
}

void AsyncConnection::discard_requeued_up_to(uint64_t seq)
{
  ldout(async_msgr->cct, 10) << __func__ << " " << seq << dendl;
  if (out_q.count(MSGR_MSG_PRIO_HIGHEST) == 0)
    return;
  deque<Message*>& rq = out_q[MSGR_MSG_PRIO_HIGHEST];
  while (!rq.empty()) {
    Message *m = rq.front();
    if (m->get_seq() == 0 || m->get_seq() > seq)
      break;
    ldout(async_msgr->cct, 10) << __func__ << " " << *m << " for resend seq " << out_seq+1
                         << " <= " << seq << ", discarding" << dendl;
    m->put();
    rq.pop_front();
    out_seq++;
  }
  if (rq.empty())
    out_q.erase(MSGR_MSG_PRIO_HIGHEST);
}

/*
 * Tears down the AsyncConnection's message queues, and removes them from the DispatchQueue
 * Must hold pipe_lock prior to calling.
 */
void AsyncConnection::discard_out_queue()
{
  ldout(async_msgr->cct, 10) << __func__ << " started" << dendl;

  for (deque<Message*>::iterator p = sent.begin(); p != sent.end(); ++p) {
    ldout(async_msgr->cct, 20) << __func__ << " discard " << *p << dendl;
    (*p)->put();
  }
  sent.clear();
  for (map<int,deque<Message*> >::iterator p = out_q.begin(); p != out_q.end(); ++p)
    for (deque<Message*>::iterator r = p->second.begin(); r != p->second.end(); ++r) {
      ldout(async_msgr->cct, 20) << __func__ << " discard " << *r << dendl;
      (*r)->put();
    }
  out_q.clear();
  outcoming_bl.clear();
}

int AsyncConnection::randomize_out_seq()
{
  if (get_features() & MSGR_FEATURE_MSG_AUTH) {
    // Set out_seq to a random value, so CRC won't be predictable.   Don't bother checking seq_error
    // here.  We'll check it on the call.  PLR
    int seq_error = get_random_bytes((char *)&out_seq, sizeof(out_seq));
    out_seq &= SEQ_MASK;
    lsubdout(async_msgr->cct, ms, 10) << __func__ << " randomize_out_seq " << out_seq << dendl;
    return seq_error;
  } else {
    // previously, seq #'s always started at 0.
    out_seq = 0;
    return 0;
  }
}

void AsyncConnection::release_throttle(int prev_state)
{
  if (prev_state > STATE_OPEN_MESSAGE_THROTTLE_MESSAGE &&
      prev_state <= STATE_OPEN_MESSAGE_DISPATCH
      && policy.throttler_messages) {
    ldout(async_msgr->cct,10) << __func__ << " releasing " << 1
                          << " message to policy throttler "
                          << policy.throttler_messages->get_current() << "/"
                          << policy.throttler_messages->get_max() << dendl;
    policy.throttler_messages->put();
  }

  if (prev_state > STATE_OPEN_MESSAGE_THROTTLE_BYTES &&
      prev_state <= STATE_OPEN_MESSAGE_DISPATCH) {
    uint64_t message_size = current_header.front_len + current_header.middle_len + current_header.data_len;
    if (policy.throttler_bytes) {
      ldout(async_msgr->cct,10) << __func__ << " releasing " << message_size
                            << " bytes to policy throttler "
                            << policy.throttler_bytes->get_current() << "/"
                            << policy.throttler_bytes->get_max() << dendl;
      policy.throttler_bytes->put(message_size);
    }
  }
}

void AsyncConnection::fault()
{
  assert(lock.is_locked_by_me());

  if (state == STATE_CLOSED) {
    ldout(async_msgr->cct, 10) << __func__ << " state is already " << get_state_name(state) << dendl;
    return ;
  }

  if (policy.lossy && !(state >= STATE_CONNECTING && state < STATE_CONNECTING_READY)) {
    ldout(async_msgr->cct, 10) << __func__ << " on lossy channel, failing" << dendl;
    Mutex::Unlocker ul(lock);
    Mutex::Locker l1(async_msgr->lock);
    Mutex::Locker l2(lock);
    _stop();
    return ;
  }

  release_throttle(state);

  {
    Mutex::Locker locker(center->lock);
    close_socket(sd);
    sd = -1;
    ev_read.stop();
    ev_write.stop();
    ev_process.stop();
  }

  msgr_assert(replacing != 1, ("fault with replacing = %d", replacing));

  handle_write_delayed = false;
  replacing = 0;
  // requeue sent items
  requeue_sent();
  recv_start = recv_end = 0;
  state_offset = 0;
  outcoming_bl.clear();
  if (policy.standby && !is_queued()) {
    ldout(async_msgr->cct,0) << __func__ << " with nothing to send, going to standby, nref=" << get_nref() << "queue len:" << async_msgr->dispatch_queue.get_queue_len() << dendl;
    state = STATE_STANDBY;
    return;
  }

  if (!(state >= STATE_CONNECTING && state < STATE_CONNECTING_READY)) {
    // policy maybe empty when state is in accept
    if (policy.server || (state >= STATE_ACCEPTING && state < STATE_ACCEPTING_WAIT_SEQ)) {
      // server mode
      ldout(async_msgr->cct, 0) << __func__ << " server, going to standby, nref=" << get_nref() << "queue len: " << async_msgr->dispatch_queue.get_queue_len() << dendl;
      state = STATE_STANDBY;
    } else {
      // client mode
      ldout(async_msgr->cct, 0) << __func__ << " initiating reconnect" << dendl;
      connect_seq++;
      state = STATE_CONNECTING;
    }
    backoff = utime_t();
  } else {
    if (backoff == utime_t()) {
      backoff.set_from_double(async_msgr->cct->conf_->ms_initial_backoff);
    } else {
      backoff += backoff;
      if (backoff > async_msgr->cct->conf_->ms_max_backoff)
        backoff.set_from_double(async_msgr->cct->conf_->ms_max_backoff);
    }
    state = STATE_CONNECTING;
    ldout(async_msgr->cct, 10) << __func__ << " waiting " << backoff << "peer " << peer_addr << dendl;
  }

  // wake up later
  if (state == STATE_CONNECTING) {
    schedule_timer(ev_process, double(backoff.to_nsec()) / 1000000000);
  }
}

void AsyncConnection::was_session_reset()
{
  assert(lock.is_locked_by_me());
  ldout(async_msgr->cct,10) << __func__ << " started" << dendl;

  in_q->discard_queue(conn_id);
  discard_out_queue();

  in_q->queue_remote_reset(this);

  if (randomize_out_seq()) {
    lsubdout(async_msgr->cct,ms,15) << __func__ << " could not get random bytes to set seq number for session reset; set seq number to " << out_seq << dendl;
  }

  in_seq = 0;
  connect_seq = 0;
  in_seq_acked = 0;
}

void AsyncConnection::_stop()
{
  assert(async_msgr->lock.is_locked_by_me());
  assert(lock.is_locked_by_me());

  if (state == STATE_CLOSED) {
    cond.Signal();
    return;
  }

  ldout(async_msgr->cct, 10) << __func__ << dendl;

  failed = true;

  if (async_msgr->cct->conf_->ms_inject_internal_delays) {
    ldout(msgr->cct, 10) << __func__ << " sleep for "
                         << async_msgr->cct->conf_->ms_inject_internal_delays
                         << dendl;
    utime_t t;
    t.set_from_double(async_msgr->cct->conf_->ms_inject_internal_delays);
    t.sleep();
  }

  release_throttle(state);

  async_msgr->_unregister_conn(this);

  if (replacing_sd != -1) {
    close_socket(replacing_sd);
    replacing_sd = -1;
  }

  center->lock.Lock();
  cleanup_handler();
  center->lock.Unlock();

  in_q->discard_queue(conn_id);
  discard_out_queue();

  state_offset = 0;
  if (sd >= 0) {
    close_socket(sd);
    sd = -1;
  }

  if (queue_reset) {
    in_q->queue_reset(this);
    queue_reset = false;
  }

  replacing = 0;
  state = STATE_CLOSED;
  cond.Signal();

  // schedule an event to detach me from event center
  center->dispatch_event_external(EventCallbackRef(new C_release(this)));
}

int AsyncConnection::_send(Message *m)
{
  m->set_seq(++out_seq);
  if (!policy.lossy) {
    // put on sent list
    sent.push_back(m); 
    m->get();
  }

  // associate message with Connection (for benefit of encode_payload)
  m->set_connection(this);

  uint64_t features = get_features();
  if (m->empty_payload())
    ldout(async_msgr->cct, 20) << __func__ << " encoding " << m->get_seq() << " features " << features
                         << " " << m << " " << *m << dendl;
  else
    ldout(async_msgr->cct, 20) << __func__ << " half-reencoding " << m->get_seq() << " features "
                         << features << " " << m << " " << *m << dendl;

  // encode and copy out of *m
  m->encode(features, async_msgr->crcflags);

  // prepare everything
  msgr_msg_header& header = m->get_header();
  msgr_msg_footer& footer = m->get_footer();

#if 0
  // Now that we have all the crcs calculated, handle the
  // digital signature for the message, if the AsyncConnection has session
  // security set up.  Some session security options do not
  // actually calculate and check the signature, but they should
  // handle the calls to sign_message and check_signature.  PLR
  if (session_security.get() == NULL) {
    ldout(async_msgr->cct, 20) << __func__ << " no session security" << dendl;
  } else {
    if (session_security->sign_message(m)) {
      ldout(async_msgr->cct, 20) << __func__ << " failed to sign seq # "
                           << header.seq << "): sig = " << footer.sig << dendl;
    } else {
      ldout(async_msgr->cct, 20) << __func__ << " signed seq # " << header.seq
                           << "): sig = " << footer.sig << dendl;
    }
  }
#endif

  bufferlist blist = m->get_payload();
  blist.append(m->get_middle());
  blist.append(m->get_data());

  ldout(async_msgr->cct, 20) << __func__ << " sending " << m->get_seq()
                       << " " << m << dendl;
  int rc = write_message(header, footer, blist);

  if (rc < 0) {
    int err = errno;
    ldout(async_msgr->cct, 1) << __func__ << " error sending " << m << ", "
                        << cpp_strerror(err) << dendl;
  } else if (rc == 0) {
    ldout(async_msgr->cct, 10) << __func__ << " sending " << m << " done." << dendl;
  } else {
    ldout(async_msgr->cct, 10) << __func__ << " sending " << m << " continuely." << dendl;
  }
  m->put();

  return rc;
}

int AsyncConnection::write_message(msgr_msg_header& header, msgr_msg_footer& footer,
                                  bufferlist& blist)
{
  int ret;

  bufferptr bp = buffer::create_malloc(1 + sizeof(header));

  // send tag
  char tag = MSGR_MSGR_TAG_MSG;
  bp.copy_in(0, 1, &tag);

  // send envelope
  bp.copy_in(1, sizeof(header), (const char *)&header);

  blist.push_front(bp);

  // send footer; if receiver doesn't support signatures, use the old footer format
  blist.append((char*)&footer, sizeof(footer));

  // send
  ret = _try_send(blist, true, true);
  if (ret < 0)
    return ret;

  return ret;
}

void AsyncConnection::handle_ack(uint64_t seq)
{
  assert(lock.is_locked_by_me());
  lsubdout(async_msgr->cct, ms, 15) << __func__ << " got ack seq " << seq << dendl;
  // trim sent list
  while (!sent.empty() && sent.front()->get_seq() <= seq) {
    Message *m = sent.front();
    sent.pop_front();
    lsubdout(async_msgr->cct, ms, 10) << __func__ << "reader got ack seq "
                                << seq << " >= " << m->get_seq() << " on "
                                << m << " " << *m << dendl;
    m->put();
  }
}

void AsyncConnection::start_write(bool feed)
{
  assert(lock.is_locked_by_me());
  assert(!center->lock.is_locked_by_me());
  Mutex::Locker locker(center->lock);
  start_write_locked(feed);
}

void AsyncConnection::start_write_locked(bool feed)
{
  assert(center->lock.is_locked_by_me());
  assert(lock.is_locked_by_me());
  if (sd >= 0) {
    ev_write.start();
    if (feed)
      ev_write.feed_event(ev::WRITE);
    center->maybe_wakeup();
  }
}

void AsyncConnection::send_keepalive()
{
  ldout(async_msgr->cct, 10) << __func__ << " started." << dendl;
  Mutex::Locker l(lock);
  if (state != STATE_CLOSED) {
    keepalive = true;
    start_write();
  }
}

void AsyncConnection::mark_down()
{
  ldout(async_msgr->cct, 10) << __func__ << " started." << dendl;

  async_msgr->lock.Lock();
  // Only need msgr lock, see connection replacing code.
  // mark_down is exclusive with connection replacing.
  if (pthread_self() == center->get_owner()) {
    if (stopping.read()) {
        async_msgr->lock.Unlock();
        return;
    }
    stopping.set(1);
    lock.Lock();
    this->queue_reset = false;
    lock.Unlock();
    async_msgr->lock.Unlock();
    center->dispatch_event_external(EventCallbackRef(new C_stop(this)));
  } else {
    stopping.set(1);
    lock.Lock();
    async_msgr->lock.Unlock();
    while(state != STATE_CLOSED) {
      this->queue_reset = false;
      center->dispatch_event_external(EventCallbackRef(new C_stop(this)));
      cond.Wait(lock);
    }
    lock.Unlock();
  }
}

void AsyncConnection::stop()
{
  ldout(async_msgr->cct, 10) << __func__ << " started." << dendl;

  //Mutex::Locker locker1(async_msgr->lock);
  Mutex::Locker locker2(lock);
  stopping.set(1);
  if (state != STATE_CLOSED)
    center->dispatch_event_external(EventCallbackRef(new C_stop(this)));
}

void AsyncConnection::mark_disposable()
{
  Mutex::Locker locker1(async_msgr->lock);
  Mutex::Locker locker2(lock);
  policy.lossy = true;
  if (state == STATE_STANDBY)
    _stop();
}

int AsyncConnection::_send_keepalive_or_ack(bool ack, utime_t *tp)
{
  assert(lock.is_locked_by_me());
  bufferlist bl;

  struct msgr_timespec ts;
  if (ack) {
    assert(tp);
    tp->encode_timeval(&ts);
    bl.append(MSGR_MSGR_TAG_KEEPALIVE2_ACK);
    bl.append((char*)&ts, sizeof(ts));
  } else {
    utime_t t = msgr_clock_now(async_msgr->cct);
    t.encode_timeval(&ts);
    bl.append(MSGR_MSGR_TAG_KEEPALIVE2);
    bl.append((char*)&ts, sizeof(ts));
  }

  ldout(async_msgr->cct, 10) << __func__ << " try send keepalive or ack" << dendl;
  return _try_send(bl, false);
}

void AsyncConnection::kick_write()
{
  bool enable = false;

  assert(lock.is_locked_by_me());
  assert(center->lock.is_locked_by_me());

  if (state >= STATE_OPEN && state <= STATE_OPEN_TAG_CLOSE) {
    if (!out_q.empty() || keepalive || keepalive_ack)
      enable = true;
  }

  if (outcoming_bl.length() && state != STATE_CONNECTING &&
      state != STATE_STANDBY && state != STATE_WAIT && state != STATE_CLOSED)
    enable = true;
  
  if (enable)
    start_write_locked();
}

void AsyncConnection::handle_write()
{
  ldout(async_msgr->cct, 10) << __func__ << " started." << dendl;

  center->lock.Unlock();
  
  Mutex::Locker locker(lock);

  if (sending) {
    handle_write_delayed = true;
    center->lock.Lock();
    ev_write.stop();
    return;
  }

  sending = true;

  bufferlist bl;
  int r = 0;
  if (state >= STATE_OPEN && state <= STATE_OPEN_TAG_CLOSE) {
    // deliver queued data in open state
    if (keepalive) {
      _send_keepalive_or_ack();
      keepalive = false;
    }

    if (keepalive_ack) {
      _send_keepalive_or_ack(true, &keepalive_ack_stamp);
      keepalive_ack = false;
    }

    while (!stopping.read()) {
      Message *m = _get_next_outgoing();
      if (!m)
        break;

      ldout(async_msgr->cct, 10) << __func__ << " try send msg " << m << dendl;
      r = _send(m);
      if (r < 0) {
        ldout(async_msgr->cct, 1) << __func__ << " send msg failed" << dendl;
        goto fail;
      } else if (r > 0) {
        break;
      }
    }

    if (in_seq > in_seq_acked) {
      msgr_le64 s;
      s = in_seq;
      bl.append(MSGR_MSGR_TAG_ACK);
      bl.append((char*)&s, sizeof(s));
      ldout(async_msgr->cct, 10) << __func__ << " try send msg ack" << dendl;
      in_seq_acked = in_seq;
      r = _try_send(bl, true, true);
    } else {
      // flush output buffer
      r = _try_send(bl, true, true);
    }

    if (r < 0) {
      ldout(async_msgr->cct, 1) << __func__ << " send msg failed" << dendl;
      goto fail;
    }
  } else if (state != STATE_CONNECTING && state != STATE_STANDBY
     && state != STATE_WAIT) {
    // only flush output buffer
    r = _try_send(bl, true, true);
    if (r < 0) {
      ldout(async_msgr->cct, 1) << __func__ << " send outcoming bl failed" << dendl;
      goto fail;
    }
    if (state == STATE_WAIT_SEND)
        do_process();
  } else {
    sending = false;
    center->lock.Lock();
    ev_write.stop();
    ldout(async_msgr->cct, 1) << __func__ << " handle_write in CONNECTING state, stop ev_write " << dendl;
    return;
  }
  
  sending = false;
  center->lock.Lock();
  kick_write();
  return;

 fail:
  sending = false;
  if (replacing == 1) {
    do_replace();
    center->lock.Lock();
  } else {
    fault();
    center->lock.Lock();
  }
}

void AsyncConnection::handle_process()
{
    ev_process.stop();
    process();
}

void AsyncConnection::_release()
{
  //cleanup_handler();
  //ldout(async_msgr->cct, 1) << __func__ << this << dendl;

  assert(state == STATE_CLOSED);
  assert(sd == -1);
  // done
  this->put();
}

void AsyncConnection::cleanup_handler()
{
  assert(center->lock.is_locked_by_me());

  ev_process.stop();
  ev_replace.stop();
  ev_read.stop();
  ev_write.stop();
}

void AsyncConnection::schedule_timer(ev::timer &timer, double timeo)
{
  Mutex::Locker locker(center->lock);
  schedule_timer_locked(timer, timeo);
}

void AsyncConnection::schedule_timer_locked(ev::timer &timer, double timeo)
{
  assert(center->lock.is_locked_by_me());
  timer.stop();
  timer.start(timeo);
  center->maybe_wakeup();
}

void AsyncConnection::dump(std::ostream &os)
{
  std::cerr << "-- " << async_msgr->get_myinst().addr << " >> " << peer_addr << " conn(" << this
            << " sd=" << sd << " :" << port
            << " s=" << get_state_name(state)
            << " pgs=" << peer_global_seq
            << " cs=" << connect_seq
            << " l=" << policy.lossy
            << ")."
            << " server:" << policy.server
            << " nref:" << get_nref()
            << " msgr:" << async_msgr
            << " msgr magic:" << async_msgr->magic
            << " msgr stopped:" << !async_msgr->started << '\n'; 
}

} // namespace msgr
