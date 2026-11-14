#include <stdio.h>
#include "bsp.h"

int main(void)
{
    bsp_init();

    /* Unbuffered output so every printf goes straight to the UART */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("\r\n================================\r\n");
    printf(" STM32F103 bare-metal skeleton\r\n");
    printf(" System clock: 8 MHz (HSI)\r\n");
    printf("================================\r\n\r\n");

    while (1) {
        bsp_led_toggle();
        printf("tick = %u ms\r\n", (unsigned)bsp_get_tick());
        bsp_delay_ms(500);
    }
}
