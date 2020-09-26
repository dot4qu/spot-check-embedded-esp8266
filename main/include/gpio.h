#ifndef GPIO_H
#define GPIO_H

#define GPIO_BUTTON_PIN     13
#define GPIO_OUTPUT_PIN_SEL  (1 <<GPIO_ERROR_PIN | 1 << GPIO_SUCCESS_PIN);
#define GPIO_INPUT_PIN_SEL   (1 << GPIO_BUTTON_PIN)

volatile uint8_t button_pressed;

void init_gpio();
bool button_was_released();

#endif
