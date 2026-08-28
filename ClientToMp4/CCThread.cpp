#include "CCThread.h"

/**
 * @brief 创建分离线程
 * @details 创建一个分离状态的线程，线程退出时自动释放资源，无需join
 *          功能：初始化线程属性、设置分离状态、创建线程、确保资源自动释放
 *          知识点：pthread分离线程、线程属性设置、资源管理
 * @param thread 线程句柄指针（可为NULL，使用内部变量）
 * @param start_routine 线程入口函数
 * @param arg 线程参数
 * @return 0=成功，-1=失败
 */
int detach_thread_create(pthread_t *thread, void * start_routine, void *arg)
{
    pthread_attr_t attr;
    pthread_t thread_t;
    int ret = 0;

    if(thread==NULL){
        thread = &thread_t;
    }
    //初始化线程的属性
    if(pthread_attr_init(&attr))
    {
        printf("pthread_attr_init fail!\n");
        return -1;
    }

    //设置线程detachstate属性。该表示新线程是否与进程中其他线程脱离同步
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
    {//新线程不能用pthread_join()来同步，且在退出时自行释放所占用的资源。
        printf("pthread_attr_setdetachstate fail!\n");
        pthread_attr_destroy(&attr);
    }

    ret = pthread_create(thread, &attr, (void *(*)(void *))start_routine, arg);
    if(ret < 0){
        printf("pthread_create fail!\n");
        pthread_attr_destroy(&attr);
    }

    //将状态设为unjoinable状态，确保资源的释放。
    ret = pthread_detach(thread_t);
    return ret;
}