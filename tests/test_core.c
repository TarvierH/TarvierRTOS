/* 宿主（PC）单元测试：验证链表与调度数据结构。
 * 用 MinGW gcc 编译运行，不需要硬件。
 * 用法：cd tests && make && ./test_core.exe
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "task.h"
#include "list.h"
#include "port.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                  \
    s_checks++;                                                           \
    if (!(cond)) {                                                        \
        printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        s_failures++;                                                     \
    }                                                                     \
} while (0)

/* ---- 宿主平台的栈初始化占位实现（真机版在 port.c） ---- */
uint32_t *pxPortInitialiseStack(uint32_t *pxTopOfStack,
                                TaskFunction_t pxCode,
                                void *pvParameters)
{
    /* 仅用于宿主测试：把参数和函数地址依次压栈 */
    *(--pxTopOfStack) = (uint32_t)(uintptr_t)pvParameters;
    *(--pxTopOfStack) = (uint32_t)(uintptr_t)pxCode;
    return pxTopOfStack;
}

/* ---- 宿主平台：调度器启动占位（真机版在 port.c，用 SVC 启动首个任务） ---- */
void xPortStartScheduler(void)
{
    /* 宿主测试不会真正启动调度器 */
}

/* ---- 测试任务函数（不会被真正调度，仅用作创建参数） ---- */
static void TaskA(void *p) { (void)p; }
static void TaskB(void *p) { (void)p; }
static void TaskC(void *p) { (void)p; }
static void TaskD(void *p) { (void)p; }

static void test_list_sorted_insert(void)
{
    printf("[1] 链表按值升序插入\n");
    List_t l;
    ListItem_t a, b, c;

    vListInitialise(&l);
    vListInitialiseItem(&a); a.xItemValue = 30;
    vListInitialiseItem(&b); b.xItemValue = 10;
    vListInitialiseItem(&c); c.xItemValue = 20;

    vListInsert(&l, &a);
    vListInsert(&l, &b);
    vListInsert(&l, &c);

    CHECK(l.uxNumberOfItems == 3);
    /* 升序：10 -> 20 -> 30 -> 哨兵 */
    CHECK(l.xListEnd.pxNext == &b);
    CHECK(b.pxNext == &c);
    CHECK(c.pxNext == &a);
    CHECK(a.pxNext == &l.xListEnd);
    /* 双向链接正确 */
    CHECK(l.xListEnd.pxPrevious == &a);
    CHECK(a.pxPrevious == &c);
    CHECK(c.pxPrevious == &b);
    CHECK(b.pxPrevious == &l.xListEnd);
    /* 哨兵值为最大值 */
    CHECK(l.xListEnd.xItemValue == UINT32_MAX);
}

static void test_list_insert_end_and_remove(void)
{
    printf("[2] 链表尾插、删除、游标\n");
    List_t l;
    ListItem_t a, b, c, d;

    vListInitialise(&l);
    vListInitialiseItem(&a);
    vListInitialiseItem(&b);
    vListInitialiseItem(&c);
    vListInitialiseItem(&d);

    vListInsertEnd(&l, &a);   /* a */
    vListInsertEnd(&l, &b);   /* a -> b */
    vListInsertEnd(&l, &c);   /* a -> b -> c */

    CHECK(l.xListEnd.pxNext == &a);
    CHECK(a.pxNext == &b);
    CHECK(b.pxNext == &c);
    CHECK(c.pxNext == &l.xListEnd);
    CHECK(l.uxNumberOfItems == 3);

    /* 删除中间节点 b */
    uint32_t n = uxListRemove(&b);
    CHECK(n == 2);
    CHECK(a.pxNext == &c);
    CHECK(c.pxPrevious == &a);
    CHECK(b.pvContainer == NULL);

    /* 再插 d，追加到尾部 */
    vListInsertEnd(&l, &d);
    CHECK(c.pxNext == &d);
    CHECK(d.pxNext == &l.xListEnd);

    /* 用游标从哨兵出发遍历一圈 */
    ListItem_t *it = l.pxIndex;      /* 哨兵 */
    int count = 0;
    for (it = it->pxNext; it != &l.xListEnd; it = it->pxNext) {
        count++;
    }
    CHECK(count == 3);
}

