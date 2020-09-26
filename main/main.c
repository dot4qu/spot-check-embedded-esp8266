#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_event.h"
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "cJSON.h"

#include "driver/gpio.h"

#include "constants.h"
#include "uart.h"
#include "gpio.h"
#include "timer.h"
#include "network.h"
#include "json.h"

// Must included below constants.h where we overwite the define of LOG_LOCAL_LEVEL
#include "esp_log.h"


void timer_expired_callback(void *timer_args) {
    // timer_expired = true;
    timer_count += 1;
}

void button_isr_handler(void *arg) {
    button_pressed = !(bool)gpio_get_level(GPIO_BUTTON_PIN);
}

volatile bool tides = true;
void app_main(void)
{
    // Create default event loop - handle hidden from user so no return
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_uart();
    init_gpio(button_isr_handler);
    init_wifi();
    init_http();
    init_timer(timer_expired_callback);

    while (1) {
        esp_task_wdt_reset();
        // if (button_was_released()) {
        if (timer_count >= 4) {
            timer_count = 0;
            // timer_expired = false;
            // Sometimes stuff gets screwy and run out of sockets. When that happens
            // we fully cleanup our http_client and re-init it
            if (!http_client_inited) {
                ESP_LOGI(TAG, "http_client not yet inited, doing it now before request");
                init_wifi();
                init_http();
            }

            // Space for base url, endpoint, and some extra
            char url_buf[strlen(URL_BASE) + 20];
            request request;
            query_param params[2];
            if (tides) {
                request = build_request("tides", "wedge", "2", url_buf, params);
                tides = false;
            } else {
                request = build_request("swell", "wedge", "2", url_buf, params);
                tides = true;
            }

            char *server_response;
            int data_length = perform_request(&request, &server_response);
            if (data_length != 0) {
                cJSON *json = parse_json(server_response);

                cJSON *data_value = cJSON_GetObjectItem(json, "data");
                int values_written = send_json_list(data_value);
                assert(values_written > 0);

                cJSON_free(data_value);
                cJSON_free(json);
            }

            // Caller responsible for freeing buffer if non-null on return
            if (server_response != NULL) {
                free(server_response);
            }
        }
    }
}
