#include <string.h>
#include "queue.h"
#include "port.h"

/**
 * @brief 使用静态内存创建队列
 *
 * 队列存储区和队列控制块均由调用者提供，不依赖动态内存分配。
 *
 * @param uxQueueLength    队列可容纳的最大元素个数
 * @param uxItemSize       单个元素的大小（字节），传入 0 表示队列用于互斥量
 * @param pucQueueStorage  队列存储区首地址，大小至少为 uxQueueLength * uxItemSize 字节
 * @param pxQueueBuffer    队列控制块（Queue_t 结构体）指针
 *
 * @return 创建成功返回队列句柄（静态创建不依赖动态内存，不会失败）
 */
QueueHandle_t xQueueCreateStatic(uint32_t uxQueueLength, uint32_t uxItemSize, uint8_t *pucQueueStorage, Queue_t *pxQueueBuffer)
{
    pxQueueBuffer->pcHead = pucQueueStorage;
    pxQueueBuffer->uxItemSize = uxItemSize;
    pxQueueBuffer->uxLength = uxQueueLength;

    pxQueueBuffer->pcReadFrom = pucQueueStorage;
    pxQueueBuffer->pcWriteTo = pucQueueStorage;

    pxQueueBuffer->uxMessagesWaiting = 0u;

    vListInitialise(&pxQueueBuffer->xTasksWaitingToReceive);
    vListInitialise(&pxQueueBuffer->xTasksWaitingToSend);

    return pxQueueBuffer;
}

/**
 * @brief 向队列发送一个元素
 *
 * 将 pvItemToQueue 指向的数据复制到队列尾部。若队列已满，调用任务
 * 将根据 xTicksToWait 阻塞等待，直到有空间可用或等待超时。
 *
 * @param xQueue          目标队列句柄，由 xQueueCreateStatic 创建
 * @param pvItemToQueue   指向待发送数据的指针，数据将被复制到队列中
 * @param xTicksToWait    队列满时的最大阻塞等待时间（系统节拍数），
 *                        0 表示不等待
 *
 * @return 成功发送返回 pdTRUE；超时未发送返回 pdFALSE
 */
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    TimeOut_t xTimeout;
    vTaskSetTimeOutState(&xTimeout);
    for (;;)
    {
        if (xQueue->uxMessagesWaiting < xQueue->uxLength)
        {
            if (xQueue->uxItemSize != 0u)
            {
                memcpy(xQueue->pcWriteTo, pvItemToQueue, xQueue->uxItemSize);
                xQueue->pcWriteTo += xQueue->uxItemSize;
                if (xQueue->pcWriteTo >= xQueue->pcHead + (xQueue->uxLength * xQueue->uxItemSize))
                {
                    xQueue->pcWriteTo = xQueue->pcHead;
                }
            }
            xQueue->uxMessagesWaiting++;

            /* 有等待接收的任务 → 唤醒一个（现在有数据了） */
            if (xQueue->xTasksWaitingToReceive.uxNumberOfItems > 0u)
            {
                if (xTaskRemoveFromEventList(&xQueue->xTasksWaitingToReceive) == pdTRUE)
                {
                    taskYIELD(); /* 唤醒的是更高优先级任务，让出 CPU */
                }
            }

            return pdTRUE;
        }
        if (xTicksToWait == 0u)
        {
            return pdFALSE;
        }
        if (xTaskCheckForTimeOut(&xTimeout, &xTicksToWait))
        {
            return pdFALSE;
        }

        vTaskPlaceOnEventList(&xQueue->xTasksWaitingToSend, xTicksToWait);
    }
}

/**
 * @brief 从队列接收一个元素
 *
 * 将队列头部的元素复制到 pvBuffer 指向的缓冲区。若队列为空，调用任务
 * 将根据 xTicksToWait 阻塞等待，直到有数据可用或等待超时。
 *
 * @param xQueue        目标队列句柄，由 xQueueCreateStatic 创建
 * @param pvBuffer      指向接收缓冲区的指针，数据将被复制到该缓冲区
 * @param xTicksToWait  队列空时的最大阻塞等待时间（系统节拍数），
 *                      0 表示不等待
 *
 * @return 成功接收返回 pdTRUE；超时未接收返回 pdFALSE
 */
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    TimeOut_t xTimeout;
    vTaskSetTimeOutState(&xTimeout);
    for (;;)
    {
        if (xQueue->uxMessagesWaiting > 0)
        {
            if (xQueue->uxItemSize != 0u)
            {
                memcpy(pvBuffer, xQueue->pcReadFrom, xQueue->uxItemSize);
                xQueue->pcReadFrom += xQueue->uxItemSize;
                if (xQueue->pcReadFrom >= xQueue->pcHead + (xQueue->uxLength * xQueue->uxItemSize))
                {
                    xQueue->pcReadFrom = xQueue->pcHead;
                }
            }
            xQueue->uxMessagesWaiting--;

            /* 有等待发送的任务 → 唤醒一个（现在有空间了） */
            if (xQueue->xTasksWaitingToSend.uxNumberOfItems > 0u)
            {
                if (xTaskRemoveFromEventList(&xQueue->xTasksWaitingToSend) == pdTRUE)
                {
                    taskYIELD();
                }
            }
            return pdTRUE;
        }
        if (xTicksToWait == 0u)
        {
            return pdFALSE;
        }

        if (xTaskCheckForTimeOut(&xTimeout, &xTicksToWait))
        {
            return pdFALSE;
        }

        vTaskPlaceOnEventList(&xQueue->xTasksWaitingToReceive, xTicksToWait);
    }
}
