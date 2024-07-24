// Copyright (c) 2015-present, Qihoo, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#include "net/src/net_multiplexer.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>

#include <glog/logging.h>

#include "pstd/include/xdebug.h"

namespace net {

NetMultiplexer::NetMultiplexer(int queue_limit) : queue_limit_(queue_limit), fired_events_(NET_MAX_CLIENTS) {
  int fds[2];
  if (pipe(fds) != 0) {
    exit(-1);
  }
  notify_receive_fd_ = fds[0];
  notify_send_fd_ = fds[1];

  fcntl(notify_receive_fd_, F_SETFD, fcntl(notify_receive_fd_, F_GETFD) | FD_CLOEXEC);
  fcntl(notify_send_fd_, F_SETFD, fcntl(notify_send_fd_, F_GETFD) | FD_CLOEXEC);

  node_cnt_ = 0;
  newest_node_ = nullptr;
}

NetMultiplexer::~NetMultiplexer() {
  if (multiplexer_ != -1) {
    ::close(multiplexer_);
  }
}

void NetMultiplexer::Initialize() {
  NetAddEvent(notify_receive_fd_, kReadable);
  init_ = true;
}

NetMultiplexer::Node* NetMultiplexer::NotifyQueuePop() {
  if (!init_) {
    LOG(ERROR) << "please call NetMultiplexer::Initialize()";
    std::abort();
  }

  Node *first = nullptr;
  auto last = newest_node_.exchange(nullptr);
  int cnt = 1;
  if(last) {
    first = CreateMissingNewerLinks(last, &cnt);
    node_cnt_ -= cnt;
  }
  return first;
}

bool NetMultiplexer::Register(const NetItem& it, bool force) {
  if (!init_) {
    LOG(ERROR) << "please call NetMultiplexer::Initialize()";
    return false;
  }

  auto cnt = ++node_cnt_;
  if (force || queue_limit_ == kUnlimitedQueue || cnt < static_cast<size_t>(queue_limit_)) {
    auto node = new Node(it);
    LinkOne(node, &newest_node_);
    ssize_t n = write(notify_send_fd_, "", 1);
    return true;
  }
  return false;
}

NetMultiplexer::Node* NetMultiplexer::CreateMissingNewerLinks(Node* head, int* cnt) {
  Node* next = nullptr;
  while (true) {
    next = head->link_older;
    if (next == nullptr) {
      return head;
    }
    ++(*cnt);
    next->link_newer = head;
    head = next;
  }
}

bool NetMultiplexer::LinkOne(Node* node, std::atomic<Node*>* newest_node) {
  auto nodes = newest_node->load(std::memory_order_relaxed);
  while (true) {
    node->link_older = nodes;
    if (newest_node->compare_exchange_weak(nodes, node)) {
      return (nodes == nullptr);
    }
  }
}

}  // namespace net
