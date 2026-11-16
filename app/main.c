/* 最小 RTOS 演示：两个同优先级任务在 1ms 时间片下轮转运行。
 *  - LED 任务：约每 500ms 翻转一次 LED
 *  - 打印任务：约每秒通过串口打印一个计数
 *
 * 两个任务目前都靠“忙等”节拍来定时；下一课会用 vTaskDelay 让任务
 * 真正阻塞（进延时列表），而不是空转。 */
#include "bsp.h"
#include "task.h"

#define TASK_STACK_WORDS 128u

static StackType_t xLedTaskStack[TASK_STACK_WORDS] __attribute__((aligned(8)));
static TCB_t xLedTaskTCB;
static StackType_t xPrintTaskStack[TASK_STACK_WORDS] __attribute__((aligned(8)));
static TCB_t xPrintTaskTCB;

static void prvLedTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t ulLast = bsp_get_tick();

    for (;;) {
        uint32_t ulNow = bsp_get_tick();
        if (ulNow - ulLast >= 500u) {
            ulLast = ulNow;
            bsp_led_toggle();
        }
    }
}

static void prvPrintTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t ulLast = bsp_get_tick();
    uint32_t ulSeconds = 0u;

    for (;;) {
        uint32_t ulNow = bsp_get_tick();
        if (ulNow - ulLast >= 1000u) {
            ulLast = ulNow;
            ulSeconds++;
            bsp_print("task2 ");
            bsp_print_dec((int32_t)ulSeconds);
            bsp_print(" s\r\n");
        }
    }
}

int main(void)
{
    bsp_init();

    bsp_print("\r\n=== TarvierRTOS: two tasks round-robin ===\r\n");

    vTaskInitialiseCore();

    (void)xTaskCreateStatic(prvLedTask, "LED",
                            sizeof(xLedTaskStack), NULL, 1u,
                            xLedTaskStack, &xLedTaskTCB);
    (void)xTaskCreateStatic(prvPrintTask, "PRINT",
                            sizeof(xPrintTaskStack), NULL, 1u,
                            xPrintTaskStack, &xPrintTaskTCB);

    vTaskStartScheduler();   /* 启动调度器，不会返回 */

    for (;;) { }
}
