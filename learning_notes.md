# FreeRTOS 内核学习笔记

## 哨兵节点（`xListEnd`）的作用

> 哨兵 = 一个永远存在、值为最大值（`UINT32_MAX`）、不计入 `uxNumberOfItems` 的"虚拟节点"。
> 它让空表和非空表长得一样，插入、删除、查找都不需要边界特判，还能标记链表的头尾。

相关代码：[rtos/core/list.h](../rtos/core/list.h)、[rtos/core/list.c](../rtos/core/list.c)

### 四个职责

#### 1. 让链表永远"闭合"，空表不需要特判

初始化时哨兵自己指向自己：

```c
pxList->xListEnd.pxNext     = &pxList->xListEnd;
pxList->xListEnd.pxPrevious = &pxList->xListEnd;
```

链表里永远有一个有效节点，所以删除操作可以直接写：

```c
pxItem->pxNext->pxPrevious = pxItem->pxPrevious;
pxItem->pxPrevious->pxNext = pxItem->pxNext;
```

完全不需要判断 `pxNext == NULL` 之类的空表分支。

#### 2. 作为排序查找的"终止值"

哨兵的 `xItemValue` 固定为 `UINT32_MAX`（最大值）。`vListInsert` 的查找循环从哨兵出发：

```c
for (pxIterator = &pxList->xListEnd;
     pxIterator->pxNext->xItemValue <= xValue;
     pxIterator = pxIterator->pxNext) {
    /* 空循环 */
}
```

任何真实元素的值都 ≤ `UINT32_MAX`，走到哨兵时循环必然终止。
哨兵同时充当"起始点"和"终点"，无需判断"找遍还没找到"。

#### 3. 定义链表的"头"和"尾"

- 链表头部 = `pxList->xListEnd.pxNext`
- 链表尾部 = `pxList->xListEnd.pxPrevious`
- 追加到尾部 = 插到哨兵之前（`vListInsertEnd`）

取最高优先级任务就是用 `xListEnd.pxNext` 拿到第一个元素。

#### 4. 提供一个稳定的"锚点"

`pxIndex` 游标初始指向哨兵。时间片轮转时从 `pxIndex` 出发取下一个节点；
即使链表删光，`pxIndex` 仍指向哨兵这个合法节点，不会悬空。

### 图示

```
初始（空表）：                    插入 A、B 后：
   ┌──────────┐                     ┌──────────┐
   │ xListEnd │◄──┐              ┌─►│ xListEnd │──┐
   └──────────┘   │              │  └──────────┘  │
      ▲  │        │              │        ▲       │
      │  └────────┘              │        │       ▼
      └──自己指向自己             │      ┌───┐   ┌───┐
                                 │  ┌───┤ B │◄──┤ A │
                                 │  │   └───┘   └───┘
                                 │  │            ▲
                                 │  └────────────┘
                                 │
                                 └ 哨兵始终是闭合的环
```

### 相关命名约定

| 前缀 | 含义 |
|------|------|
| `u`  | unsigned（无符号） |
| `x`  | 随平台变化的非标准类型（`BaseType_t` / `TickType_t`） |
| `ux` | `u` + `x`，无符号的基础类型（`UBaseType_t`） |
| `p`  | 指针 |
| `v`  | void（函数名前缀） |
| `prv`| private，静态内部函数前缀 |

- `xItemValue`：类型是 `TickType_t`（时间/排序值），用 `x` 前缀。
- `uxNumberOfItems`：类型是 `UBaseType_t`（无符号计数），用 `ux` 前缀。
- 两者在 32 位平台上都是 `uint32_t`，但语义不同（时间 vs 个数），所以前缀不同。

## 上下文切换（PendSV + SVC）

> 切换任务 = 保存当前任务的 CPU 现场，再恢复另一个任务的现场。
> 现场存在各任务的栈里；切换由 PendSV（最低优先级异常）完成。

相关代码：[rtos/port/port.c](../rtos/port/port.c)、
[startup_stm32f103.s](../startup_stm32f103.s)（向量表第 14 项）

### PendSV 是什么

- 全称 **Pendable Service Call**，Cortex-M 内置的**异常 14**，专为 RTOS 设计。
- "Pend" = 挂起：向它发起请求后**先排队**，等 CPU 忙完更高优先级的事才响应。
- 它是所有任务切换的**统一入口**，由软件触发。

| 异常 | 作用 |
|------|------|
| SysTick（15） | 系统节拍，1ms 时基 |
| **PendSV（14）** | **执行上下文切换** |
| SVC（11） | 启动第一个任务 |

### 它怎么触发

只需向 `SCB->ICSR` 的 PENDSVSET 位写 1，PendSV 就被挂起：

```c
void taskYIELD(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET;   /* 写 1 → 挂起 PendSV */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}
```

### 完整流程：挂起 → 响应 → 切换

```
SysTick 到点 → rtos_systick_hook() → 需要切换？ → taskYIELD()（挂起 PendSV）
    ↓
CPU 忙不忙？
├─ 不忙 → 立刻响应 PendSV
└─ 忙（高优先级中断中）→ PendSV 排队，等所有中断处理完
    ↓
PendSV_Handler：保存当前任务 → vTaskSwitchContext() 选新任务 → 恢复新任务
```

### 为什么用 PendSV 而不是直接在 SysTick 里切？

- SysTick 是普通外设中断，如果它打断了一个更高优先级的中断，直接切任务
  会把"中断处理到一半"的任务切走，造成嵌套混乱。
- PendSV 优先级设成最低（0xFF），所有中断都处理完才轮到它，此时切换最安全。

### NVIC 优先级：分组、抢占优先级、子优先级

> STM32F103 每个中断的优先级是 8 位字段，但只实现高 4 位（16 个等级）。
> "分组"把这 4 位切成 **抢占优先级 + 子优先级** 两段。

#### 优先级分组（PRIGROUP）

```
4 位优先级字段：
┌──────────┬──────────┐
│ 抢占优先级 │  子优先级  │
└──────────┴──────────┘
   高位      低位
```

| 分组 | 抢占位数 | 子优先级位数 | 抢占级别 | 子级别 |
|------|---------|------------|---------|-------|
| 分组4 | 4 | 0 | 16 | 1 |
| 分组3 | 3 | 1 | 8 | 2 |
| 分组2 | 2 | 2 | 4 | 4 |
| 分组1 | 1 | 3 | 2 | 8 |
| 分组0 | 0 | 4 | 1 | 16 |

#### 抢占优先级 vs 子优先级

| | 抢占优先级 | 子优先级 |
|---|-----------|---------|
| 作用 | 决定谁**抢占**谁 | 同时挂起时决定**谁先服务** |
| 能否嵌套打断 | ✅ 能 | ❌ 不能 |
| 生效时机 | 任何时刻 | 仅"同抢占优先级 + 同时挂起" |
| 别称 | preemption priority | sub-priority / 响应优先级 |

