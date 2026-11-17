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

