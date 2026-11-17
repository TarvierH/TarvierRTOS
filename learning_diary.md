# 学习日志

## 2026-11-14 · FreeRTOS 内核学习第 1 天

### 今日目标
搞懂这个从零手写 RTOS 项目的整体结构，确定学习路线，并吃透内核的第一个基础数据结构——双向循环链表。

### 完成的任务

1. **梳理项目结构，确定入口**
   - 程序运行入口：[app/main.c](../app/main.c)（初始化板级支持 → 点灯 + 打印 tick 的裸机循环）。
   - 启动流程：[startup_stm32f103.s](../startup_stm32f103.s) 复位后跳转 `main`。
   - 当前 RTOS 核心**尚未接入固件构建**（Makefile 只编译 app/bsp），内核上下文切换（PendSV）还未实现。

2. **制定 RTOS 内核学习路线**
   - 链表 → TCB/就绪列表 → 上下文切换（PendSV）→ 调度器 + 时间管理 → IPC → 软件定时器/内存管理。
   - 参考书：野火《FreeRTOS 内核实现与应用开发实战指南》、《ARM Cortex-M3 权威指南》、恐龙书。

3. **读懂 [rtos/core/task.c](../rtos/core/task.c)**
   - 作用：任务的"注册表 + 就绪队列"，负责初始化就绪列表、静态创建任务、维护就绪位图、O(1) 查找最高优先级就绪任务。
   - 关键：`__builtin_clz` + 32 位位图实现 O(1) 调度查找。

4. **学习 FreeRTOS 变量命名规范**
   - `x` = 随平台变化的非标准类型（`BaseType_t` / `TickType_t`）。
   - `ux` = 无符号基础类型（`UBaseType_t`）。
   - `xItemValue`（时间）与 `uxNumberOfItems`（个数）底层都是 `uint32_t`，但语义不同所以前缀不同。

5. **吃透哨兵节点（`xListEnd`）**
   - 四个职责：链表闭合免特判、排序查找终止值（`UINT32_MAX`）、定义链表头尾、`pxIndex` 稳定锚点。
   - 已整理成详细笔记：[learning_notes.md](./learning_notes.md)。

### 关键收获
- 数据结构是一切的基础：链表没吃透，后面的就绪列表、延时列表、IPC 等待列表都会卡壳。
- FreeRTOS 的命名前缀是在表达**语义**（类型角色），而不是重复底层类型信息。
- 哨兵节点是"用空间换简单性"的经典设计——多一个不参与计数的节点，省掉了所有空表边界判断。

### 下一步
- 继续链表：理解 `vListInsertEnd` 如何配合 `pxIndex` 实现同一优先级任务的时间片轮转。
- 然后进入上下文切换（`pxPortInitialiseStack` + PendSV），这是内核最核心的一步。

### 疑问/待查
- 暂无，留待后续。

---

## 2026-11-16 · FreeRTOS 内核学习第 2 天

### 今日目标
读懂"任务创建 → 就绪列表 → 上下文切换 → 调度器启动"这条主线，补上 C 语言语法细节和内存模型知识。

### 完成的任务

1. **C 语言语法细节**
   - 函数指针 typedef：`typedef void (*TaskFunction_t)(void *)` 与不 typedef 的写法对比。
   - `(void)pvParameters;`：消除 `-Wunused-parameter` 警告的空操作。
   - `__attribute__((aligned(8)))`：GCC 扩展，强制变量 8 字节对齐（满足 Cortex-M 栈指针对齐要求）。
   - `prv` = private：static 私有函数前缀；`v` = void 返回、`x` = 返回句柄/BaseType_t。
   - `extern` 声明放 `.h` 的好处：单一权威来源，避免各文件重复声明导致类型漂移。
   - malloc（堆）vs 定义变量（栈/静态区）：分配时机、生命周期、释放责任、大小可变性、确定性。

2. **链表设计深入**
   - 哨兵节点 vs 教科书头结点：头结点只管头（单向线性），哨兵管头尾 + 查找终止（双向循环）。
   - 头插 = 插到哨兵之后；尾插 = 插到哨兵之前。但 `vListInsertEnd` 实际以**游标 pxIndex** 为锚点，游标=哨兵时才是尾插。
   - **侵入式 vs 非侵入式链表**：节点内嵌在对象里 vs 独立节点指向数据；FreeRTOS 选侵入式是为了零 malloc、可多链表共存、删除 O(1)。

