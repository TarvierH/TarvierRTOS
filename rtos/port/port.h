/* 移植层接口声明。具体实现放在 rtos/port 下：
 *  - Cortex-M3 版本（下一步）：用汇编/内联汇编构造真实栈帧
 *  - 宿主测试版本：在 PC 上用占位实现验证核心逻辑
 */
#ifndef PORT_H
#define PORT_H

#include <stdint.h>
#include "task.h"

/* 初始化任务栈帧，返回新的栈顶指针。
 * pxTopOfStack 指向任务栈顶（高地址端，栈向下生长）。 */
uint32_t *pxPortInitialiseStack(uint32_t *pxTopOfStack,
                                TaskFunction_t pxCode,
                                void *pvParameters);

#endif /* PORT_H */
