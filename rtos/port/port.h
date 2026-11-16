/* 移植层接口：Cortex-M3 上下文切换（port.c 实现）。 */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>
#include "task.h"

/* 初始化任务栈帧，返回新的栈顶指针。
 * pxTopOfStack 指向任务栈顶（高地址端，栈向下生长）。 */
uint32_t *pxPortInitialiseStack(uint32_t *pxTopOfStack,
                                TaskFunction_t pxCode,
                                void *pvParameters);

/* 启动调度器：设置中断优先级并启动第一个任务（不会返回） */
void xPortStartScheduler(void);

/* 请求一次上下文切换：置位 PendSV（可在任务或中断里调用） */
void taskYIELD(void);

/* SysTick 钩子：bsp.c 以弱符号调用，port.c 用强符号覆盖，
 * 在其中执行调度节拍处理。 */
void rtos_systick_hook(void);

#endif /* PORT_H */
