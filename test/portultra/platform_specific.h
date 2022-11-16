#ifndef __PLATFORM_SPECIFIC_H__
#define __PLATFORM_SPECIFIC_H__

#if defined(__linux__)

//#include <pthread.h>
//
//typedef struct {
//    pthread_t t;
//    pthread_barrier_t pause_barrier;
//    pthread_mutex_t pause_mutex;
//    pthread_cond_t pause_cond;
//    void (*entrypoint)(void *);
//    void *arg;
//} OSThreadNative;

#elif defined(_WIN32)

//#include <pthread.h>
//
//typedef struct {
//    pthread_t t;
//    pthread_barrier_t pause_barrier;
//    pthread_mutex_t pause_mutex;
//    pthread_cond_t pause_cond;
//    void (*entrypoint)(void*);
//    void* arg;
//} OSThreadNative;

#endif

#endif