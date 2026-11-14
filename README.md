# 从零手写 RTOS（STM32F103C8T6）

在 STM32F103C8T6（Blue Pill，Cortex-M3）上从零实现一个微型实时操作系统。
当前进度：**核心数据结构已完成**（双向链表 + 任务控制块 + 就绪列表/位图），
下一步是 PendSV 上下文切换。

## 目录结构

```
├── rtos/                 # RTOS 内核
│   ├── core/             # 链表、任务控制块、就绪列表/位图
│   ├── port/             # Cortex-M3 移植层（上下文切换汇编，下一步）
│   ├── ipc/              # 信号量、互斥锁、消息队列（待实现）
│   ├── timer/            # 软件定时器（待实现）
│   └── rtos_config.h     # 内核配置
├── app/                  # 应用与演示例程
├── bsp/                  # 板级支持：寄存器定义、点灯、串口、SysTick
├── tests/                # 宿主（PC）单元测试，不依赖硬件
├── build/                # 构建产物（make 生成）
├── Makefile
├── linker.ld             # 链接脚本
├── startup_stm32f103.s   # 启动文件 + 向量表
└── openocd.cfg           # ST-Link + STM32F1x 烧录配置
```

## 环境要求

- `arm-none-eabi-gcc`（Arm GNU Toolchain，arm-none-eabi-* 需在 PATH）
- `make`（MinGW make 或 GNU make）
- `sh`（Git for Windows 自带，需把 `C:\Program Files\Git\usr\bin` 加入 PATH）
- `openocd`（烧录用，xPack 版本）
- ST-Link V2 + Blue Pill
- 宿主测试另需本机 `gcc`（MinGW）

## 构建

```
make          # 生成 build/firmware.elf / .bin / .hex
make test     # 在 PC 上运行宿主单元测试（验证链表/调度数据结构）
make clean    # 清理
```

## 烧录

接上 ST-Link，然后：

```
make flash
```

等价命令：

```
openocd -f openocd.cfg -c "program build/firmware.elf verify reset exit"
```

## 串口输出

USART1（PA9 TX / PA10 RX），115200 8N1。上电后每 500 ms 切换 PC13 LED 并打印 tick。

## 内核设计要点

- **双向循环链表**（`rtos/core/list.c`）：哨兵节点保证链表永远闭合，
  插入/删除无需特判空表；按 `xItemValue` 升序插入，`pvContainer` 标记归属。
- **任务控制块 TCB**：每个任务持有两个链表节点（状态节点 + 事件节点），
  记录栈顶指针、优先级、栈信息。
- **就绪列表 + 就绪位图**：每优先级一条链表，32 位位图标记哪些优先级有
  就绪任务，`__builtin_clz` 实现 O(1) 查找最高优先级就绪任务。
- 系统时钟使用内部 HSI 8 MHz（未开 PLL），SysTick 1 ms 时基
- 串口波特率用 BRR 小数分频修正到 115200
- 自写启动文件 + 链接脚本，不使用 CMSIS/HAL
- `printf` 通过重定向 `_write` 输出到 USART1（newlib-nano）
