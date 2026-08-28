/**
 * @file CCThread.cpp
 * @brief 线程创建工具函数
 * @details 封装pthread API，创建分离线程（detached thread）
 *          知识点：pthread线程库、线程属性、分离线程
 */

#include "CCThread.h"//主要是用了pthread.h

/**
 * @brief 创建分离线程
 * @details 知识点：pthread线程创建流程
 *          1. 初始化线程属性（pthread_attr_init）
 *          2. 设置分离属性（pthread_attr_setdetachstate：PTHREAD_CREATE_DETACHED）
 *          3. 创建线程（pthread_create）
 *          4. 销毁属性（pthread_attr_destroy）
 *          5. 分离线程（pthread_detach）
 *          
 *          分离线程特点：
 *          - 不能使用pthread_join等待
 *          - 线程退出时自动释放资源
 *          - 适合后台任务，不需要等待结果
 * @param thread 输出参数，线程ID（如果为NULL，使用局部变量）
 * @param start_routine 线程入口函数（void* func(void*)）
 * @param arg 传递给线程函数的参数
 * @return 0=成功，-1=失败
 */
int detach_thread_create(pthread_t *thread, void * start_routine, void *arg)
{
    pthread_attr_t attr;      // 线程属性结构
    pthread_t thread_t;       // 局部线程ID（当thread为NULL时使用）让系统随机给
    //一个有效的线程ID，初始化后确保是有效的。

    int ret = 0;

    // 如果thread为NULL，使用局部变量
    if(thread==NULL){
        thread = &thread_t;
    }
    
    // 知识点：初始化线程属性
    // pthread_attr_init - 初始化线程属性结构为默认值
    // 返回：0=成功，非0=失败
    if(pthread_attr_init(&attr))
    {
        printf("pthread_attr_init fail!\n");
        return -1;
    }

    // 知识点：设置线程分离属性
    // pthread_attr_setdetachstate - 设置线程为分离状态
    // PTHREAD_CREATE_DETACHED: 分离线程
    //   特点：线程退出时自动释放资源，不能用pthread_join等待
    //   用途：适合后台任务，不需要等待结果
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
    {
        printf("pthread_attr_setdetachstate fail!\n");
        pthread_attr_destroy(&attr);
        return -1;
    }

    // 知识点：创建线程
    // pthread_create - 创建新线程
    // 参数：
    //   - thread: 输出参数，线程ID
    //   - &attr: 线程属性（NULL=使用默认属性）
    //   - start_routine: 线程入口函数（需要强制转换函数指针类型），上面是通用指针
    //   - arg: 传递给线程函数的参数
    // 返回：0=成功，非0=失败
    ret = pthread_create(thread, &attr, (void *(*)(void *))start_routine, arg);
    if(ret < 0){
        printf("pthread_create fail!\n");
        pthread_attr_destroy(&attr);
        return -1;
    }

    // 知识点：销毁线程属性
    // pthread_attr_destroy - 释放属性结构占用的资源
    pthread_attr_destroy(&attr);
    
    // 知识点：分离线程（双重保险）
    // pthread_detach - 将线程设为分离状态
    // 即使创建时已设置分离属性，这里再次调用确保线程被分离
    // 返回：0=成功，非0=失败
    ret = pthread_detach(*thread);
    return ret;
}