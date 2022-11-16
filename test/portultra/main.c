#if 0

#include <stdio.h>
#include <stdlib.h>
#include "ultra64.h"

#define THREAD_STACK_SIZE 0x1000

u8 idle_stack[THREAD_STACK_SIZE] ALIGNED(16);
u8 main_stack[THREAD_STACK_SIZE] ALIGNED(16);
u8 thread3_stack[THREAD_STACK_SIZE] ALIGNED(16);
u8 thread4_stack[THREAD_STACK_SIZE] ALIGNED(16);

OSThread idle_thread;
OSThread main_thread;
OSThread thread3;
OSThread thread4;

OSMesgQueue queue;
OSMesg buf[1];

void thread3_func(UNUSED void *arg) {
    OSMesg val;
    printf("Thread3 recv\n");
    fflush(stdout);
    osRecvMesg(&queue, &val, OS_MESG_BLOCK);
    printf("Thread3 complete: %d\n", (int)(intptr_t)val);
    fflush(stdout);
}

void thread4_func(void *arg) {
    printf("Thread4 send %d\n", (int)(intptr_t)arg);
    fflush(stdout);
    osSendMesg(&queue, arg, OS_MESG_BLOCK);
    printf("Thread4 complete\n");
    fflush(stdout);
}

void main_thread_func(UNUSED void* arg) {
    osCreateMesgQueue(&queue, buf, sizeof(buf) / sizeof(buf[0]));

    printf("main thread creating thread 3\n");
    osCreateThread(&thread3, 3, thread3_func, NULL, &thread3_stack[THREAD_STACK_SIZE], 14);
    printf("main thread starting thread 3\n");
    osStartThread(&thread3);
    
    printf("main thread creating thread 4\n");
    osCreateThread(&thread4, 4, thread4_func, (void*)10, &thread4_stack[THREAD_STACK_SIZE], 13);
    printf("main thread starting thread 4\n");
    osStartThread(&thread4);

    while (1) {
        printf("main thread doin stuff\n");
        sleep(1);
    }
}

void idle_thread_func(UNUSED void* arg) {
    printf("idle thread\n");
    printf("creating main thread\n");
    osCreateThread(&main_thread, 2, main_thread_func, NULL, &main_stack[THREAD_STACK_SIZE], 11);
    printf("starting main thread\n");
    osStartThread(&main_thread);

    // Set this thread's priority to 0, making it the idle thread
    osSetThreadPri(NULL, 0);

    // idle
    while (1) {
        printf("idle thread doin stuff\n");
        sleep(1);
    }
}

void bootproc(void) {
    osInitialize();
    
    osCreateThread(&idle_thread, 1, idle_thread_func, NULL, &idle_stack[THREAD_STACK_SIZE], 127);
    printf("Starting idle thread\n");
    osStartThread(&idle_thread);
}

#endif
