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

#define PTR(x) int32_t
#define RDRAM_ARG uint8_t *rdram, 
#define RDRAM_ARG1 uint8_t *rdram
#define PASS_RDRAM rdram, 
#define PASS_RDRAM1 rdram
#define TO_PTR(type, var) ((type*)(&rdram[(uint64_t)var - 0xFFFFFFFF80000000]))
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

typedef u64	OSTime;

#define OS_EVENT_SW1              0     /* CPU SW1 interrupt */
#define OS_EVENT_SW2              1     /* CPU SW2 interrupt */
#define OS_EVENT_CART             2     /* Cartridge interrupt: used by rmon */
#define OS_EVENT_COUNTER          3     /* Counter int: used by VI/Timer Mgr */
#define OS_EVENT_SP               4     /* SP task done interrupt */
#define OS_EVENT_SI               5     /* SI (controller) interrupt */
#define OS_EVENT_AI               6     /* AI interrupt */
#define OS_EVENT_VI               7     /* VI interrupt: used by VI/Timer Mgr */
#define OS_EVENT_PI               8     /* PI interrupt: used by PI Manager */
#define OS_EVENT_DP               9     /* DP full sync interrupt */
#define OS_EVENT_CPU_BREAK        10    /* CPU breakpoint: used by rmon */
#define OS_EVENT_SP_BREAK         11    /* SP breakpoint:  used by rmon */
#define OS_EVENT_FAULT            12    /* CPU fault event: used by rmon */
#define OS_EVENT_THREADSTATUS     13    /* CPU thread status: used by rmon */
#define OS_EVENT_PRENMI           14    /* Pre NMI interrupt */

#define	M_GFXTASK	1
#define	M_AUDTASK	2
#define	M_VIDTASK	3

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
    int32_t sp;
} OSThread;

typedef u32 OSEvent;
typedef PTR(void) OSMesg;

typedef struct OSMesgQueue {
    PTR(OSThread) blocked_on_recv; /* Linked list of threads blocked on receiving from this queue */
    PTR(OSThread) blocked_on_send; /* Linked list of threads blocked on sending to this queue */ 
    s32 validCount;            /* Number of messages in the queue */
    s32 first;                 /* Index of the first message in the ring buffer */
    s32 msgCount;              /* Size of message buffer */
    PTR(OSMesg) msg;               /* Pointer to circular buffer to store messages */
} OSMesgQueue;

typedef struct {
    u32	type;
    u32	flags;

    PTR(u64) ucode_boot;
    u32	ucode_boot_size;

    PTR(u64) ucode;
    u32	ucode_size;

    PTR(u64) ucode_data;
    u32	ucode_data_size;

    PTR(u64) dram_stack;
    u32	dram_stack_size;

    PTR(u64) output_buff;
    PTR(u64) output_buff_size;

    PTR(u64) data_ptr;
    u32	data_size;

    PTR(u64) yield_data_ptr;
    u32	yield_data_size;
} OSTask_t;

typedef union {
    OSTask_t t;
    int64_t force_structure_alignment;
} OSTask;

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
void osStopThread(RDRAM_ARG PTR(OSThread) t);
void osDestroyThread(RDRAM_ARG PTR(OSThread) t);
void osSetThreadPri(RDRAM_ARG PTR(OSThread) t, OSPri pri);
OSPri osGetThreadPri(RDRAM_ARG PTR(OSThread) thread);
OSId osGetThreadId(RDRAM_ARG PTR(OSThread) t);

s32 MQ_GET_COUNT(RDRAM_ARG PTR(OSMesgQueue));
s32 MQ_IS_EMPTY(RDRAM_ARG PTR(OSMesgQueue));
s32 MQ_IS_FULL(RDRAM_ARG PTR(OSMesgQueue));

void osCreateMesgQueue(RDRAM_ARG PTR(OSMesgQueue), PTR(OSMesg), s32);
s32 osSendMesg(RDRAM_ARG PTR(OSMesgQueue), OSMesg, s32);
s32 osJamMesg(RDRAM_ARG PTR(OSMesgQueue), OSMesg, s32);
s32 osRecvMesg(RDRAM_ARG PTR(OSMesgQueue), PTR(OSMesg), s32);
void osSetEventMesg(RDRAM_ARG OSEvent, PTR(OSMesgQueue), OSMesg);
void osViSetEvent(RDRAM_ARG PTR(OSMesgQueue), OSMesg, u32);
void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr);
u32 osGetCount();
OSTime osGetTime();
u32 osVirtualToPhysical(PTR(void) addr);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
