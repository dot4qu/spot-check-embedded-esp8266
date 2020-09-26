#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"

#include "gpio.h"
#include "timer.h"

typedef enum {
    WAITING_FOR_PRESS,
    DEBOUNCING_PRESS,
    DEBOUNCING_RELEASE,
    WAITING_FOR_RELEASE
} debounce_state;

static volatile debounce_state current_state;

void init_gpio(gpio_isr_t button_isr_handler) {
    button_pressed = false;
    current_state = WAITING_FOR_PRESS;

    gpio_config_t input_config;
    input_config.intr_type = GPIO_INTR_ANYEDGE;
    input_config.mode = GPIO_MODE_INPUT;
    input_config.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    input_config.pull_down_en = 0;
    input_config.pull_up_en = 1;

    gpio_config(&input_config);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON_PIN, button_isr_handler, (void *)GPIO_BUTTON_PIN));
}

bool button_was_released() {
    switch (current_state) {
        case WAITING_FOR_PRESS:
            if (button_pressed) {
                reset_timer();
                current_state = DEBOUNCING_PRESS;
            }
            break;
        case DEBOUNCING_PRESS:
            if (timer_expired) {
                if (button_pressed) {
                    current_state = WAITING_FOR_RELEASE;
                } else {
                    current_state = WAITING_FOR_PRESS;
                }
            }
        break;
        case WAITING_FOR_RELEASE:
            if (!button_pressed) {
                reset_timer();
                current_state = DEBOUNCING_RELEASE;
            }
            break;
        case DEBOUNCING_RELEASE:
            if (timer_expired) {
                if (button_pressed) {
                    current_state = WAITING_FOR_RELEASE;
                } else {
                    current_state = WAITING_FOR_PRESS;
                    return true;
                }
            }
            break;
    }

    return false;
}
