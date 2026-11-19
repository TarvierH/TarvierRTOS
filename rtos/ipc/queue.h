#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#include "list.h"
#include "task.h"

typedef struct queue
{
    uint8_t *pcHead;                     /* 存储区起始地址 */
    uint8_t *pcWriteTo;                  /* 下一个写入位置 */
    uint8_t *pcReadFrom;                 /* 下一个读取位置 */
    uint32_t uxLength;                   /* 最大元素个数 */
    uint32_t uxItemSize;                 /* 每个元素的字节数 */
    volatile uint32_t uxMessagesWaiting; /* 当前队列里的元素个数 */
    List_t xTasksWaitingToSend;          /* 等待发送的任务（阶段 2 用） */
    List_t xTasksWaitingToReceive;       /* 等待接收的任务（阶段 2 用） */
} Queue_t;

typedef Queue_t *QueueHandle_t;

QueueHandle_t xQueueCreateStatic(uint32_t uxQueueLength,
                                 uint32_t uxItemSize,
                                 uint8_t *pucQueueStorage,
                                 Queue_t *pxQueueBuffer);

BaseType_t xQueueSend(QueueHandle_t xQueue,
                      const void *pvItemToQueue,
                      TickType_t xTicksToWait);

BaseType_t xQueueReceive(QueueHandle_t xQueue,
                         void *pvBuffer,
                         TickType_t xTicksToWait);

#endif