- **抢占优先级**：数值越小越紧急；高抢占优先级可打断低抢占优先级（嵌套中断）。
- **子优先级**：只在抢占优先级相同且同时挂起时决定先后，不能抢占。

#### FreeRTOS 为什么用"分组 4"

- 分组 4 = 4 位全给抢占优先级、0 位子优先级，16 级抢占最灵活。
- RTOS 里的"优先级"就是抢占优先级；任务调度用的是另一套软件优先级，别混。
- 对应 [rtos/port/port.c](../rtos/port/port.c)：

```c
#define portNVIC_PENDSV_PRI   (0xFFu << 16)  /* PendSV = 最低优先级 */
#define portNVIC_SYSTICK_PRI  (0xFFu << 24)  /* SysTick = 最低优先级 */
```

`0xFF` 数值最大 = 优先级最低，所以所有外设中断都能抢占 PendSV/SysTick，
保证切换发生在所有中断处理完之后。

### 一次切换发生了什么

1. 硬件自动保存：进入异常时，CPU 自动把 R0-R3、R12、LR、PC、xPSR 压进
   当前任务栈（PSP）。
2. 软件保存：PendSV 里 `stmdb r0!, {r4-r11}` 再压入 R4-R11（AAPCS 规定
   这些由被调用方保存，硬件不帮你压）。
3. 选下一个任务：调用 `vTaskSwitchContext()`（按优先级 + 同优先级轮转）。
4. 恢复：`ldmia r0!, {r4-r11}` 弹出新任务的 R4-R11，`msr psp, r0` 切换 PSP，
   `bx r14` 用 EXC_RETURN 让硬件继续弹出剩下的寄存器。

### 启动第一个任务为什么用 SVC

第一次没有"上一个任务"可保存，只需要"恢复"。SVC 异常返回时把 EXC_RETURN
从 `0xFFFFFFF9`（线程模式 + MSP）改成 `0xFFFFFFFD`（线程模式 + PSP），
CPU 一返回就开始从任务栈恢复现场——任务跑起来了。

### 栈帧布局（低地址在下）

```
[R4..R11][R0..R3][R12][LR][PC][xPSR]
```

- PC 填任务函数入口；xPSR 的 Thumb 位必须为 1（Cortex-M 只执行 Thumb 指令）。
- LR 填 `prvTaskExitError`：任务函数万一 return，就停在那里。
- R0 填任务参数。

## 侵入式链表 vs 非侵入式链表

> 侵入式 = 链表节点**内嵌**在对象里；非侵入式 = 节点是独立结构，用指针**指向**数据。

### 非侵入式（教科书）

## 侵入式链表里 free 对象导致的 use-after-free

### 问题

遍历侵入式链表时，删除节点并 `free(owner)` 之后，循环的步进语句
`pxIt = pxIt->pxNext` 读取了已释放的内存：

```c
for (ListItem_t *pxIt = list->xListEnd.pxNext;
     pxIt->xItemValue < UINT32_MAX;
     pxIt = pxIt->pxNext) {          // ← free 之后这里读 pxIt->pxNext
    Student_t *student = (Student_t *)pxIt->pvOwner;
    if (student->xId % 2 == 1) {
        vListDel(&student->xListItem);
        free(student);               // ← 释放了整个 student 块
    }
}
```

### 根本原因：节点和对象是同一块内存

侵入式链表只有**一次分配**：

```c
student = malloc(sizeof(*student));          // 一块内存，同时装数据 + 节点
vListAdd(&student->xListItem, &list);        // 链进去的是 student 内部的字段
```

所以 `pxIt = &student->xListItem` 指向 student 块**内部**；`free(student)`
释放的是整块，包括 `pxNext` 字段所在的存储位置：

```
free() 释放的整块内存
┌──────────────────────────────────────────────┐
│ xId │ cName[100] │ xListItem                 │
│     │            │  ├ pxPrevious              │
│     │            │  ├ pxNext   ← 就在这里     │
│     │            │  ├ pvOwner  (=块首地址)    │
│     │            │  └ xItemValue              │
└──────────────────────────────────────────────┘
                      ↑
              pxIt = &student->xListItem
```

释放后 `pxIt` 变成悬空指针，`pxIt->pxNext` 读的就是已释放内存。

### 易混淆点：pvOwner 和释放没关系

`pvOwner` 只是存在节点里的一个**指针值**（= 块首地址），它本身不参与释放。
真正的问题不是"pvOwner 被释放了"，而是"**节点本身（pxNext/pxPrevious 等字段）
就存放在被释放的 student 块里**"。方向和直觉相反：

- 不是 `student` 是 `pxIt` 的字段；
- 而是 `xListItem` 是 `student` 的字段，`pxIt` 指向 student 块内部。

### 为什么"碰巧能跑"

`free()` 只是把块归还给堆管理器并标记，**不会立刻清零或归还 OS**。旧字节还
保留着原来的 `pxNext`/`pxPrevious` 值，所以循环暂时走通。一旦堆管理器复用
这块内存（后续 `malloc`），`pxIt->pxNext` 就变成垃圾值，程序崩溃或行为错乱。

### 正确写法：先保存 next 再删除

```c
for (ListItem_t *pxIt = list->xListEnd.pxNext;
     pxIt->xItemValue < UINT32_MAX; ) {
    Student_t *student = (Student_t *)pxIt->pvOwner;
    ListItem_t *pxNext = pxIt->pxNext;   // ① 先记住下一个节点
    if (student->xId % 2 == 1) {
        vListDel(&student->xListItem);
        free(student);                   // ② 安全释放当前
    }
    pxIt = pxNext;                       // ③ 用保存的指针继续
}
```

若不想释放（演示程序退出时无所谓），只删节点不 `free` 也能避免 UB，因为
`vListDel` 后节点的 next/prev 字段仍然有效。

```c
typedef struct node {
    TCB_t       *data;    /* 指向数据 */
    struct node *next;
} Node_t;
```

- 每次插入要 `malloc` 一个 Node（可能失败、有碎片、耗时不确定）。
- 从节点到数据要跳一次指针（`node->data`）。

### 侵入式（FreeRTOS / Linux 内核）

```c
typedef struct list_item {
    struct list_item *pxNext, *pxPrevious;
    void *pvOwner;          /* 指回拥有者 TCB */
    void *pvContainer;      /* 所属链表 */
    uint32_t xItemValue;
} ListItem_t;

typedef struct tcb {
    ListItem_t xStateListItem;   /* 节点长在 TCB 里 */
    ListItem_t xEventListItem;
    /* ... */
} TCB_t;
```

- 插入直接链入内嵌节点，**零 malloc**。
- 从节点回查对象：`pvOwner`（Linux 用 `container_of` 反推地址）。

### 为什么选侵入式

1. 零动态分配：节点随对象一起静态存在，无失败、无碎片、确定性好。
2. 一个对象可内嵌多个节点 → **同时挂多条链表**。
3. 删除 O(1)：节点地址已知（就在对象里），无需遍历。
4. 缓存友好：节点和数据在同一块内存。

