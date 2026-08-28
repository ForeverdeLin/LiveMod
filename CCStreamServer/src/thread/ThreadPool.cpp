/**                                 //已修改，不用死磕，看.h
 * @file ThreadPool.cpp
 * @brief 线程池实现
 * @details 实现固定数量的工作线程，从任务队列中取任务执行
 *          知识点：线程池、任务队列、条件变量、生产者-消费者模式
 */

#include "ThreadPool.h"
#include <iostream>

/**
 * @brief 构造函数
 * @details 创建指定数量的工作线程，存入vector
 * @111 emplace_back：是std::vector的成员函数，直接在容器末尾构造新对象
 * （这里是构造std::thread对象），比push_back更高效。
 * [this] { WorkerThread(); }：这是一个 lambda 表达式（作为线程的执行函数）：
[this]：捕获当前ThreadPool对象的指针，让 lambda 内部能访问类的成员函数WorkerThread()。

 */
ThreadPool::ThreadPool(size_t threadCount)
    : m_stop(false)//为false就是运行线程池
{
    // 创建指定数量的工作线程,,//向线程容器vector中添加线程
    for (size_t i = 0; i < threadCount; ++i) {
        m_threads.emplace_back([this] {//emplace调用直接尾部插入对象，this是这个线程池对象，用来使用work函数
            WorkerThread();
        });
        //ThreadPool(count) 会创建count个 std::thread
        //每个线程运行 WorkerThread() 函数，这个函数会从任务队列中取任务并执行。
    }
    
    printf("ThreadPool created with %zu threads\n", threadCount);
}

/**
 * @brief 析构函数
 * @details 停止所有线程，等待任务完成
 */
ThreadPool::~ThreadPool()
{
    Stop();
}

/**
 * @brief 停止线程池
 * @details 设置停止标志，唤醒所有线程，等待所有线程退出
 */
void ThreadPool::Stop()
{
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;  // 设置停止标志
    }
    
    m_condition.notify_all();  // 唤醒所有等待的线程
    
    // 等待所有线程退出
    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    printf("ThreadPool stopped\n");
}

/**
 * @brief 提交任务到线程池
 * @details 将任务加入队列，唤醒一个等待的线程
 */
bool ThreadPool::EnqueueTask(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        
        // 如果线程池已停止，不接受新任务
        if (m_stop) {
            return false;
        }
        
        // 将任务加入队列
        m_tasks.push(std::move(task));
    }
    
    // 唤醒一个等待的线程
    m_condition.notify_one();
    
    return true;
}

/**
 * @brief 工作线程函数
 * @details 从任务队列中取任务并执行
 *          知识点：条件变量、生产者-消费者模式
 */
void ThreadPool::WorkerThread()
{
    while (true) {
        std::function<void()> task;
        
        {
            // 知识点：条件变量等待
            // unique_lock: 可解锁的锁，用于条件变量
            // wait: 等待条件满足（有新任务或线程池停止）
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            // 等待条件：有新任务或线程池停止
            // lambda表达式：条件判断函数
            m_condition.wait(lock, [this] {
                return !m_tasks.empty() || m_stop;
            });
//不是“线程池要停了还要继续干活”，而是：要停了的时候，
//得让正在睡觉的线程醒过来，才能正常退出。


            
            // 如果线程池停止且队列为空，退出线程
            if (m_stop && m_tasks.empty()) {
                return;
            }
            
            // 从队列中取任务
            task         = std::move(m_tasks.front());
            m_tasks.pop();
        }
        
        // 执行任务（在锁外执行，避免长时间占用锁）
        task();//到这里，worker
    }
}

/**
 * @brief 获取当前任务队列大小
 */
size_t ThreadPool::GetQueueSize() const
{
    std::lock_guard<std::mutex> lock(m_queueMutex);
    return m_tasks.size();
}

