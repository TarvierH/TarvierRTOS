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
