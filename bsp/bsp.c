#include "bsp.h"
#include "stm32f103.h"

/* The Blue Pill runs from the internal 8 MHz HSI by default.
 * We deliberately keep 8 MHz: no PLL setup needed and the RTOS tick
 * math stays trivial. UART baud accuracy is fixed with the BRR
 * fractional divider. */
#define HSI_HZ     8000000UL
#define UART_BAUD  115200UL

static volatile uint32_t s_tick_ms;

void bsp_init(void)
{
    /* Clocks for GPIOA, GPIOC, USART1 and AFIO */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN |
                    RCC_APB2ENR_USART1EN | RCC_APB2ENR_AFIOEN;

    /* On-board LED on PC13: push-pull output, 2 MHz */
    GPIOC->CRH &= ~(0xFu << 20);
    GPIOC->CRH |=  (0x2u << 20);   /* MODE=10 (2MHz), CNF=00 (push-pull) */

    /* USART1 TX = PA9  (alternate push-pull, 50 MHz) */
    GPIOA->CRH &= ~(0xFu << 4);
    GPIOA->CRH |=  (0xBu << 4);    /* MODE=11 (50MHz), CNF=10 (AF PP) */

    /* USART1 RX = PA10 (input floating) */
    GPIOA->CRH &= ~(0xFu << 8);
    GPIOA->CRH |=  (0x4u << 8);    /* MODE=00, CNF=01 */

    /* 8N1 @ 115200 with the fractional baud divider.
     * STM32F1 波特率公式：USARTDIV = f_PCLK / (16 * 波特率)
     * = 8e6 / (16*115200) = 4.34 -> 尾数 4，小数 0.34*16 ≈ 5 */
    uint32_t div  = HSI_HZ / (UART_BAUD * 16u);
    uint32_t frac = ((HSI_HZ % (UART_BAUD * 16u)) * 16u + (UART_BAUD * 8u))
                    / (UART_BAUD * 16u);
    USART1->BRR = (div << 4) | frac;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    /* SysTick: 1 ms tick, processor clock source, interrupt on */
    SysTick->LOAD = HSI_HZ / 1000u - 1u;
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE | SysTick_CTRL_TICKINT |
                    SysTick_CTRL_ENABLE;

    bsp_led_off();
}

void bsp_led_on(void)
{
    GPIOC->BRR = (1u << 13);   /* active low */
}

void bsp_led_off(void)
{
    GPIOC->BSRR = (1u << 13);
}

void bsp_led_toggle(void)
{
    if (GPIOC->ODR & (1u << 13)) {
        GPIOC->BRR = (1u << 13);
    } else {
        GPIOC->BSRR = (1u << 13);
    }
}

void bsp_putc(char c)
{
    while (!(USART1->SR & USART_SR_TXE)) { }
    USART1->DR = (uint8_t)c;
}

void bsp_uart_write(const char *data, unsigned len)
{
    for (unsigned i = 0; i < len; i++) {
        bsp_putc(data[i]);
    }
}

void bsp_print(const char *s)
{
    while (*s) {
        bsp_putc(*s++);
    }
}

void bsp_print_dec(int32_t v)
{
    char buf[12];
    int i = 0;
    uint32_t u;
    if (v < 0) {
        bsp_putc('-');
        u = (uint32_t)(-(v + 1)) + 1u;
    } else {
        u = (uint32_t)v;
    }
    do {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u);
    while (i) {
        bsp_putc(buf[--i]);
    }
}

void bsp_print_hex(uint32_t v)
{
    bsp_print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint32_t n = (v >> i) & 0xFu;
        bsp_putc((char)(n < 10 ? '0' + n : 'A' + n - 10));
    }
}

uint32_t bsp_get_tick(void)
{
    return s_tick_ms;
}

void bsp_delay_ms(uint32_t ms)
{
    uint32_t start = s_tick_ms;
    while ((s_tick_ms - start) < ms) { }
}

void bsp_tick_isr(void)
{
    s_tick_ms++;
}

/* 弱符号：RTOS 移植层（port.c）用强符号覆盖，执行调度节拍 */
__attribute__((weak)) void rtos_systick_hook(void)
{
}

/* SysTick_Handler is declared weak in the startup file; this strong
 * definition overrides it. */
void SysTick_Handler(void)
{
    bsp_tick_isr();
    rtos_systick_hook();
}
