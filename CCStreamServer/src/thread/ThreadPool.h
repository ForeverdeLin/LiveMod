#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>           //已经修改完,先创建对应数量的工作线程，然后
#include <queue>           //工作线程里面等待任务队列有任务，然后执行任务              
#include <thread>           //后面的发送接收线程直接提交到任务队列里
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

/**
 * @brief 线程池类
 * @details 预先创建固定数量的工作线程，从任务队列中取任务执行<----看这里就行
 *          优点：避免频繁创建/销毁线程，提高性能，控制并发数量
 */
class ThreadPool
{
public:
    /**
     * @brief 构造函数
     * @param threadCount 线程池中工作线程的数量（默认=CPU核心数）
     */
    ThreadPool(size_t threadCount = std::thread::hardware_concurrency());
    //hardware_concurrency()是 C++ 标准库中的一个函数，作用是获取当前系统的硬件并发能力，
    //简单说就是返回设备支持的 “同时执行线程数”（通常等于 CPU 核心数
    /**
     * @brief 析构函数
     * @details 停止所有线程，等待任务完成
     */
    ~ThreadPool();
    
    /**
     * @brief 提交任务到线程池，提交后不一定能立刻执行
     * @param task 要执行的任务（函数对象）
     * @return true=成功，false=线程池已停止
     */
    bool EnqueueTask(std::function<void()> task);
    //std::function：C++11 引入的模板类，用来包装任意可调用对象（比如函数、lambda 表达式、函数对象等），
    //是一种 “通用函数容器”。
    //<void()>：是std::function的模板参数，表示这个可调用对象的 “签名”——无参数（括号里为空）、
    //返回值是 void（无返回值）。

    /**
     * @brief 停止线程池
     * @details 不再接受新任务，等待所有任务完成
     */
    void Stop();
    
    /**
     * @brief 获取当前任务队列大小
     */
    size_t GetQueueSize() const;//标记函数内部不会修改类的任何成员变量。
    
    /**
     * @brief 获取线程池大小
     */
    size_t GetThreadCount() const { return m_threads.size(); }

private:
    /**
     * @brief 工作线程函数
     * @details 从任务队列中取任务并执行
     */
    void WorkerThread();
    
private:
    std::vector<std::thread> m_threads;           // 工作线程数组
    std::queue<std::function<void()>> m_tasks;    // 任务队列
    mutable std::mutex m_queueMutex;              // 任务队列互斥锁（mutable允许在const方法中使用）
    std::condition_variable m_condition;          // 条件变量（通知线程有新任务）
    std::atomic<bool> m_stop;                     // 停止标志
};

#endif // THREADPOOL_H

