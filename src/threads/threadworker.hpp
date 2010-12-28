// Distributed under the BSD License, see accompanying LICENSE.txt
// (C) Copyright 2010 John-John Tedro et al.
#ifndef _THREADWORKER_H_
#define _THREADWORKER_H_
#include <assert.h>

#include <queue>
#include <list>

#include <iostream>

// I admit, this is pretty drastic, but I was desperate
// now I just keep it for the sake of having it available to new
// platforms.
#if !defined(C10T_DISABLE_THREADS)
#include <boost/detail/atomic_count.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <boost/bind.hpp>

class interrupted_exception : public std::exception {};

template<typename T>
class sync_queue {
  private:
    std::queue<T> q;
    boost::mutex mutex;
    boost::condition empty_cond;
    volatile int count;
    volatile bool interrupted;
  public:
    sync_queue() : count(0), interrupted(false) {}

    void add(T o) {
      boost::mutex::scoped_lock lock(mutex);
      q.push(o);
      empty_cond.notify_one();
    }
    
    T take(int& pos) throw(interrupted_exception) {
      boost::mutex::scoped_lock lock(mutex);
      while (!interrupted && q.empty()) {
        empty_cond.wait(lock);
      }
      
      if (interrupted) {
        throw interrupted_exception();
      }
      
      T o = q.front();
      q.pop();
      pos = count++;
      return o;
    }
    
    bool empty() {
      boost::mutex::scoped_lock lock(mutex);
      return q.empty();
    }
    
    void interrupt() {
      boost::mutex::scoped_lock lock(mutex);
      interrupted = true;
      empty_cond.notify_all();
    }
};

template <class I, class O>
class threadworker
{
private:
  const int total;
  
  sync_queue<I> in;
  sync_queue<O> out;
  boost::condition start_cond;
  boost::condition input_cond;
  boost::condition order_cond;
  boost::mutex start_mutex;
  boost::mutex order_mutex;
  
  const int thread_count;
  
  boost::detail::atomic_count input;
  boost::detail::atomic_count output;
  volatile bool started;
  volatile bool interrupted;
  
  boost::scoped_array<boost::thread> threads;
public:
  threadworker(int c, int total) : total(total), thread_count(c),
    input(0), output(0), started(false), interrupted(false),
    threads(new boost::thread[thread_count])
  {
    for (int i = 0; i < thread_count; i++) {
      threads[i] = boost::thread(boost::bind(&threadworker::run, this, i));
    }
  }
  
  virtual ~threadworker() {
    in.interrupt();
    out.interrupt();
    
    interrupted = true;
    
    {
      boost::mutex::scoped_lock lock(order_mutex);
      order_cond.notify_all();
    }
    
    {
      boost::mutex::scoped_lock lock(start_mutex);
      start_cond.notify_all();
    }
    
    for (int i = 0; i < thread_count; i++) {
      threads[i].join();
    }
  }
  
  void give(I t) {
    in.add(t);
  }
  
  void start() {
    boost::mutex::scoped_lock lock(start_mutex);
    started = true;
    start_cond.notify_all();
  }

  void internal_work() throw(interrupted_exception) {
    int qp;
    
    I i = in.take(qp);
    
    O o = work(i);
    
    {
      // first, make sure that we output the results in the same order they came in
      // try to make it as lock-free as possible
      boost::mutex::scoped_lock lock(order_mutex);
      
      while (!interrupted && qp != output) {
        order_cond.wait(lock);
      }
      
      if (interrupted) {
        throw interrupted_exception();
      }
      
      out.add(o);
      ++output;
      order_cond.notify_all();
    }
  }
  
  void run(int id) {
    {
      boost::mutex::scoped_lock lock(start_mutex);
      
      while (!interrupted && !started) {
        start_cond.wait(lock);
      }
    }
    
    if (interrupted) {
      return;
    }
    
    while (++input <= total) {
      try {
        internal_work();
      } catch(interrupted_exception& e) {
        break;
      }
    }
  }

  virtual O work(I) = 0;
  
  O get() {
    int t;
    return out.take(t);
  }
  
  void join() {
  }
};
#else
template <class I, class O>
class threadworker
{
private:
  std::queue<I> in;
  
  const int thread_count;
public:
  threadworker(int c) : thread_count(c) {
  }
  
  virtual ~threadworker() {
  }
  
  void give(I t) {
    in.push(t);
  }
  
  void start() {
  }
  
  void run(int id) {
  }

  virtual O work(I) = 0;
  
  O get() {
    I i = in.front();
    in.pop();
    return work(i);
  }
  
  void join() {
  }
};

#endif

#endif /* _THREADWORKER_H_ */
