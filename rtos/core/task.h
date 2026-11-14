/* 任务控制块（TCB）与任务管理。
 *
 * 本阶段只做“数据结构 + 就绪队列管理”，不涉及上下文切换：
 *  - 每个 TCB 持有两个链表节点：xStateListItem 用于挂到就绪列表/挂起列表，
 *    xEventListItem 预留给后续的信号量/队列等待。
 *  - 就绪列表是一个“每优先级一条”的链表数组，配合 32 位就绪位图，
 *    用 __builtin_clz 实现 O(1) 找到最高优先级就绪任务。
 */
#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "list.h"
#include "rtos_config.h"

typedef void (*TaskFunction_t)(void *);

typedef enum {
    TASK_STATE_READY = 0,
    TASK_STATE_BLOCKED,
    TASK_STATE_SUSPENDED,
    TASK_STATE_DELETED
} TaskState_t;

typedef struct task_control_block {
    volatile uint32_t *pxTopOfStack; /* 任务栈顶（上下文切换的恢复点） */
    ListItem_t xStateListItem;       /* 就绪列表 / 挂起列表节点 */
    ListItem_t xEventListItem;       /* 事件等待列表节点（后续 IPC 使用） */
    uint32_t uxPriority;             /* 当前优先级（互斥锁可能临时抬高） */
    uint32_t uxBasePriority;         /* 基础优先级（优先级继承用） */
    uint32_t *pxStack;               /* 栈底（用于溢出检测） */
    uint32_t ulStackSize;            /* 栈大小，单位：字节 */
    const char *pcTaskName;
    TaskState_t eState;
} TCB_t;

typedef TCB_t *TaskHandle_t;

/* 就绪列表：每优先级一条链表 */
extern List_t pxReadyTasksLists[configMAX_PRIORITIES];

/* 就绪位图：bit i 置位表示优先级 i 有就绪任务 */
extern uint32_t uxReadyPriorityBitmap;

/* 初始化内核核心数据结构（就绪列表、位图），须在创建任务前调用一次 */
void vTaskInitialiseCore(void);

/* 静态创建任务：栈和 TCB 由调用者提供（本阶段用静态分配，稳定可预测） */
TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *pcName,
                               uint32_t ulStackSizeBytes,
                               void *pvParameters,
                               uint32_t uxPriority,
                               uint32_t *puxStackBuffer,
                               TCB_t *pxTaskBuffer);

/* 返回当前最高优先级就绪任务；无就绪任务返回 NULL */
TCB_t *pxGetHighestPriorityReadyTask(void);

/* 内部接口：加入 / 移出就绪列表 */
void prvAddTaskToReadyList(TCB_t *pxTCB);
void prvRemoveTaskFromReadyList(TCB_t *pxTCB);

#endif /* TASK_H */
