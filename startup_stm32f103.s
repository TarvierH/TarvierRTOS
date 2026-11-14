/* Minimal startup file for STM32F103C8T6 (Cortex-M3)
 * GNU assembler syntax. Vector table + Reset_Handler.
 */

    .syntax unified
    .cpu cortex-m3
    .thumb

    .section .isr_vector, "a", %progbits

    .word _estack            /* initial stack pointer */
    .word Reset_Handler      /* 1  Reset */
    .word NMI_Handler        /* 2  NMI */
    .word HardFault_Handler  /* 3  HardFault */
    .word MemManage_Handler  /* 4  MemManage */
    .word BusFault_Handler   /* 5  BusFault */
    .word UsageFault_Handler /* 6  UsageFault */
    .word 0                  /* 7  reserved */
    .word 0                  /* 8  reserved */
    .word 0                  /* 9  reserved */
    .word 0                  /* 10 reserved */
    .word SVC_Handler        /* 11 SVCall (used by the RTOS later) */
    .word DebugMon_Handler   /* 12 DebugMonitor */
    .word 0                  /* 13 reserved */
    .word PendSV_Handler     /* 14 PendSV (context switch, RTOS) */
    .word SysTick_Handler    /* 15 SysTick (RTOS time base) */

    /* External interrupts: STM32F103 medium-density has 43 lines */
    .rept 43
    .word Default_Handler
    .endr

    .section .text

    .thumb_func
    .global Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    /* Set the stack pointer to the top of SRAM */
    ldr r0, =_estack
    mov sp, r0

    /* Copy .data initializers from flash to RAM */
    ldr r0, =_sidata
    ldr r1, =_sdata
    ldr r2, =_edata
    b 2f
1:
    ldr r3, [r0], #4
    str r3, [r1], #4
2:
    cmp r1, r2
    bcc 1b

    /* Zero-fill .bss */
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
    b 4f
3:
    str r2, [r0], #4
4:
    cmp r0, r1
    bcc 3b

    /* Jump to main (never returns) */
    bl main
    b .

    .thumb_func
    .weak Default_Handler
    .type Default_Handler, %function
Default_Handler:
    b .

    /* All exception handlers default to a spin loop unless defined in C */
    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak HardFault_Handler
    .thumb_set HardFault_Handler, Default_Handler
    .weak MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak UsageFault_Handler
    .thumb_set UsageFault_Handler, Default_Handler
    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler
    .weak DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler
    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler
    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

    .end
