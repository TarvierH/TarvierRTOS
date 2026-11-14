# 宿主单元测试：用本机 MinGW gcc 编译，验证链表与调度数据结构
CC     := gcc
CFLAGS := -std=c11 -Wall -Wextra -O0 -g3 \
          -I../rtos -I../rtos/core -I../rtos/port

TARGET := test_core.exe

SRCS := test_core.c \
        ../rtos/core/list.c \
        ../rtos/core/task.c

.PHONY: all clean

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(TARGET)