3. **任务管理**
   - `uxReadyPriorityBitmap`：32 位位图，bit i = 优先级 i 有就绪任务，配 `__builtin_clz` 实现 O(1) 找最高优先级。
   - `xStateListItem`（运行状态：就绪/挂起/延时）vs `xEventListItem`（事件等待：队列/信号量）——两个节点让任务可同时挂两条链表（如带超时等队列）。
   - `pxPortInitialiseStack`：在任务栈里伪造"假中断现场"（xPSR/PC/LR/R0=参数），调度器靠一次异常返回启动任务。

4. **状态机**
   - BLOCKED（阻塞）= 主动等事件、满足自动醒；SUSPENDED（挂起）= 被动冻结、必须 `vTaskResume` 显式恢复。

5. **学习方法论**
   - "看得懂但不懂作用"的解法：反问"删掉会怎样"、追数据流（谁写→谁读→驱动什么决策）、放进调度故事里理解、动手改代码验证。

### 关键收获
- 链表节点是内嵌的（侵入式），所以"插节点"= 把 TCB 的一部分挂上去，`pvOwner` 指回 TCB。
- 任务第一次运行的本质：把"异常返回"当成"函数调用"来用，假栈帧被硬件弹出成真实寄存器。
- 嵌入式内核偏爱静态分配（不 malloc），换来确定性和无运行时失败。

### 下一步
- 顺着 port.c 精读 PendSV_Handler / SVC_Handler 汇编，理解现场保存与恢复的全流程。
- 然后进入时间管理：SysTick + 延时列表（`vTaskDelay` 如何把任务摘出就绪、按唤醒 tick 排序）。

### 疑问/待查
- PendSV 为何要设成最低优先级、为什么要用 PSP 而非 MSP——精读汇编时一并解决。

---

## 2026-11-17 · FreeRTOS 内核学习第 3 天

### 今日目标
读完 list/task/port 三大件，完成里程碑 2：`vTaskDelay` + 延时列表 + 唤醒逻辑。

### 完成的任务

1. **汇编与底层机制**
   - 什么是 PendSV：Cortex-M 异常 14，"Pend"=挂起，切换的统一入口；优先级最低保证切换发生在中断处理完后。
   - 什么是 NVIC：中断调度中心（使能/优先级/挂起/嵌套向量跳转）；PendSV/SysTick/SVC 三个系统异常归 SCB 管，外设中断归 NVIC 管。
   - `taskYIELD`：写 ICSR bit28 挂起 PendSV + dsb/isb 同步，只"申请"不"立即切"。
   - `prvStartFirstTask` 汇编：movw/movt 拼地址 → 读 VTOR 取初始 MSP → cpsie 开中断 → svc 0 触发 SVC。
   - 学了 11 条核心汇编指令（ldr/str/mov/msr/mrs/ldmia/stmdb/bx/cpsie/svc/dsb/isb），够读懂 port.c。

2. **内存映射寄存器**
   - `#define SCB ((SCB_TypeDef *)0xE000ED00UL)`：把固定地址强转结构体指针，`SCB->ICSR` 就是按名字访问硬件寄存器。
   - 字段布局=寄存器排布、为什么必须 volatile、与 CMSIS 的关系。

3. **实现里程碑 2（亲手写代码）**
   - 加 `configTICK_RATE_HZ`、延时列表 `xDelayedTaskList`、tick 计数 `xTickCount`。
   - `vTaskDelay`：摘就绪 → 阻塞 → 算绝对唤醒时刻 → 排序插入延时列表 → yield。
   - `xTaskIncrementTick`：tick++ → 循环唤醒到期任务 → 保留轮转判断。
   - main.c 任务从"忙等"改成"干完活就睡"。
   - **编译通过：0 错误 0 警告**（firmware.elf 1916 text / 1764 bss）。

4. **踩坑记录**
   - `prvAddTaskToReadyList(pxHead)` 把节点当任务传 → 类型错位导致按错误优先级索引数组。
   - 教训：链表 API 吃节点（ListItem_t*），任务 API 吃任务（TCB_t*）。

### 关键收获
- 延时列表存"绝对唤醒时刻"并升序排序，于是每个 tick 只需检查表头，O(1)。
- 阻塞 = 任务从就绪列表"搬家"到延时列表，用同一个 `xStateListItem`。
- RTOS 时间管理的本质：SysTick 驱动 tick，延时列表按唤醒时刻排队，到点搬回就绪。

### 下一步
- 上板验证（make flash）观察 LED/串口。
- 可选：`vTaskDelayUntil`（精确周期）、`vTaskSuspend`/`vTaskResume`、或开始 IPC 信号量。

### 疑问/待查
- 暂无。