## TCB 的两个链表节点

| 字段 | 挂到哪些链表 | 回答的问题 |
|------|-------------|-----------|
| `xStateListItem` | 就绪列表 / 挂起列表 / 延时列表 | 运行状态是什么？ |
| `xEventListItem` | 队列、信号量等事件等待列表 | 在等什么事件？ |

一个节点只有一对 next/prev，不能同时待在两个链表里；但任务"带超时等队列"时
要**同时**挂在"队列等待列表"和"延时列表"上，所以需要两个节点。哪个条件先满足，
就把任务从两条链表都摘下来放回就绪。

## `vTaskDelay` 与延时列表（里程碑 2）

> 忙等 vs 阻塞：忙等是"任务还在就绪列表，只是空转"；`vTaskDelay` 是"把任务从
> 就绪列表摘掉，真正阻塞，CPU 去跑别的任务"。这才是 RTOS 的意义。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的
`vTaskDelay()`、`xTaskIncrementTick()`，[app/main.c](../app/main.c)。

### 核心思想：存"绝对唤醒时刻"，按值排序

每个阻塞任务记录一个绝对时刻：

```c
TickType_t xTimeToWake = xTickCount + ulTicksToDelay;  /* 不是"还差多少"，而是"什么时候醒" */
```

延时列表按这个值**升序**插入（用 `vListInsert`），所以"最早醒的任务"永远在表头。
每个 tick 只需检查表头是否到期即可，O(1)。

```
延时列表：[tick=1000] → [tick=1500] → [tick=2000] → 哨兵
            ↑表头：最早要醒的
```

### vTaskDelay 的五个步骤（顺序不能乱）

```c
void vTaskDelay(uint32_t ulTicksToDelay)
{
    prvRemoveTaskFromReadyList(pxCurrentTCB);          /* ① 摘出就绪列表 */
    pxCurrentTCB->eState = TASK_STATE_BLOCKED;         /* ② 改状态 */
    TickType_t xTimeToWake = xTickCount + ulTicksToDelay;  /* ③ 算唤醒时刻 */
    pxCurrentTCB->xStateListItem.xItemValue = xTimeToWake; /* ④ 先设排序值 */
    vListInsert(&xDelayedTaskList, &pxCurrentTCB->xStateListItem); /* ④ 再排序插入 */
    taskYIELD();                                       /* ⑤ 让出 CPU */
}
```

- 用的是 **`xStateListItem`**：任务原来挂在就绪列表，现在"搬家"到延时列表，
  同一个节点从一条链表换到另一条。
- **必须先设 `xItemValue` 再 `vListInsert`**——它是按值排序插入的。

### 唤醒逻辑：每个 tick 检查表头

```c
BaseType_t xTaskIncrementTick(void)
{
    xTickCount++;    /* ① 时间前进 */

    while (xDelayedTaskList.uxNumberOfItems > 0u) {
        ListItem_t *pxHead = xDelayedTaskList.xListEnd.pxNext;  /* 表头 = 最早醒 */
        TCB_t *pxTask = (TCB_t *)pxHead->pvOwner;               /* 节点反查任务 */

        if ((TickType_t)pxHead->xItemValue > xTickCount) {
            break;    /* 表头都没到，后面的更晚，停止 */
        }

        uxListRemove(pxHead);                /* 摘出延时列表（吃节点） */
        pxTask->eState = TASK_STATE_READY;
        prvAddTaskToReadyList(pxTask);       /* 挂回就绪列表（吃任务） */

        xSwitchRequired = pdTRUE;            /* 有任务被唤醒，要切换 */
    }

    /* 原有轮转判断保留 */
    if (pxReadyTasksLists[pxCurrentTCB->uxPriority].uxNumberOfItems > 1u) {
        xSwitchRequired = pdTRUE;
    }
    return xSwitchRequired;
}
```

- 用 `while` 不用 `if`：同一时刻可能有多个任务同时到期。
- 判断到期：`xItemValue <= xTickCount`（唤醒时刻已到或已过）。

### 最容易犯的错：节点 vs 任务类型混用

| API | 参数类型 | 传什么 |
|-----|---------|--------|
| `vListInsert` / `vListInsertEnd` / `uxListRemove` | `ListItem_t *` | **节点** |
| `prvAddTaskToReadyList` / `prvRemoveTaskFromReadyList` | `TCB_t *` | **任务** |

典型错误：`prvAddTaskToReadyList(pxHead)` —— 把节点当任务传。
后果：函数内部读 `pxTCB->uxPriority` 时，实际读到的是 `ListItem_t` 的 `xItemValue`
（唤醒时刻），导致按错误优先级索引就绪列表数组 → 越界 → 崩溃。

正确关系：`pxHead == &pxTask->xStateListItem`，靠 `pxHead->pvOwner` 反查回任务。

### 任务结构从"轮询"翻转为"干完就睡"

```c
/* 忙等（旧）：一直空转，到点才干活 */
for (;;) {
    if (现在 - 上次 >= 500) { 做事情; }
}

/* 阻塞（新）：干完活就睡，CPU 去跑别人 */
for (;;) {
    做事情;
    vTaskDelay(500);
}
```

### `vTaskDelayUntil`：精确周期延时（M3）

> `vTaskDelay` 从"现在"延时 → 周期 = 干活耗时 + 延时，会漂移；
> `vTaskDelayUntil` 对齐到绝对时刻 → 周期恒定，不漂移。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的
`vTaskDelayUntil()`、`xTaskGetTickCount()`。

#### 问题：vTaskDelay 的周期漂移

```c
for (;;) {
    干活();            /* 假设花 0.2ms */
    vTaskDelay(500);   /* 从"现在"再睡 500ms */
}
/* 实际周期 = 0.2 + 500 = 500.2ms，干活耗时每次累加 → 周期忽长忽短 */
```

#### 解法：对齐到绝对唤醒时刻

```c
TickType_t xLastWakeTime = xTaskGetTickCount();  /* 只初始化一次 */
for (;;) {
    干活();
    vTaskDelayUntil(&xLastWakeTime, 500);  /* 等到"上次+500"这个绝对时刻 */
}
/* 下次唤醒 = 上次唤醒时刻 + 500，雷打不动 → 周期恒定 500ms */
```

#### 实现要点

```c
void vTaskDelayUntil(TickType_t *pxPreviousWakeTime, uint32_t ulTimeIncrement)
{
    TickType_t xTimeToWake = *pxPreviousWakeTime + ulTimeIncrement;

    if (xTimeToWake <= xTickCount) {
        /* 睡过头：唤醒时刻已过，重置基准为"现在"，不再追旧时间 */
        xTimeToWake = xTickCount + 1u;
        *pxPreviousWakeTime = xTickCount;
    } else {
        /* 正常：基准前进一个周期 */
        *pxPreviousWakeTime = xTimeToWake;
    }
    prvDelayCurrentTask(xTimeToWake);
}
```

