#ifndef __ULTRA64_MULTILIBULTRA_H__
#define __ULTRA64_MULTILIBULTRA_H__

#include <stdint.h>
#include "platform_specific.h"

#ifdef __cplusplus
#include <queue>
#endif

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#define ALIGNED(x) __attribute__((aligned(x)))
#else
#define UNUSED
#define ALIGNED(x)
#endif

typedef int64_t s64;
typedef uint64_t u64;
typedef int32_t s32;
typedef uint32_t u32;
typedef int16_t s16;
typedef uint16_t u16;
typedef int8_t s8;
typedef uint8_t u8;

#define PTR(x) uint32_t
#define RDRAM_ARG uint8_t *rdram, 
#define RDRAM_ARG1 uint8_t *rdram
#define PASS_RDRAM rdram, 
#define PASS_RDRAM1 rdram
#define TO_PTR(type, var) ((type*)(&rdram[var & 0x3FFFFFF]))
#ifdef __cplusplus
#define NULLPTR (PTR(void))0
#endif

#ifndef NULL
#define NULL (PTR(void) 0)
#endif

#define OS_MESG_NOBLOCK     0
#define OS_MESG_BLOCK       1

typedef s32 OSPri;
typedef s32 OSId;

/////////////
// Structs //
/////////////

typedef struct UltraThreadContext UltraThreadContext;

typedef enum {
    RUNNING,
    PAUSED,
    PREEMPTED
} OSThreadState;

typedef struct OSThread_t {
    PTR(struct OSThread_t) next; // Next thread in the given queue
    OSPri priority;
    uint32_t pad1;
    uint32_t pad2;
    uint16_t flags; // These two are swapped to reflect rdram byteswapping
    uint16_t state;
    OSId id;
    int32_t pad3;
    UltraThreadContext* context; // An actual pointer regardless of platform
    uint32_t sp;
} OSThread;

typedef u32 OSEvent;
typedef PTR(void) OSMesg;

// This union holds C++ members along with a padding array. Those members are guarded by an ifdef for C++
// so that they don't cause compilation errors in C. The padding array reserves the necessary space to
// hold the atomic members in C and a static assert is used to ensure that the union is large enough.
// typedef union UltraQueueContext {
//     u64 pad[1];
// #ifdef __cplusplus
//     struct {
//     } atomics;
//     // Construct pad instead of the atomics, which get constructed in-place in osCreateMesgQueue
//     UltraQueueContext() : pad{} {}
// #endif
// } UltraQueueContext;

// #ifdef __cplusplus
// static_assert(sizeof(UltraQueueContext::pad) == sizeof(UltraQueueContext),
//     "UltraQueueContext does not have enough padding to hold C++ members!");
// #endif

typedef struct OSMesgQueue {
    PTR(OSThread) blocked_on_recv; /* Linked list of threads blocked on receiving from this queue */
    PTR(OSThread) blocked_on_send; /* Linked list of threads blocked on sending to this queue */ 
    s32 validCount;            /* Number of messages in the queue */
    s32 first;                 /* Index of the first message in the ring buffer */
    s32 msgCount;              /* Size of message buffer */
    PTR(OSMesg) msg;               /* Pointer to circular buffer to store messages */
} OSMesgQueue;

///////////////
// Functions //
///////////////

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

void osInitialize(void);

typedef void (thread_func_t)(PTR(void));

void osCreateThread(RDRAM_ARG PTR(OSThread) t, OSId id, PTR(thread_func_t) entry, PTR(void) arg, PTR(void) sp, OSPri p);
void osStartThread(RDRAM_ARG PTR(OSThread) t);
void osSetThreadPri(RDRAM_ARG PTR(OSThread) t, OSPri pri);

s32 MQ_GET_COUNT(RDRAM_ARG PTR(OSMesgQueue));
s32 MQ_IS_EMPTY(RDRAM_ARG PTR(OSMesgQueue));
s32 MQ_IS_FULL(RDRAM_ARG PTR(OSMesgQueue));

void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue), PTR(OSMesg), s32);
s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue), OSMesg, s32);
s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue), OSMesg, s32);
s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue), PTR(OSMesg), s32);
void osSetEventMesg(RDRAM_ARG OSEvent, PTR(OSMesgQueue), OSMesg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
