#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "queue.h"

typedef QueueHandle_t SemaphoreHandle_t;   /* 信号量 = 队列句柄 */

/* 计数信号量：最大计数 uxMaxCount，初始计数 uxInitialCount */
SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount,
                                                 UBaseType_t uxInitialCount,
                                                 Queue_t *pxSemaphoreBuffer);

/* 二值信号量：最大计数 1，初始 0 */
#define xSemaphoreCreateBinaryStatic(pxSemaphoreBuffer) \
    xSemaphoreCreateCountingStatic(1u, 0u, pxSemaphoreBuffer)

/* take = receive（计数值 -1，为 0 则阻塞） */
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);

/* give = send（计数值 +1，达上限则失败，非阻塞） */
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

#endif