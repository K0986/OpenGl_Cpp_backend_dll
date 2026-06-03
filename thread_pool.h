#pragma once
// thread_pool.h — fixed-size thread pool sized to hardware_concurrency.
// Workers are created once at construction and destroyed at shutdown.
// submit() enqueues a job; waitAll() blocks until the queue is empty.
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool
{
public:
    // numThreads=0 → use hardware_concurrency(), clamped to [2, 4]
    explicit ThreadPool(int numThreads = 0)
    {
        int n = numThreads > 0 ? numThreads
                               : (int)std::thread::hardware_concurrency();
        if (n < 2) n = 2;
        if (n > 4) n = 4;
        m_workers.reserve(n);
        for (int i = 0; i < n; ++i)
            m_workers.emplace_back([this]{ worker(); });
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_workers) if (t.joinable()) t.join();
    }

    // Non-copyable
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(std::function<void()> job)
    {
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_jobs.push(std::move(job));
            ++m_pending;
        }
        m_cv.notify_one();
    }

    // Block until all submitted jobs have completed.
    void waitAll()
    {
        std::unique_lock<std::mutex> lk(m_mtx);
        m_doneCv.wait(lk, [this]{ return m_pending == 0 && m_jobs.empty(); });
    }

    int workerCount() const { return (int)m_workers.size(); }

private:
    void worker()
    {
        for (;;)
        {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cv.wait(lk, [this]{ return m_stop || !m_jobs.empty(); });
                if (m_stop && m_jobs.empty()) return;
                job = std::move(m_jobs.front());
                m_jobs.pop();
            }
            job();
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                if (--m_pending == 0 && m_jobs.empty())
                    m_doneCv.notify_all();
            }
        }
    }

    std::vector<std::thread>          m_workers;
    std::queue<std::function<void()>> m_jobs;
    std::mutex                        m_mtx;
    std::condition_variable           m_cv;
    std::condition_variable           m_doneCv;
    std::atomic<int>                  m_pending{0};
    bool                              m_stop = false;
};
