#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "uart.h"

static uint8_t *uart_buffer;

void init_uart() {
    uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_NUM_1, &config);
    // UART_NUM_0 is default port that main logging and all serial print output goes to
    // (pins RX/TX on board, GPIO3/1 respectively)
    // UART_NUM_1 only has a TX pin which is perfect for us (pin D4, GPIO2)
    // port, rx buf size, rx buf size, queue size, queue handle, irrelevant
    uart_driver_install(UART_NUM_1, UART_BUF_SIZE * 2, 0, 0, NULL, 0);

    uart_buffer = (uint8_t *)malloc(UART_BUF_SIZE);
}
