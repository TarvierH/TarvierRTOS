/* Minimal register definitions for STM32F103 (Cortex-M3).
 * Only what we need so far; extend as the RTOS grows.
 * This intentionally avoids CMSIS so the whole project is self-contained.
 */
#ifndef STM32F103_H
#define STM32F103_H

#include <stdint.h>

/* ---- GPIO ---- */
typedef struct {
    volatile uint32_t CRL;   /* port config low  (pin 0..7)  */
    volatile uint32_t CRH;   /* port config high (pin 8..15) */
    volatile uint32_t IDR;   /* input data */
    volatile uint32_t ODR;   /* output data */
    volatile uint32_t BSRR;  /* bit set/reset */
    volatile uint32_t BRR;   /* bit reset */
    volatile uint32_t LCKR;  /* config lock */
} GPIO_TypeDef;

#define GPIOA ((GPIO_TypeDef *)0x40010800UL)
#define GPIOB ((GPIO_TypeDef *)0x40010C00UL)
#define GPIOC ((GPIO_TypeDef *)0x40011000UL)

/* ---- RCC ---- */
typedef struct {
    volatile uint32_t CR;
    volatile uint32_t CFGR;
    volatile uint32_t CIR;
    volatile uint32_t APB2RSTR;
    volatile uint32_t APB1RSTR;
    volatile uint32_t AHBENR;
    volatile uint32_t APB2ENR;
    volatile uint32_t APB1ENR;
    volatile uint32_t BDCR;
    volatile uint32_t CSR;
} RCC_TypeDef;

#define RCC ((RCC_TypeDef *)0x40021000UL)

#define RCC_APB2ENR_AFIOEN    (1u << 0)
#define RCC_APB2ENR_IOPAEN    (1u << 2)
#define RCC_APB2ENR_IOPBEN    (1u << 3)
#define RCC_APB2ENR_IOPCEN    (1u << 4)
#define RCC_APB2ENR_USART1EN  (1u << 14)

/* ---- USART1 ---- */
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
    volatile uint32_t GTPR;
} USART_TypeDef;

#define USART1 ((USART_TypeDef *)0x40013800UL)

#define USART_SR_TC   (1u << 6)
#define USART_SR_TXE  (1u << 7)
#define USART_SR_RXNE (1u << 5)
#define USART_CR1_UE  (1u << 13)
#define USART_CR1_TE  (1u << 3)
#define USART_CR1_RE  (1u << 2)

/* ---- SysTick ---- */
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_TypeDef;

#define SysTick ((SysTick_TypeDef *)0xE000E010UL)

#define SysTick_CTRL_ENABLE    (1u << 0)
#define SysTick_CTRL_TICKINT   (1u << 1)
#define SysTick_CTRL_CLKSOURCE (1u << 2)
#define SysTick_CTRL_COUNTFLAG (1u << 16)

/* ---- SCB (subset; the RTOS will need ICSR for PendSV) ---- */
typedef struct {
    volatile uint32_t CPUID;
    volatile uint32_t ICSR;
    volatile uint32_t VTOR;
    volatile uint32_t AIRCR;
    volatile uint32_t SCR;
    volatile uint32_t CCR;
    volatile uint32_t SHPR1;
    volatile uint32_t SHPR2;
    volatile uint32_t SHPR3;
    volatile uint32_t SHCSR;
    volatile uint32_t CFSR;
    volatile uint32_t HFSR;
    volatile uint32_t DFSR;
    volatile uint32_t MMFAR;
    volatile uint32_t BFAR;
    volatile uint32_t AFSR;
} SCB_TypeDef;

#define SCB ((SCB_TypeDef *)0xE000ED00UL)

#define SCB_ICSR_PENDSVSET (1u << 28)
#define SCB_ICSR_PENDSVCLR (1u << 27)

#endif /* STM32F103_H */