#### 指针参数的双向作用

- 传 `TickType_t *pxPreviousWakeTime` 而不是传值，因为函数要**读旧值 + 写回新值**。
- 调用者只初始化一次 `xLastWakeTime = xTaskGetTickCount();`，之后每次调用自动更新。

#### 重构：抽出公共阻塞函数

`vTaskDelay` 和 `vTaskDelayUntil` 的"阻塞"部分相同，只有"唤醒时刻怎么算"不同：

```c
static void prvDelayCurrentTask(TickType_t xTimeToWake)
{
    prvRemoveTaskFromReadyList(pxCurrentTCB);
    pxCurrentTCB->eState = TASK_STATE_BLOCKED;
    pxCurrentTCB->xStateListItem.xItemValue = xTimeToWake;
    vListInsert(&xDelayedTaskList, &pxCurrentTCB->xStateListItem);
    taskYIELD();
}

void vTaskDelay(uint32_t ulTicksToDelay)
{
    prvDelayCurrentTask(xTickCount + ulTicksToDelay);
}
```

## 挂起 / 恢复（`vTaskSuspend` / `vTaskResume`，M3）

> 挂起 = 被动冻结，不参与调度，必须显式 `vTaskResume` 才能恢复；
> 阻塞 = 主动等待，条件满足自动醒。两者是不同状态。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的
`vTaskSuspend()`、`vTaskResume()`，[app/main.c](../app/main.c) 的演示。

### 状态对比（回顾）

| | BLOCKED 阻塞 | SUSPENDED 挂起 |
|---|---|---|
| 谁发起 | 任务自己主动等待 | 别的任务（或自己）显式挂起 |
| 恢复方式 | 条件满足自动醒 | 必须 `vTaskResume` |
| 挂在哪 | 延时列表 | 挂起列表 |

### 挂起列表：只需一条，不分优先级

挂起的任务完全不参与调度（优先级没意义），所以是一条独立链表：

```c
static List_t xSuspendedTaskList;   /* task.c 内部静态，外部看不到 */
/* vTaskInitialiseCore() 里 vListInitialise(&xSuspendedTaskList); */
```

### 实现

```c
void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    /* NULL 或自己 = 挂起当前任务 */
    TCB_t *pxTCB = (xTaskToSuspend == NULL) ? pxCurrentTCB : xTaskToSuspend;

    if (pxTCB->eState == TASK_STATE_READY) {        /* 只挂起就绪任务 */
        prvRemoveTaskFromReadyList(pxTCB);
        pxTCB->eState = TASK_STATE_SUSPENDED;
        vListInsertEnd(&xSuspendedTaskList, &pxTCB->xStateListItem);

        if (pxTCB == pxCurrentTCB) {
            taskYIELD();        /* 挂起的是自己 → 必须让出 CPU */
        }
    }
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
    TCB_t *pxTCB = xTaskToResume;

    if (pxTCB->eState == TASK_STATE_SUSPENDED) {    /* 只有挂起的任务能恢复 */
        uxListRemove(&pxTCB->xStateListItem);
        pxTCB->eState = TASK_STATE_READY;
        prvAddTaskToReadyList(pxTCB);

        if (pxTCB->uxPriority > pxCurrentTCB->uxPriority) {
            taskYIELD();        /* 恢复高优先级任务 → 立刻让出 CPU */
        }
    }
}
```

### 关键点

1. **挂起和恢复都用 `xStateListItem`**：同一个节点在"就绪列表 ↔ 挂起列表"之间搬家。
2. **`eState` 是守卫条件**：防止"恢复一个没挂起的任务"或"挂起一个阻塞中的任务"。
3. **挂起自己必须 `taskYIELD`**：否则一个已摘出就绪列表的任务永远霸占 CPU。
4. **恢复高优先级任务要 `taskYIELD`**：立即让新就绪的高优先级任务得到 CPU。
5. **句柄的实际用途**：`xLedTaskHandle = xTaskCreateStatic(...)` 保存句柄，
   别的任务用它 `vTaskSuspend(xLedTaskHandle)` 跨任务控制。

### taskYIELD 的功能

```c
void taskYIELD(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET;   /* 往 bit28 写 1 → 挂起 PendSV */
    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
}
```

- = "请求调度器切走我"：只**挂起 PendSV**，不立即切换。
- 等所有更高优先级中断处理完，PendSV 才执行"保存现场 → 选新任务 → 恢复现场"。
- 挂起自己 / 阻塞自己 / 恢复高优先级任务时都必须调用，否则当前任务霸占 CPU。

## 修改优先级（`vTaskPrioritySet`，M3）

> 修改优先级 = 把任务从"旧优先级链表"搬到"新优先级链表"，位图自动跟着维护。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的 `vTaskPrioritySet()`。

### 实现

```c
void vTaskPrioritySet(TaskHandle_t xTask, uint32_t uxNewPriority)
{
    TCB_t *pxTCB;
    uint32_t uxCurrentPriority;

    if (uxNewPriority >= configMAX_PRIORITIES) {
        return;   /* 非法优先级 */
    }

    pxTCB = (xTask == NULL) ? pxCurrentTCB : xTask;
    uxCurrentPriority = pxTCB->uxPriority;

    if (uxCurrentPriority == uxNewPriority) {
        return;   /* 没变 */
    }

    if (pxTCB->eState == TASK_STATE_READY) {
        prvRemoveTaskFromReadyList(pxTCB);   /* 旧链表摘出（自动维护位图） */
        pxTCB->uxPriority = uxNewPriority;
        pxTCB->uxBasePriority = uxNewPriority;
        prvAddTaskToReadyList(pxTCB);        /* 新链表挂入（自动置位图） */
    } else {
        pxTCB->uxPriority = uxNewPriority;
        pxTCB->uxBasePriority = uxNewPriority;
    }

    if (pxTCB == pxCurrentTCB) {
        if (uxCurrentPriority > uxNewPriority) taskYIELD();  /* 降低自己 */
    } else {
        if (uxNewPriority > pxCurrentTCB->uxPriority) taskYIELD();  /* 抬高别人 */
    }
}
```

### 关键点

1. 阻塞/挂起的任务改优先级**不搬家**（不在就绪列表），只改字段，回就绪时自动进新链表。
2. `uxBasePriority` 一起改：用户改的是"基础优先级"，无继承时 `uxPriority` 跟着变。
3. "改自己"和"改别人"分开判断让不让出 CPU：
   - 降低自己 → 可能有更高就绪任务，yield；
   - 抬高别人 → 它比当前任务更高，yield 给它。

### 两个优先级字段

| 字段 | 含义 | 用途 |
|------|------|------|
| `uxBasePriority` | 基础优先级（用户设定的"真身"） | 记录原值，供恢复 |
| `uxPriority` | 当前有效优先级（调度器实际用） | 可被优先级继承临时抬高 |

