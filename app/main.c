#include "bsp.h"
#include "task.h"
#include "semaphore.h"

#define TASK_STACK_WORDS 128u

static Queue_t xSemBuffer;   /* 二值信号量的队列结构体 */

static StackType_t xWaiterStack[TASK_STACK_WORDS] __attribute__((aligned(8)));
static TCB_t       xWaiterTCB;
static StackType_t xSignalerStack[TASK_STACK_WORDS] __attribute__((aligned(8)));
static TCB_t       xSignalerTCB;

/* 等待者：阻塞等信号 */
static void prvWaiterTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        xSemaphoreTake(&xSemBuffer, portMAX_DELAY);  /* 计数 0 → 阻塞，直到有人 give */
        bsp_print("signal received!\r\n");
    }
}

/* 发信号者：每 1s 给一次信号 */
static void prvSignalerTask(void *pvParameters)
{
    (void)pvParameters;
    TickType_t xLast = xTaskGetTickCount();
    for (;;) {
        bsp_print("give signal\r\n");
        xSemaphoreGive(&xSemBuffer);                 /* 计数 +1，唤醒等待者 */
        vTaskDelayUntil(&xLast, 1000);
    }
}

int main(void)
{
    bsp_init();
    bsp_print("\r\n=== Binary semaphore demo (M5) ===\r\n");

    vTaskInitialiseCore();

    xSemaphoreCreateBinaryStatic(&xSemBuffer);

    (void)xTaskCreateStatic(prvWaiterTask, "WAITER",
                            sizeof(xWaiterStack), NULL, 1u,
                            xWaiterStack, &xWaiterTCB);
    (void)xTaskCreateStatic(prvSignalerTask, "SIGNAL",
                            sizeof(xSignalerStack), NULL, 1u,
                            xSignalerStack, &xSignalerTCB);

    vTaskStartScheduler();
    for (;;) { }
}