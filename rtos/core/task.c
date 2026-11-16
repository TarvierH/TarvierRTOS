#include <string.h>
#include "task.h"
#include "port.h"

List_t pxReadyTasksLists[configMAX_PRIORITIES];
uint32_t uxReadyPriorityBitmap = 0u;

TCB_t *pxCurrentTCB = NULL;
volatile uint32_t xSchedulerRunning = 0u;

/* 空闲任务：最低优先级，当没有其它任务就绪时运行 */
static StackType_t xIdleTaskStack[configMINIMAL_STACK_SIZE]
    __attribute__((aligned(8)));
static TCB_t xIdleTaskTCB;

static void prvIdleTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        /* 真正的内核会在这里用 WFI 让 CPU 休眠省电 */
    }
}

/* 任务函数正常返回后停在这里 */
void prvTaskExitError(void)
{
    for (;;) { }
}

void vTaskInitialiseCore(void)
{
    for (uint32_t i = 0; i < configMAX_PRIORITIES; i++) {
        vListInitialise(&pxReadyTasksLists[i]);
    }
    uxReadyPriorityBitmap = 0u;
}

/* 静态创建任务：调用者提供栈缓冲区和 TCB 内存，内核不动态分配。
 * 返回任务句柄，供以后挂起、恢复、删除等操作使用。 */
TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *pcName,
                               uint32_t ulStackSizeBytes,
                               void *pvParameters,
                               uint32_t uxPriority,
                               uint32_t *puxStackBuffer,
                               TCB_t *pxTaskBuffer)
{
    TCB_t *pxTCB = pxTaskBuffer;

    /* 清空 TCB，避免未初始化字段残留脏数据 */
    memset(pxTCB, 0, sizeof(*pxTCB));

    /* 填充任务控制块的基本属性 */
    pxTCB->pcTaskName = pcName;
    pxTCB->uxPriority = uxPriority;      /* 当前优先级，调度依据 */
    pxTCB->uxBasePriority = uxPriority;  /* 基础优先级，后续支持互斥量继承时使用 */
    pxTCB->pxStack = puxStackBuffer;     /* 记录栈起始地址，便于检查溢出 */
    pxTCB->ulStackSize = ulStackSizeBytes;
    pxTCB->eState = TASK_STATE_READY;    /* 新建任务直接进入就绪态 */

    /* 两个链表节点先各自初始化，并标记所属任务 */
    vListInitialiseItem(&pxTCB->xStateListItem);
    vListInitialiseItem(&pxTCB->xEventListItem);
    pxTCB->xStateListItem.pvOwner = pxTCB;
    pxTCB->xEventListItem.pvOwner = pxTCB;
    pxTCB->xEventListItem.xItemValue = UINT32_MAX; /* 事件排序时默认排最后 */

    /* 初始化任务栈帧（由移植层实现；Cortex-M 版本在下一步完成）。
     * 栈为满递减，因此从栈缓冲区的最高地址向下布局初始上下文。 */
    pxTCB->pxTopOfStack = pxPortInitialiseStack(
        puxStackBuffer + (ulStackSizeBytes / sizeof(uint32_t)),
        pxTaskCode,
        pvParameters);

    /* 挂入对应优先级的就绪链表，并置位就绪位图 */
    prvAddTaskToReadyList(pxTCB);

    return pxTCB;
}

/* 将任务挂入其优先级对应的就绪链表，并更新就绪优先级位图。
 * 位图第 N 位为 1 表示优先级 N 上有就绪任务，调度器据此快速定位最高就绪优先级。 */
void prvAddTaskToReadyList(TCB_t *pxTCB)
{
    /* 插入链表末尾：同优先级任务按到达顺序排列，配合轮转游标实现公平调度 */
    vListInsertEnd(&pxReadyTasksLists[pxTCB->uxPriority],
                   &pxTCB->xStateListItem);

    /* 置位该优先级对应的位图位，表示此优先级存在就绪任务 */
    uxReadyPriorityBitmap |= (1u << pxTCB->uxPriority);
}

void prvRemoveTaskFromReadyList(TCB_t *pxTCB)
{
    uint32_t uxPriority = pxTCB->uxPriority;

    uxListRemove(&pxTCB->xStateListItem);

    /* 该优先级已无就绪任务时清掉对应位 */
    if (pxReadyTasksLists[uxPriority].uxNumberOfItems == 0u) {
        uxReadyPriorityBitmap &= ~(1u << uxPriority);
    }
}

TCB_t *pxGetHighestPriorityReadyTask(void)
{
    if (uxReadyPriorityBitmap == 0u) {
        return NULL;
    }

    /* __builtin_clz 返回前导零个数：最高置位即最高就绪优先级 */
    uint32_t uxTopPriority = 31u - (uint32_t)__builtin_clz(uxReadyPriorityBitmap);

    List_t *pxList = &pxReadyTasksLists[uxTopPriority];
    if (pxList->uxNumberOfItems == 0u) {
        return NULL; /* 位图与列表不一致，不应发生 */
    }

    return (TCB_t *)pxList->xListEnd.pxNext->pvOwner;
}

/* 选出下一个要运行的任务。在 PendSV 里调用。
 * 同优先级任务靠游标轮转实现时间片公平调度。 */
void vTaskSwitchContext(void)
{
    uint32_t uxTopPriority;
    List_t *pxList;

    if (uxReadyPriorityBitmap == 0u) {
        return; /* 没有就绪任务（正常不会发生：空闲任务永远就绪） */
    }

    uxTopPriority = 31u - (uint32_t)__builtin_clz(uxReadyPriorityBitmap);
    pxList = &pxReadyTasksLists[uxTopPriority];

    /* 游标前进一位，跳过哨兵，得到下一个任务 */
    pxList->pxIndex = pxList->pxIndex->pxNext;
    if ((void *)pxList->pxIndex == (void *)&pxList->xListEnd) {
        pxList->pxIndex = pxList->pxIndex->pxNext;
    }

    pxCurrentTCB = (TCB_t *)pxList->pxIndex->pvOwner;
}

/* 系统节拍处理：返回 pdTRUE 表示需要切换任务。
 * 里程碑 1：同一优先级有多个就绪任务时触发时间片轮转；
 * 里程碑 2 会在这里加入延时列表处理（vTaskDelay）。 */
BaseType_t xTaskIncrementTick(void)
{
    BaseType_t xSwitchRequired = pdFALSE;

    if (pxReadyTasksLists[pxCurrentTCB->uxPriority].uxNumberOfItems > 1u) {
        xSwitchRequired = pdTRUE;
    }

    return xSwitchRequired;
}

void vTaskStartScheduler(void)
{
    /* 创建空闲任务（优先级 0，最低） */
    (void)xTaskCreateStatic(prvIdleTask, "IDLE",
                            sizeof(xIdleTaskStack),
                            NULL, 0u,
                            xIdleTaskStack, &xIdleTaskTCB);

    /* 首个运行的任务：当前最高优先级就绪任务 */
    pxCurrentTCB = pxGetHighestPriorityReadyTask();

    xSchedulerRunning = 1u;

    /* 启动第一个任务，此函数不会返回 */
    xPortStartScheduler();

    for (;;) { } /* 不应到达 */
}
