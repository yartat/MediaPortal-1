// Runs calls on the thread that drives the live555 event loop (live555 is single-threaded).
// Event thread: Enter() before the first step of a run, Drain() between steps, Leave() after the last step.
// Any other thread: Call(). While no run is active the call executes directly on the caller.
#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

class EventThreadCalls
{
public:
  void Enter()
  {
    std::lock_guard<std::mutex> lk(m_lock);
    m_thread = std::this_thread::get_id();
    m_active = true;
  }

  void Drain()
  {
    for (;;)
    {
      std::function<void()> f;
      {
        std::lock_guard<std::mutex> lk(m_lock);
        if (m_queue.empty()) return;
        f = std::move(m_queue.front());
        m_queue.pop_front();
      }
      f();
    }
  }

  // Runs what is left in the queue; later calls execute directly.
  void Leave()
  {
    std::lock_guard<std::mutex> lk(m_lock);
    m_active = false;
    while (!m_queue.empty())
    {
      std::function<void()> f = std::move(m_queue.front());
      m_queue.pop_front();
      f();
    }
  }

  // true: fn has run. false: timed out; fn stays queued and runs later, so it must own everything it uses.
  bool Call(std::function<void()> fn, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lk(m_lock);
    if (!m_active || m_thread == std::this_thread::get_id())
    {
      fn();   // under the lock: Enter() cannot start a run in the middle of it
      return true;
    }
    std::shared_ptr<Done> done = std::make_shared<Done>();
    m_queue.push_back([fn, done]()
    {
      try { fn(); } catch (...) {}
      std::lock_guard<std::mutex> dl(done->m);
      done->finished = true;
      done->cv.notify_all();
    });
    lk.unlock();
    std::unique_lock<std::mutex> dl(done->m);
    return done->cv.wait_for(dl, timeout, [&done] { return done->finished; });
  }

private:
  struct Done
  {
    std::mutex m;
    std::condition_variable cv;
    bool finished = false;
  };

  std::mutex m_lock;
  std::deque<std::function<void()> > m_queue;
  std::thread::id m_thread;
  bool m_active = false;
};