- 平时两者相等；实现互斥锁的**优先级继承**后，`uxPriority` 会被临时抬高，
  释放锁时恢复成 `uxBasePriority`。

## 删除任务（`vTaskDelete`，M3）

> 删除 = 按状态找到所在链表摘除节点 + 标记 DELETED。静态分配的内存不释放。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的 `vTaskDelete()`。

### 实现

```c
void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    TCB_t *pxTCB = (xTaskToDelete == NULL) ? pxCurrentTCB : xTaskToDelete;

    if (pxTCB == NULL) return;

    switch (pxTCB->eState) {                 /* 按状态摘除 */
        case TASK_STATE_READY:     prvRemoveTaskFromReadyList(pxTCB); break;
        case TASK_STATE_BLOCKED:   uxListRemove(&pxTCB->xStateListItem); break;
        case TASK_STATE_SUSPENDED: uxListRemove(&pxTCB->xStateListItem); break;
        default: break;
    }

    pxTCB->eState = TASK_STATE_DELETED;

    if (pxTCB == pxCurrentTCB) {
        taskYIELD();   /* 删自己 → 让出 CPU，之后不会回来 */
    }
}
```

### 关键点

1. 三种状态恰好对应三种链表，都用 `xStateListItem`。
2. 静态分配的内存不 free（也 free 不了），只摘链表 + 改状态。
3. 删自己必须 yield，删别人不用。

## 队列（Queue，M4）

> 队列 = **环形缓冲区 + 两张等待列表**，是 IPC 的地基。
> 信号量 = 队列的特殊用法；互斥锁 = 特殊信号量 + 优先级继承。

相关代码：[rtos/ipc/queue.h](../rtos/ipc/queue.h)、[rtos/ipc/queue.c](../rtos/ipc/queue.c)。

### 队列结构体

```c
typedef struct queue {
    uint8_t *pcHead;            /* 存储区起始地址 */
    uint8_t *pcWriteTo;         /* 下一个写入位置 */
    uint8_t *pcReadFrom;        /* 下一个读取位置 */
    uint32_t uxLength;          /* 最大元素个数 */
    uint32_t uxItemSize;        /* 每个元素的字节数 */
    volatile uint32_t uxMessagesWaiting;  /* 当前元素个数 */
    List_t xTasksWaitingToSend;      /* 等待发送（队列满时） */
    List_t xTasksWaitingToReceive;   /* 等待接收（队列空时） */
} Queue_t;
```

### 三个核心设计

1. **按值复制**：队列存数据本身（`memcpy`），不是指针，所以要 `uxItemSize`。
2. **环形缓冲区**：读写指针走到末尾绕回开头（`pcWriteTo >= pcHead + uxLength*uxItemSize` 时回 `pcHead`）。
3. **两张等待列表**：满 → 等发送；空 → 等接收。这就是 `xEventListItem` 的用途。

### 静态创建：不 malloc

`xQueueCreateStatic` 的存储区和结构体都由调用者传入（全局变量），函数只初始化。

### 阶段 1：不阻塞（已实现）

- 满则 `send` 失败、空则 `receive` 失败。
- **坑**：满判断必须用 `uxLength`（最大个数），不是 `uxItemSize`（字节数）！

### 阶段 2：带超时阻塞

#### 阻塞 = 同时挂两条链表

`vTaskPlaceOnEventList` 把当前任务：

```c
vListInsertEnd(pxEventList, &pxCurrentTCB->xEventListItem);   /* ① 事件列表 */
if (xTicksToWait != portMAX_DELAY) {
    pxCurrentTCB->xStateListItem.xItemValue = xTickCount + xTicksToWait;
    vListInsert(&xDelayedTaskList, &pxCurrentTCB->xStateListItem);  /* ② 延时列表 */
}
```

- 事件列表用 `xEventListItem`（等"空间/数据"）；
- 延时列表用 `xStateListItem`（等"超时"）。
- 这正是 TCB 需要两个链表节点的原因。

#### 唤醒有两条路径，两条都要清理

| 路径 | 谁触发 | 清理 |
|------|--------|------|
| 超时 | `xTaskIncrementTick` | 延时列表 + **事件列表**（要加 `pvContainer != NULL` 检查） |
| 事件 | `xTaskRemoveFromEventList` | 事件列表 + 延时列表（同样检查） |

#### 阻塞 API 的固定套路：`for(;;)` 循环

```c
for (;;) {
    if (条件满足) { 干活; 唤醒等待的另一方; return pdTRUE; }
    if (xTicksToWait == 0) return pdFALSE;            /* 不等待 */
    if (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait)) return pdFALSE;  /* 超时 */
    vTaskPlaceOnEventList(事件列表, xTicksToWait);    /* 阻塞，醒来回到循环头 */
}
```

**关键**：醒来后必须回到循环头重新判断"是有空间还是超时"，
阻塞只是循环里的一步，不是函数的终点。

#### 超时剩余时间：`TimeOut_t`

```c
BaseType_t xTaskCheckForTimeOut(TimeOut_t *pxTimeOut, TickType_t *pxTicksToWait)
{
    if (*pxTicksToWait == portMAX_DELAY) return pdFALSE;   /* 无限等待 */
    TickType_t xElapsed = xTickCount - pxTimeOut->xTimeOnEntering;
    if (xElapsed >= *pxTicksToWait) { *pxTicksToWait = 0; return pdTRUE; }
    else { *pxTicksToWait -= xElapsed; }
    pxTimeOut->xTimeOnEntering = xTickCount;   /* ← 基准必须同步更新为"现在" */
    return pdFALSE;
}
```

- `xTicksToWait` 存**剩余时间**，是相对"现在"的，所以基准 `xTimeOnEntering`
  必须跟着更新；不更新会把已等过的时间重复扣，导致提前误判超时。

#### 实现时的 4 个坑（实战记录）

1. **漏 `for(;;)` 循环**：醒来后函数直接掉到结尾，返回垃圾值。
2. **`vTaskPlaceOnEventList` 第一个参数传错**：要传**队列自己的等待列表**
   （`xTasksWaitingToSend` / `xTasksWaitingToReceive`），不是全局 `xDelayedTaskList`。
3. **第二个参数传指针**：签名是 `TickType_t`（值），不是 `TickType_t *`。
4. **成功后没唤醒等待的另一方**：发送成功要唤醒等待接收者，接收成功要唤醒等待发送者。

#### 上板验证（生产-消费者握手）

队列容量 2，生产者 200ms 发一次、消费者 1s 收一次，都用 `portMAX_DELAY` 阻塞：

- 输出节奏是 `send ok: 1,2` → `recv: 1` → `send ok: 3` → `recv: 2` ...
- 生产者被"压"到 1s 发一个（阻塞在满队列上），证明阻塞机制在真板工作。

## 信号量（Semaphore，M5）

