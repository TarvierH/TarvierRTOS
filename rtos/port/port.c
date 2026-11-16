/* Cortex-M3 移植层：上下文切换（本项目最难、也最重要的部分）。
 *
 * 机制（面试必问）：
 *  - 每个任务的现场保存在它自己的栈里。CPU 进入异常时，硬件自动把
 *    R0-R3、R12、LR、PC、xPSR 压进当前栈；软件只需再保存 R4-R11。
 *  - PendSV 优先级设为最低（0xFF），保证“所有外设中断都处理完，
 *    最后才切换任务”，避免在中断处理中途把任务切走。
 *  - 启动第一个任务用 SVC：把 EXC_RETURN 从 0xFFFFFFF9（线程模式 + MSP）
 *    改成 0xFFFFFFFD（线程模式 + PSP），返回后 CPU 就从任务栈恢复现场。
 */
#include "task.h"
#include "port.h"
#include "stm32f103.h"

/* SHPR3 寄存器：PendSV 在 bits[23:16]，SysTick 在 bits[31:24]。
 * Cortex-M3 每个优先级字段只实现高 8 位，0xFF = 最低优先级。 */
#define portNVIC_PENDSV_PRI   (0xFFu << 16)
#define portNVIC_SYSTICK_PRI  (0xFFu << 24)

/* 任务函数正常返回后的停靠点（定义在 task.c） */
extern void prvTaskExitError(void);

uint32_t *pxPortInitialiseStack(uint32_t *pxTopOfStack,
                                TaskFunction_t pxCode,
                                void *pvParameters)
{
    /* 异常返回时硬件按“低地址 -> 高地址”恢复：
     * R4..R11, R0..R3, R12, LR, PC, xPSR。
     * 栈向下生长，因此从高地址往低地址填。 */
    *(--pxTopOfStack) = 0x01000000u;                 /* xPSR：Thumb 位必须为 1 */
    *(--pxTopOfStack) = (uint32_t)pxCode;            /* PC：任务入口地址 */
    *(--pxTopOfStack) = (uint32_t)prvTaskExitError;  /* LR：任务返回后停住 */
    pxTopOfStack -= 5;                               /* R12, R3, R2, R1 留空 */
    *pxTopOfStack = (uint32_t)pvParameters;          /* R0：任务参数 */
    pxTopOfStack -= 8;                               /* R4..R11 留空 */
    return pxTopOfStack;
}

void taskYIELD(void)
{
    /* 写 1 到 PENDSVSET 触发 PendSV；dsb/isb 让请求立即生效 */
    SCB->ICSR = SCB_ICSR_PENDSVSET;
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}

/* 启动第一个任务：重置 MSP、开中断，然后用 SVC 进入第一个任务。
 * 该函数不会返回。 */
__attribute__((naked)) static void prvStartFirstTask(void)
{
    __asm volatile (
        "movw r0, #0xED08\n\t"   /* r0 = SCB->VTOR (0xE000ED08) */
        "movt r0, #0xE000\n\t"
        "ldr  r0, [r0]\n\t"      /* 读 VTOR = 向量表基址 */
        "ldr  r0, [r0]\n\t"      /* 向量表第 0 项 = 初始 MSP（栈顶） */
        "msr  msp, r0\n\t"
        "cpsie i\n\t"
        "cpsie f\n\t"
        "dsb\n\t"
        "isb\n\t"
        "svc  0\n\t"             /* 触发 SVC，由 SVC_Handler 启动任务 */
        "bx   lr\n\t"
    );
}

void xPortStartScheduler(void)
{
    /* PendSV 与 SysTick 设成最低优先级：上下文切换最后执行 */
    SCB->SHPR3 |= (portNVIC_PENDSV_PRI | portNVIC_SYSTICK_PRI);

    prvStartFirstTask();   /* 启动首个任务，不会返回 */
}

/* SVC 异常：恢复第一个任务的现场，返回线程模式 + PSP */
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile (
        "movw r3, #:lower16:pxCurrentTCB\n\t"
        "movt r3, #:upper16:pxCurrentTCB\n\t"
        "ldr  r1, [r3]\n\t"        /* r1 = 当前 TCB */
        "ldr  r0, [r1]\n\t"        /* r0 = 任务栈顶 */
        "ldmia r0!, {r4-r11}\n\t"  /* 恢复 R4-R11 */
        "msr  psp, r0\n\t"         /* PSP 指向剩余栈帧 */
        "isb\n\t"
        "mov  r0, #0\n\t"
        "msr  basepri, r0\n\t"     /* 解除 basepri 屏蔽 */
        "orr  r14, #0xd\n\t"       /* EXC_RETURN: 0x...F9 -> 0x...FD */
        "bx   r14\n\t"
    );
}

/* PendSV 异常：保存当前任务 -> 选新任务 -> 恢复新任务 */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile (
        "mrs  r0, psp\n\t"         /* r0 = 当前任务栈指针 */
        "isb\n\t"
        "movw r3, #:lower16:pxCurrentTCB\n\t"
        "movt r3, #:upper16:pxCurrentTCB\n\t"
        "ldr  r2, [r3]\n\t"        /* r2 = 当前 TCB */
        "stmdb r0!, {r4-r11}\n\t"  /* 把 R4-R11 压入任务栈 */
        "str  r0, [r2]\n\t"        /* 保存新的栈顶到 TCB */
        "push {r14}\n\t"           /* 保存 EXC_RETURN（压在 MSP） */
        "cpsid i\n\t"              /* 关中断，保护切换过程 */
        "bl   vTaskSwitchContext\n\t"
        "cpsie i\n\t"
        "pop  {r14}\n\t"
        "movw r3, #:lower16:pxCurrentTCB\n\t"
        "movt r3, #:upper16:pxCurrentTCB\n\t"
        "ldr  r1, [r3]\n\t"        /* r1 = 新 TCB */
        "ldr  r0, [r1]\n\t"        /* r0 = 新任务栈顶 */
        "ldmia r0!, {r4-r11}\n\t"
        "msr  psp, r0\n\t"
        "isb\n\t"
        "bx   r14\n\t"             /* 硬件继续弹出 R0-R3,R12,LR,PC,xPSR */
    );
}

/* SysTick 钩子：每个 1ms 节拍检查是否需要切换任务 */
void rtos_systick_hook(void)
{
    if (xSchedulerRunning != 0u) {
        if (xTaskIncrementTick() != pdFALSE) {
            taskYIELD();
        }
    }
}
