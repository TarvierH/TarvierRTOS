#include <string.h>
#include "task.h"
#include "port.h"

List_t pxReadyTasksLists[configMAX_PRIORITIES];
uint32_t uxReadyPriorityBitmap = 0u;

void vTaskInitialiseCore(void)
{
    for (uint32_t i = 0; i < configMAX_PRIORITIES; i++) {
        vListInitialise(&pxReadyTasksLists[i]);
    }
    uxReadyPriorityBitmap = 0u;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *pcName,
                               uint32_t ulStackSizeBytes,
                               void *pvParameters,
                               uint32_t uxPriority,
                               uint32_t *puxStackBuffer,
                               TCB_t *pxTaskBuffer)
{
    TCB_t *pxTCB = pxTaskBuffer;

    memset(pxTCB, 0, sizeof(*pxTCB));

    pxTCB->pcTaskName = pcName;
    pxTCB->uxPriority = uxPriority;
    pxTCB->uxBasePriority = uxPriority;
    pxTCB->pxStack = puxStackBuffer;
    pxTCB->ulStackSize = ulStackSizeBytes;
    pxTCB->eState = TASK_STATE_READY;

    /* 两个链表节点先各自初始化，并标记所属任务 */
    vListInitialiseItem(&pxTCB->xStateListItem);
    vListInitialiseItem(&pxTCB->xEventListItem);
    pxTCB->xStateListItem.pvOwner = pxTCB;
    pxTCB->xEventListItem.pvOwner = pxTCB;
    pxTCB->xEventListItem.xItemValue = UINT32_MAX;

    /* 初始化任务栈帧（由移植层实现；Cortex-M 版本在下一步完成） */
    pxTCB->pxTopOfStack = pxPortInitialiseStack(
        puxStackBuffer + (ulStackSizeBytes / sizeof(uint32_t)),
        pxTaskCode,
        pvParameters);

    prvAddTaskToReadyList(pxTCB);

    return pxTCB;
}

void prvAddTaskToReadyList(TCB_t *pxTCB)
{
    vListInsertEnd(&pxReadyTasksLists[pxTCB->uxPriority],
                   &pxTCB->xStateListItem);
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
