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

相关代码：[rtos/port/port.c](../rtos/port/port.c)

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

## 就绪位图 `uxReadyPriorityBitmap`

- 32 位整数，bit i = 优先级 i 是否有就绪任务。
- 配合 `__builtin_clz`（数前导零）在 O(1) 内找到最高就绪优先级：

```c
uint32_t uxTopPriority = 31u - (uint32_t)__builtin_clz(uxReadyPriorityBitmap);
```

- 维护三处必须一致：初始化清零、加任务置位、该优先级链表空时清位。
- 作用：避免从头扫描每个优先级的链表数组（O(N) → O(1)）。

## 任务栈初始化（`pxPortInitialiseStack`）

作用：在任务栈里**伪造第一次运行的"假中断现场"**，调度器靠一次异常返回启动任务。

```c
puxStackBuffer + (ulStackSizeBytes / sizeof(uint32_t))  /* 数组末尾 = 栈顶（栈向下生长） */
```

- 从栈顶往低地址铺：xPSR、PC（任务入口）、LR（prvTaskExitError）、R0（参数）。
- 返回值存进 `pxTCB->pxTopOfStack`，TCB 把它放第一个字段（汇编按偏移 0 读取）。
- 启动时：`ldr r0,[r1]` 取栈顶 → `ldmia` 恢复 R4-R11 → `msr psp` → `bx r14`，
  硬件继续弹出 R0-R3/R12/LR/PC/xPSR → 任务带参数跑起来。

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
