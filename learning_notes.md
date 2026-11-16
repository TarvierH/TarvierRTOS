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
