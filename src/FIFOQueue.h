#ifndef FIFOQUEUE_9c21a7c5e3244ea6b81134de81762c29_H
#define FIFOQUEUE_9c21a7c5e3244ea6b81134de81762c29_H

#include <deque>
#include <map>
#include <utility>

#include "msgr/msgr_assert.h"

namespace msgr {
template <typename T, typename K>
class FIFOQueue {
  struct SubQueue {
  private:
    typedef std::list<std::pair<K, T> >  List;
    List q;
  public:
    SubQueue(const SubQueue &other)
      : q(other.q)
      {}
    SubQueue()
      {}
    void enqueue(K cl, T item) {
      q.push_back(std::make_pair(cl, item));
    }
    void enqueue_front(K cl, T item) {
      q.push_front(std::make_pair(cl, item));
    }
    std::pair<K, T> front() const {
      assert(!(q.empty()));
      return q.front();
    }
    void pop_front() {
      assert(!(q.empty()));
      q.pop_front();
    }
    unsigned length() const {
      return q.size();
    }
    bool empty() const {
      return q.empty();
    }

    template <class F>
    void remove_by_filter(F f, std::list<T> *out) {
      if (out) {
        for (typename List::reverse_iterator i = q.rbegin();
            i != q.rend(); ++i) {
          if (f(i->second)) {
            out->push_front(i->second);
          }
        }
      }
      for (typename List::iterator i = q.begin(); i != q.end();) {
        if (f(i->second)) {
          q->erase(i++);
        } else {
          ++i;
        }
      }
    }

    void remove_by_class(K k, std::list<T> *out) {
      if (out) {
        for (typename List::reverse_iterator j = q.rbegin();
            j != q.rend(); ++j) {
          if (j->first == k)
            out->push_front(j->second);
	      }
      }
      for (typename List::iterator i = q.begin(); i != q.end();) {
        if (i->first == k)
          q.erase(i++);
        else
          ++i;
      }
    }
  };

  std::map<unsigned, SubQueue> high_queue;
  typedef std::map<unsigned, SubQueue> Map;
  SubQueue queue;

public:
  FIFOQueue()
  {}

  unsigned length() const {
    unsigned total = 0;
    for (typename Map::const_iterator i = high_queue.begin();
        i != high_queue.end(); ++i) {
      assert(i->second.length());
      total += i->second.length();
    }
    total += queue.length();
    return total;
  }

  template <class F>
  void remove_by_filter(F f, std::list<T> *removed = 0) {
    queue.remove_by_filter(f, removed);
    for (typename Map::iterator i = high_queue.begin();
        i != high_queue.end();) {
      i->second.remove_by_filter(f, removed);
      if (i->second.empty()) {
        high_queue.erase(i++);
      } else {
        ++i;
      }
    }
  }

  void remove_by_class(K k, std::list<T> *out = 0) {
    queue.remove_by_class(k, out);
    for (typename Map::iterator i = high_queue.begin();
        i != high_queue.end();) {
      i->second.remove_by_class(k, out);
      if (i->second.empty()) {
        high_queue.erase(i++);
      } else {
        ++i;
      }
    }
  }

  void enqueue_strict(K cl, unsigned priority, T item) {
    high_queue[priority].enqueue(cl, item);
  }

  void enqueue_strict_front(K cl, unsigned priority, T item) {
    high_queue[priority].enqueue_front(cl, item);
  }

  void enqueue(K cl, unsigned priority, unsigned cost, T item) {
    queue.enqueue(cl, item);
  }

  void enqueue_front(K cl, unsigned priority, unsigned cost, T item) {
    queue.enqueue_front(cl, item);
  }

  bool empty() const {
    return queue.empty() && high_queue.empty();
  }

  T dequeue() {
    assert(!empty());

    if (!(high_queue.empty())) {
      T ret = high_queue.rbegin()->second.front().second;
      high_queue.rbegin()->second.pop_front();
      if (high_queue.rbegin()->second.empty())
        high_queue.erase(high_queue.rbegin()->first);
      return ret;
    }

    T ret = queue.front().second;
    queue.pop_front();
    return ret;
  }
};

}

#endif // FIFOQUEUE_9c21a7c5e3244ea6b81134de81762c29_H
