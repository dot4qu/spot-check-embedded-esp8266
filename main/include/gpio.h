#ifndef GPIO_H
#define GPIO_H

#include "constants.h"

#if ESP_01
#define GPIO_BUTTON_PIN     2
#else
#define GPIO_BUTTON_PIN     13
#endif

#define GPIO_OUTPUT_PIN_SEL  (1 <<GPIO_ERROR_PIN | 1 << GPIO_SUCCESS_PIN);
#define GPIO_INPUT_PIN_SEL   (1 << GPIO_BUTTON_PIN)

volatile uint8_t button_pressed;

void init_gpio();
bool button_was_released();

#endif
