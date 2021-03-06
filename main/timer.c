#include "freeRTOS/FreeRTOS.h"
#include "driver/hw_timer.h"

#include "timer.h"

void init_timer(void *timer_expired_callback) {
    timer_count = 0;
    timer_expired = false;

    // Init code adapted from the hw_timer.c hw_timer_alarm_us function
    // used for abstracting timer load and start
    ESP_ERROR_CHECK(hw_timer_init(timer_expired_callback, NULL));
    hw_timer_set_clkdiv(TIMER_CLKDIV_16);

    // Reload new timer val on expiration
    bool reload = true;
    hw_timer_set_reload(reload);
    hw_timer_set_intr_type(TIMER_EDGE_INT);

    // alarm_us handles load in and enable of timer and settings
    // Use the same assert check as the hw_timer.c init code
    int timer_period_us = TIMER_PERIOD_MS * 1000;
    assert((reload ? ((timer_period_us > 50) ? 1 : 0) : ((timer_period_us > 10) ? 1 : 0)) && (timer_period_us <= 0x199999));

#if BUTTON_FOR_REQUESTS
    // Timer will be used for debouncing, make sure you set a short period
    assert(TIMER_PERIOD_MS < 200);
    hw_timer_set_load_data(((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * timer_period_us);
    hw_timer_enable(true);
#else
    // Timer will be used to send requests, make sure you set a long period
    assert(TIMER_PERIOD_MS > 200);
    reset_timer();
#endif
}

void reset_timer() {
    hw_timer_set_load_data(((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * (TIMER_PERIOD_MS * 1000));
    timer_expired = false;

    // Start timer on our first call to reset
    if (!hw_timer_get_enable()) {
        hw_timer_enable(true);
    }
}
