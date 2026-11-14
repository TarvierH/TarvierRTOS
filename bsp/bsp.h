/* Board support package: LED, UART debug output, SysTick time base. */
#ifndef BSP_H
#define BSP_H

#include <stdint.h>

void bsp_init(void);            /* clocks, LED, UART1, SysTick          */
void bsp_led_on(void);          /* PC13 active-low                     */
void bsp_led_off(void);
void bsp_led_toggle(void);

void bsp_putc(char c);
void bsp_uart_write(const char *data, unsigned len);
void bsp_print(const char *s);
void bsp_print_dec(int32_t v);
void bsp_print_hex(uint32_t v);

void bsp_delay_ms(uint32_t ms);      /* busy wait on the SysTick counter */
uint32_t bsp_get_tick(void);         /* milliseconds since boot          */

/* Called from SysTick_Handler; this is where the RTOS scheduler tick
 * will hook in later. */
void bsp_tick_isr(void);

#endif /* BSP_H */