> 信号量 = **没有数据、只计数的队列**。昨天做的队列阻塞机制被信号量原样复用，
> take/give 就是 receive/send 的别名。

相关代码：[rtos/ipc/semaphore.h](../rtos/ipc/semaphore.h)、
[rtos/ipc/semaphore.c](../rtos/ipc/semaphore.c)。

### 核心映射

| 队列 | 信号量 |
|------|--------|
| 存数据，`uxItemSize > 0` | 不存数据，`uxItemSize == 0` |
| `uxMessagesWaiting` = 元素个数 | `uxMessagesWaiting` = 计数值 |
| send = 放入数据 | **give = 计数 +1** |
| receive = 取出数据 | **take = 计数 -1** |

- **二值信号量** = `uxLength == 1`（计数 0 或 1）。
- **计数信号量** = `uxLength == N`（计数 0~N）。

### 关键改动：queue.c 支持 `uxItemSize == 0`

信号量没有数据，send/receive 里必须跳过 memcpy 和指针移动：

```c
if (xQueue->uxItemSize != 0u) {
    memcpy(...);
    pcWriteTo += uxItemSize;
    if (pcWriteTo >= pcHead + uxLength * uxItemSize) pcWriteTo = pcHead;
}
```

这样 `uxItemSize == 0` 时，send/receive 就纯粹是 `uxMessagesWaiting++/--` 的计数操作。

### 封装（极薄一层）

```c
typedef QueueHandle_t SemaphoreHandle_t;   /* 信号量 = 队列句柄 */

SemaphoreHandle_t xSemaphoreCreateCountingStatic(uxMaxCount, uxInitialCount, buffer)
{
    SemaphoreHandle_t xSem = xQueueCreateStatic(uxMaxCount, 0u, NULL, buffer);
    xSem->uxMessagesWaiting = uxInitialCount;   /* 初始计数 */
    return xSem;
}

#define xSemaphoreCreateBinaryStatic(buffer) \
    xSemaphoreCreateCountingStatic(1u, 0u, buffer)   /* 二值 = 最大1 初始0 */

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSem, TickType_t xTicksToWait)
{ return xQueueReceive(xSem, NULL, xTicksToWait); }

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSem)
{ return xQueueSend(xSem, NULL, 0); }
```

### 关键点

1. `take` 传 `pvBuffer = NULL`：信号量无数据，配合 `uxItemSize == 0` 跳过 memcpy。
2. `give` 是非阻塞（`xTicksToWait = 0`）：计数满就返回 `pdFALSE`。
3. 二值信号量 `uxLength=1`：give 后计数只能是 1，再 give 就失败——天然实现"二值"。
4. **复用哲学**：信号量只是队列的别名，一个函数都没新写阻塞逻辑。

### 上板验证（二值信号量同步）

等待者 `take(portMAX_DELAY)` 阻塞，发信号者每 1s `give`：

```
give signal
signal received!
give signal
signal received!
```

等待者启动即阻塞，每 1s 被 give 唤醒一次，证明 take/give 阻塞握手工作正常。

## 就绪位图 `uxReadyPriorityBitmap`

- 32 位整数，bit i = 优先级 i 是否有就绪任务。
- 配合 `__builtin_clz`（数前导零）在 O(1) 内找到最高就绪优先级：

```c
uint32_t uxTopPriority = 31u - (uint32_t)__builtin_clz(uxReadyPriorityBitmap);
```

- 维护三处必须一致：初始化清零、加任务置位、该优先级链表空时清位。
- 作用：避免从头扫描每个优先级的链表数组（O(N) → O(1)）。

## `__builtin_clz`：O(1) 找最高置位

> 核心公式：`最高置位下标 = 31 - __builtin_clz(x)`。
> 就绪位图每位代表一个优先级，所以这个下标就是"最高就绪优先级"。

相关代码：[rtos/core/task.c](../rtos/core/task.c) 的
`pxGetHighestPriorityReadyTask()` 和 `vTaskSwitchContext()`。

### 含义

`__builtin_clz(x)` 是 GCC/Clang 内建函数，全称 **Count Leading Zeros**：
从最高位（bit 31）起数 `x` 有多少个**连续的 0**。

例（8 位示意）：

```
x = 0b00101000
     ↑↑ 前导零共 2 个 → __builtin_clz(x) = 2
```

### 为什么 `31 - clz` = 最高置位下标

设最高置位在 bit N，则它上面（bit 31 ~ bit N+1）全是 0，共 `31 - N` 个前导零：

```
bit31 ... bit(N+1)  bitN   bit(N-1) ... bit0
  0  0  0  0  0      1       ?  ?  ?  ?
  └── 前导零：31 - N 个 ──┘
```

所以 `N = 31 - __builtin_clz(x)`。

例：`bitmap = 0x28 = 0b...00101000`（优先级 3 和 5 就绪），
前导零 26 个 → `31 - 26 = 5`，最高就绪优先级是 5。✓

### 为什么用它

- 在 ARM Cortex-M 上编译成**一条 `CLZ` 指令**，单周期完成。
- 对比手写循环 `for (i=31; i!=0; i--)` 是 O(N)，`CLZ` 是 O(1)。
- 这是调度器"常数时间选最高优先级任务"的关键。

### 注意事项

- **`clz(0)` 是未定义行为**：调用前必须先判断非 0：

```c
if (uxReadyPriorityBitmap == 0u) {
    return;  /* 没有就绪任务 */
}
```

- 配套 `__builtin_ctz`（数尾部零，Count Trailing Zeros）、
  `__builtin_popcount`（数 1 的个数）、`__builtin_ffs`（最低置位下标，从 1 起）。

## 任务栈初始化（`pxPortInitialiseStack`）

作用：在任务栈里**伪造第一次运行的"假中断现场"**，调度器靠一次异常返回启动任务。

相关代码：[rtos/port/port.c](../rtos/port/port.c) 的 `pxPortInitialiseStack`。

### 核心思想：借"异常返回"之力启动任务

Cortex-M 从异常返回时，硬件会自动从栈上按 **低地址 → 高地址** 弹出寄存器：

```
R0, R1, R2, R3, R12, LR, PC, xPSR
```

只要在栈上按这个顺序提前摆好值，再让 CPU 执行一次异常返回，它就会：
- 把 PC 设成任务入口 → 开始执行任务代码；
- 把 R0 设成参数 → 任务函数第一句就能拿到参数；
- 把 xPSR 的 Thumb 位置 1 → 告诉 CPU 执行 Thumb 指令（Cortex-M 必须）。

### 函数实现逐行拆解