static void test_task_create_and_ready_list(void)
{
    printf("[3] 任务创建、就绪列表与位图\n");
    static uint32_t stackA[configMINIMAL_STACK_SIZE];
    static uint32_t stackB[configMINIMAL_STACK_SIZE];
    static uint32_t stackC[configMINIMAL_STACK_SIZE];
    static TCB_t tcbA, tcbB, tcbC;

    vTaskInitialiseCore();

    TaskHandle_t hA = xTaskCreateStatic(TaskA, "A", sizeof(stackA), NULL,
                                        1, stackA, &tcbA);
    TaskHandle_t hB = xTaskCreateStatic(TaskB, "B", sizeof(stackB), NULL,
                                        3, stackB, &tcbB);
    TaskHandle_t hC = xTaskCreateStatic(TaskC, "C", sizeof(stackC), NULL,
                                        2, stackC, &tcbC);

    CHECK(hA == &tcbA && hB == &tcbB && hC == &tcbC);
    CHECK(tcbA.eState == TASK_STATE_READY);
    CHECK(tcbA.uxBasePriority == 1);

    /* 位图 bit1、bit2、bit3 置位 -> 0b1110 = 14 */
    CHECK(uxReadyPriorityBitmap == 0x0Eu);

    /* 每个优先级的就绪列表各有一个任务 */
    CHECK(pxReadyTasksLists[1].uxNumberOfItems == 1);
    CHECK(pxReadyTasksLists[2].uxNumberOfItems == 1);
    CHECK(pxReadyTasksLists[3].uxNumberOfItems == 1);

    /* 最高优先级 = 3 -> 任务 B */
    TCB_t *top = pxGetHighestPriorityReadyTask();
    CHECK(top == &tcbB);

    /* 栈顶指针已被初始化（宿主 stub 压了两项） */
    CHECK(tcbA.pxTopOfStack != NULL);
}

static void test_same_priority_fifo(void)
{
    printf("[4] 同优先级任务按创建顺序排队\n");
    static uint32_t stackA[configMINIMAL_STACK_SIZE];
    static uint32_t stackD[configMINIMAL_STACK_SIZE];
    static TCB_t tcbA, tcbD;

    vTaskInitialiseCore();

    xTaskCreateStatic(TaskA, "A", sizeof(stackA), NULL, 2, stackA, &tcbA);
    xTaskCreateStatic(TaskD, "D", sizeof(stackD), NULL, 2, stackD, &tcbD);

    /* 同一优先级链表里，先创建的在前 */
    CHECK(pxReadyTasksLists[2].uxNumberOfItems == 2);
    CHECK(pxReadyTasksLists[2].xListEnd.pxNext == &tcbA.xStateListItem);
    CHECK(tcbA.xStateListItem.pxNext == &tcbD.xStateListItem);
    CHECK(tcbD.xStateListItem.pxNext == &pxReadyTasksLists[2].xListEnd);

    /* 位图只占一位 */
    CHECK(uxReadyPriorityBitmap == (1u << 2));
}

static void test_remove_updates_priority(void)
{
    printf("[5] 移出就绪列表后最高优先级更新\n");
    static uint32_t stackA[configMINIMAL_STACK_SIZE];
    static uint32_t stackB[configMINIMAL_STACK_SIZE];
    static uint32_t stackC[configMINIMAL_STACK_SIZE];
    static TCB_t tcbA, tcbB, tcbC;

    vTaskInitialiseCore();

    xTaskCreateStatic(TaskA, "A", sizeof(stackA), NULL, 1, stackA, &tcbA);
    xTaskCreateStatic(TaskB, "B", sizeof(stackB), NULL, 3, stackB, &tcbB);
    xTaskCreateStatic(TaskC, "C", sizeof(stackC), NULL, 2, stackC, &tcbC);

    CHECK(pxGetHighestPriorityReadyTask() == &tcbB);

    /* 移除最高优先级任务 B */
    prvRemoveTaskFromReadyList(&tcbB);
    CHECK((uxReadyPriorityBitmap & (1u << 3)) == 0);
    CHECK(pxReadyTasksLists[3].uxNumberOfItems == 0);
    CHECK(pxGetHighestPriorityReadyTask() == &tcbC); /* 现在是优先级 2 */

    /* 再移除 C，剩下 A */
    prvRemoveTaskFromReadyList(&tcbC);
    CHECK(pxGetHighestPriorityReadyTask() == &tcbA);

    /* 移除最后一个任务后位图清空 */
    prvRemoveTaskFromReadyList(&tcbA);
    CHECK(uxReadyPriorityBitmap == 0);
    CHECK(pxGetHighestPriorityReadyTask() == NULL);
}

static void test_lowest_priority(void)
{
    printf("[6] 优先级 0（最低优先级）\n");
    static uint32_t stackA[configMINIMAL_STACK_SIZE];
    static TCB_t tcbA;

    vTaskInitialiseCore();
    xTaskCreateStatic(TaskA, "A", sizeof(stackA), NULL, 0, stackA, &tcbA);

    /* clz(1) = 31 -> 最高优先级 0 */
    CHECK(uxReadyPriorityBitmap == 1u);
    CHECK(pxGetHighestPriorityReadyTask() == &tcbA);
}

int main(void)
{
#ifdef _WIN32
    /* Windows 控制台默认使用 GBK(936) 代码页，而本程序输出 UTF-8，
     * 不设置的话中文会显示为乱码。改为 UTF-8 后正常显示。 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    test_list_sorted_insert();
    test_list_insert_end_and_remove();
    test_task_create_and_ready_list();
    test_same_priority_fifo();
    test_remove_updates_priority();
    test_lowest_priority();

    printf("\n%d checks, %d failure(s)\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
