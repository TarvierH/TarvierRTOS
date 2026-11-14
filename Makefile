# STM32F103C8T6 "Blue Pill" bare-metal build
#   make         - build firmware.elf/.bin/.hex
#   make flash   - flash via ST-Link + OpenOCD
#   make clean   - remove build output

PREFIX  ?= arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size
OPENOCD ?= openocd

TARGET := firmware
BUILD  := build

# Source files (add RTOS sources here as they are written)
SRCS := app/main.c \
        bsp/bsp.c \
        bsp/syscalls.c
ASMS := startup_stm32f103.s

OBJS := $(addprefix $(BUILD)/, $(SRCS:.c=.o)) \
        $(addprefix $(BUILD)/, $(ASMS:.s=.o))

CFLAGS  := -mcpu=cortex-m3 -mthumb -O2 -g3 -std=c11 \
           -Wall -Wextra -ffunction-sections -fdata-sections \
           -Ibsp -Iapp
LDFLAGS := -mcpu=cortex-m3 -mthumb -nostartfiles -T linker.ld \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           --specs=nano.specs

.PHONY: all clean flash test

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	@echo
	@$(SIZE) $(BUILD)/$(TARGET).elf

test:
	$(MAKE) -C tests

$(BUILD)/$(TARGET).elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.s
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@rm -rf $(BUILD)

flash: $(BUILD)/$(TARGET).elf
	$(OPENOCD) -f openocd.cfg -c "program $(BUILD)/$(TARGET).elf verify reset exit"