```c
uint32_t *pxPortInitialiseStack(uint32_t *pxTopOfStack,   /* 栈顶（最高地址） */
                                TaskFunction_t pxCode,
                                void *pvParameters)
{
    *(--pxTopOfStack) = 0x01000000u;                 /* xPSR：Thumb 位(bit24) 必须为 1 */
    *(--pxTopOfStack) = (uint32_t)pxCode;            /* PC：任务入口地址 */
    *(--pxTopOfStack) = (uint32_t)prvTaskExitError;  /* LR：任务 return 后停住 */
    pxTopOfStack -= 5;                               /* R12,R3,R2,R1 留空 */
    *pxTopOfStack = (uint32_t)pvParameters;          /* R0：任务参数（已到位，不再 --） */
    pxTopOfStack -= 8;                               /* R4..R11 留空（软件保存，硬件不弹） */
    return pxTopOfStack;                             /* 新的栈顶 */
}
```

### 栈帧布局（上 = 高地址，先压）

```
高地址（栈底方向）
  xPSR         ← 0x01000000（Thumb 位=1）
  PC           ← pxCode（任务入口）
  LR           ← prvTaskExitError（任务返回后停住）
  R12,R3,R2,R1 ← 空位
  R0           ← pvParameters（参数）
  R4..R11      ← 8 个空位
低地址（栈顶，返回给 pxTopOfStack）
```

- 入参 `pxTopOfStack = 栈数组基址 + 栈大小`（数组末尾 = 最高地址），栈向下生长。
- `--pxTopOfStack` 先减后写；第 5 步写 R0 时指针已到位，所以**不用再 `--`**。

### 谁在消费这个栈帧

[SVC_Handler](../rtos/port/port.c) / [PendSV_Handler](../rtos/port/port.c) 里：

```asm
ldr  r0, [r1]        ; r0 = pxTopOfStack（本函数的返回值）
ldmia r0!, {r4-r11}  ; ① 软件先弹 R4-R11（对应最后留的 8 个空位）
msr  psp, r0         ; PSP 指向剩余栈帧（R0 的位置）
bx   r14             ; ② 异常返回 → 硬件弹 R0-R3,R12,LR,PC,xPSR
```

- 第 ① 步弹掉 8 个字后，PSP 正好指向 R0；
- 第 ② 步硬件异常返回，按顺序弹出 R0(=参数)、R1-R3、R12、LR(=ExitError)、
  PC(=任务入口)、xPSR(Thumb) → 任务带参数跑起来。

- 返回值存进 `pxTCB->pxTopOfStack`，TCB 把它放第一个字段（汇编按偏移 0 读取）。

## C 语言要点

### 指针：类型不同，大小相同，但行为不同

> 指针变量存的都是地址。在 Cortex-M3（32 位）上，所有指针都是 4 字节；
> 但"类型"决定了**解引用读几个字节**和**指针运算的步长**。

#### 大小 vs 类型信息

```c
int    a = 10;
char   c = 'x';
float  f = 3.14f;

int   *p1 = &a;    /* 存的都是地址 */
char  *p2 = &c;
float *p3 = &f;
```

- **大小**：32 位平台上 `int*`、`char*`、`float*`、`void*` 都是 4 字节（地址总线 32 根）。
- **类型**：大小相同 ≠ 类型相同。类型信息决定两个行为。

#### ① 解引用：读几个字节、怎么解释

```c
int  a  = 0x41424344;        /* 小端内存：44 43 42 41 */
int  *pi = &a;
char *pc = (char *)&a;       /* 同一个地址，当成 char* */

*pi;   /* 读 4 字节按 int 解释 → 0x41424344 */
*pc;   /* 只读 1 字节按 char 解释 → 0x44 */
```

地址一样，类型不同 → 读的字节数和解释方式都不同。
这正是 `pxHead->pvOwner` 要先强转成 `TCB_t *` 才能用 `->uxPriority` 的原因。

#### ② 指针运算：步长 = sizeof(类型)

```c
int   *pi = (int *)0x1000;
char  *pc = (char *)0x1000;

pi + 1;   /* 0x1004（步长 4） */
pc + 1;   /* 0x1001（步长 1） */
```

`p + 1` = 地址 + 1×sizeof(类型)，不是"地址 + 1"。
所以 `uint32_t *pxTopOfStack` 做 `--pxTopOfStack` 时地址每次减 4（一个字）。

#### ③ void*：只有地址，没有类型信息

- `void *` 只表示"一个地址"，不能解引用、不能做运算（不知道步长和解释方式）。
- 必须强转成具体类型才能用：`(TCB_t *)pxList->pxNext->pvOwner`。
- 强转不改变地址值，只改变编译器"怎么看这个地址"。

#### ④ C 标准不保证所有指针一样大（进阶）

- 实践上主流 32/64 位平台所有指针大小相同（4/8 字节）。
- 但 C 标准允许**函数指针**和**数据指针**大小不同（如哈佛架构 AVR：代码/数据存储分开）。
- 严谨说法：同平台通常相同，但标准不保证，可移植代码不要假设。

### 大端 vs 小端存储

> 一个多字节数在内存里怎么排：**最低字节放最低地址 = 小端**；
> **最高字节放最低地址 = 大端**。Cortex-M3 是小端。

以 `0x12345678`（4 字节：12 34 56 78）为例：

```
小端（Cortex-M3 / x86 / ARM）：
低地址 →                        高地址
 0x2000    0x2001    0x2002    0x2003
┌────────┬────────┬────────┬────────┐
│  0x78  │  0x56  │  0x34  │  0x12  │
└────────┴────────┴────────┴────────┘
↑ 最低字节(LSB)放最低地址

大端（网络字节序）：
低地址 →                        高地址
 0x2000    0x2001    0x2002    0x2003
┌────────┬────────┬────────┬────────┐
│  0x12  │  0x34  │  0x56  │  0x78  │
└────────┴────────┴────────┴────────┘
↑ 最高字节(MSB)放最低地址
```

- 记忆：小端 = "小头朝前"（数字倒着写）；大端 = "大头朝前"（像人正着写数字）。
- 重要性：网络协议是大端（需 `htonl`/`ntohl`）；用 `char*` 逐字节读 `int` 时结果取决于端序。
- 项目里 `0x01000000` 压栈后内存为 `00 00 00 01`（低字节在低地址）。

### 栈的增长方向：向下 vs 向上

> Cortex-M3 的栈**向下（低地址）增长**，即压栈时 SP 减小。

```
向下增长（满递减，Cortex-M3）：
高地址（栈底，初始 SP 在这里）
┌──────────────┐
│   刚压的数据  │ ← 第一笔数据：紧贴初始 SP 下方（最高地址端）
├──────────────┤ ← 压栈后 SP（当前栈顶）
│              │
│   空余空间    │     压栈：SP = SP - 4（地址减小），数据往低地址写
│              │     出栈：SP = SP + 4（地址增大）
└──────────────┘
低地址
```

- **数据从高地址往下存**：第一次压栈 `SP = SP - 4` 后写入，第一笔数据落在
  栈内存的**最高处**（紧贴初始 SP 下方）；继续压栈，数据一路向低地址延伸。
