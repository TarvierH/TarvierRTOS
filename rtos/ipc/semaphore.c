#include "semaphore.h"

/**
 * @brief 静态创建计数信号量
 * @param uxMaxCount 信号量最大计数值（最多可被获取的次数）
 * @param uxInitialCount 信号量初始计数值
 * @param pxSemaphoreBuffer 指向静态分配的队列结构体存储区
 * @return 创建成功返回信号量句柄（静态创建不会失败）
 */
SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount,
                                                 UBaseType_t uxInitialCount,
                                                 Queue_t *pxSemaphoreBuffer)
{
    /* 信号量 = uxItemSize 为 0 的队列，不需要存储区 */
    SemaphoreHandle_t xSemaphore =
        xQueueCreateStatic(uxMaxCount, 0u, NULL, pxSemaphoreBuffer);

    /* 设置初始计数值（队列创建时默认为 0） */
    xSemaphore->uxMessagesWaiting = uxInitialCount;

    return xSemaphore;
}

/**
 * @brief 获取信号量（计数值减 1）
 * @param xSemaphore 信号量句柄
 * @param xTicksToWait 等待超时时间（以 tick 计）
 * @return 获取成功返回 pdTRUE，超时返回 pdFALSE
 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    return xQueueReceive(xSemaphore, NULL, xTicksToWait);
}

/**
 * @brief 释放信号量（计数值加 1）
 * @param xSemaphore 信号量句柄
 * @return 释放成功返回 pdTRUE，计数值已达最大值返回 pdFALSE
 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    return xQueueSend(xSemaphore, NULL, 0);
}