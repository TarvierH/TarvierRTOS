/* RTOS 全局配置。数值越大优先级越高，0 为最低优先级（留给空闲任务）。 */
#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

#define configMAX_PRIORITIES      8   /* 支持的优先级个数（就绪位图为 32 位） */
#define configMINIMAL_STACK_SIZE  64  /* 最小任务栈，单位：字（4 字节） */

#endif /* RTOS_CONFIG_H */