- 所以图里**数据在高地址端，空余空间在低地址端**——不要搞反。
- **栈底** = 栈内存的起始端（高地址，固定）；**栈顶** = 当前 SP（动态，向下移动）。
- 所以 `*(--pxTopOfStack)` 先 `--` 再写（对应压栈 SP 先减）。
- 栈顶计算：`puxStackBuffer + ulStackSizeBytes/sizeof(uint32_t)` = 数组末尾（最高地址）= 初始栈顶。
- 少数架构（部分 DSP）用向上增长；Cortex-M 系列是**满递减（Full Descending）**。

### 函数指针 typedef

```c
typedef void (*TaskFunction_t)(void *);   /* 任务函数类型 */
/* 不用 typedef 的等价写法：void (*pxTaskCode)(void *); */
```

- `(*name)` 的括号不能省：`void *f(void *)` 是"返回 void* 的函数"，不是函数指针。
- typedef 把复杂度收起来，参数列表才清爽。

### `(void)变量;`

- 空操作，把参数"消费"一次，消除 `-Wunused-parameter`（`-Wextra` 开启）警告。
- 函数签名必须带该参数（如 `TaskFunction_t` 要求 `void *`），但实现用不到时使用。

### `volatile`：每次真实读写内存，不优化

> `volatile` = "每次访问都真的去内存读/写，不许缓存到寄存器、不许优化掉"。

#### 为什么需要：编译器会"自作聪明"

```c
volatile uint32_t s_tick_ms;   /* bsp.c：SysTick ISR 里改 */

while (s_tick_ms < 100) { }    /* 忙等 tick 变化 */
```

- 无 `volatile`：编译器看循环体没人改它，把值缓存进寄存器 → 死循环
  （编译器不知道 ISR 在后台改内存）。
- 有 `volatile`：每次循环真实读内存，才能看到 ISR 加 1。

#### 三类使用场景

1. **内存映射寄存器**：硬件自己改值 / 写寄存器触发硬件动作。
   `SCB->ICSR = SCB_ICSR_PENDSVSET;` 不能被优化掉。
2. **ISR 与主代码共享的变量**：`s_tick_ms`、`xTickCount`、`xSchedulerRunning`。
3. **硬件/别的执行流异步修改的内存**（DMA、多核共享）。

#### 语法：修饰数据 vs 修饰指针

```c
volatile uint32_t x;           /* 修饰数据（常用） */
uint32_t volatile x;           /* 等价 */

volatile uint32_t *p;          /* ① 指向的数据是 volatile（指针本身可改） */
uint32_t * volatile p;         /* ② 指针变量本身 volatile（数据普通） */
volatile uint32_t * volatile p;/* ③ 两者都 volatile */
```

- 判断：`volatile` 修饰它右边紧跟的东西。
  项目里 `volatile uint32_t *pxTopOfStack;` 是第 ① 种（栈内容会被硬件改变）。

#### volatile 不能做什么

| 误区 | 真相 |
|------|------|
| 保证原子性 | ❌ 不保证 |
| 解决多线程数据竞争 | ❌ 不能，要用互斥锁/原子操作 |
| 保证有序性 | ❌ 不保证，跨屏障顺序要用 `dsb`/`isb` |

- RTOS 里两个任务共享变量**不能用 volatile 代替互斥锁**：volatile 只解决
  "编译器优化"，不解决"两个执行流同时读写"的竞争。

### `__attribute__((aligned(8)))`

- GCC 扩展，强制变量起始地址按 8 字节对齐。
- 任务栈对齐是为了满足 Cortex-M AAPCS：栈指针在公开接口处必须 8 字节对齐。

### `extern` 声明放 `.h` 的好处

- 定义只在一处 `.c`（真正分配内存），声明放 `.h` 供所有文件 include。
- 好处：单一权威来源，避免各文件手写 `extern` 造成类型漂移（链接器不检查类型）。
- 注意：`extern uint32_t x = 0;` 带初始化是**定义**不是声明。

### malloc 与定义变量的区别

| 维度 | malloc（堆） | 局部变量（栈） | 全局/静态（静态区） |
|------|-------------|---------------|--------------------|
| 分配时机 | 运行时按需 | 进入函数自动 | 编译/链接时 |
| 生命周期 | 到 free | 函数返回即失效 | 整个程序 |
| 大小 | 运行时可变 | 编译期固定 | 编译期固定 |
| 释放 | 手动 free | 自动 | 无需 |
| 失败 | 返回 NULL | 可能栈溢出 | 链接期可发现 |

RTOS 内核偏爱静态分配（`xTaskCreateStatic` 由调用者提供栈和 TCB），换来确定性、无运行时失败。

### 内存映射寄存器（Memory-Mapped I/O）

相关代码：[bsp/stm32f103.h](../bsp/stm32f103.h)

```c
#define SCB ((SCB_TypeDef *)0xE000ED00UL)
```

- 把固定地址 `0xE000ED00`（SCB 寄存器组起始地址）强转成结构体指针，
  于是 `SCB->ICSR` 就是"访问该地址处的一个 32 位寄存器"。
- 前提：Cortex-M 上**外设寄存器被映射到内存地址空间**，访问某地址 = 读写硬件寄存器。

#### 结构体字段布局 = 寄存器排布

```c
typedef struct {
    volatile uint32_t CPUID;   /* 偏移 0x00 */
    volatile uint32_t ICSR;    /* 偏移 0x04 */
    volatile uint32_t VTOR;    /* 偏移 0x08 */
    ...
    volatile uint32_t SHPR3;   /* 偏移 0x20 */
} SCB_TypeDef;
```

编译器按字段顺序连续布局，所以：

```
SCB->ICSR  → 0xE000ED00 + 4  = 0xE000ED04
SCB->VTOR  → 0xE000ED00 + 8  = 0xE000ED08
SCB->SHPR3 → 0xE000ED00 + 32 = 0xE000ED20
```

与硬件手册寄存器地址一一对应。

#### 为什么必须 `volatile`

硬件会**自己改变寄存器值**（如 PendSV 挂起后硬件置位 ICSR）。`volatile` 告诉
编译器"每次都要真实读写这个地址，不许缓存/优化掉"，否则会读到过期值或丢失写入。

#### 与普通指针的区别

| | 普通指针 | 寄存器宏（如 SCB） |
|---|---------|-------------------|
| 指向 | 软件分配的内存 | 硬件定死的固定地址 |
| 地址 | 编译器/运行时决定 | 芯片手册规定 |
| 语法 | `*p` / `p->x` | 完全相同 |

#### CMSIS 的关系

这段代码手动实现了 CMSIS 里的现成定义（注释 `intentionally avoids CMSIS`）。
看懂"结构体 + 强制转换 + volatile"这套模式，就懂了 HAL/标准库寄存器访问的底层原理。

#### 项目里的使用

```c
SCB->ICSR = SCB_ICSR_PENDSVSET;   /* 挂起 PendSV：往 0xE000ED04 写 bit28=1 */
SCB->SHPR3 |= (...);              /* 设优先级：改 0xE000ED20 的某些位 */
```

