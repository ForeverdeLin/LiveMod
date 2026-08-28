#ifndef CCTHREAD_H
#define CCTHREAD_H

#include "CCSocketDefine.h"

int detach_thread_create(pthread_t * thread, void* start_routine, void* arg);

#endif