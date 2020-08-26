#define LOG_LOCAL_LEVEL ESP_LOG_WARN

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "cJSON.h"
#include "esp_task_wdt.h"

#include "driver/gpio.h"
#include "driver/hw_timer.h"
#include "driver/uart.h"

// #include "gdbstub.h"
// #include "gdbstub-cfg.h"

#define UART_BUF_SIZE (1024)

#define GPIO_BUTTON_PIN     13
#define GPIO_OUTPUT_PIN_SEL  (1 <<GPIO_ERROR_PIN | 1 << GPIO_SUCCESS_PIN);
#define GPIO_INPUT_PIN_SEL   (1 << GPIO_BUTTON_PIN)

#define SSID CONFIG_ESP_WIFI_SSID
#define PASSWORD CONFIG_ESP_WIFI_PASSWORD
#define MAX_RETRY CONFIG_ESP_MAXIMUM_RETRY
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MAX_READ_BUFFER_SIZE 4096
#define START_LIST_TRANSMISSION_COMMAND "START_LIST%"
#define END_LIST_TRANSMISSION_COMMAND "END_LIST%"

#define RELOAD_ON_EXPIRATION true
#define TIMER_PERIOD_MS (1000)

#define URL_BASE "http://192.168.1.70/"
#define MAX_QUERY_PARAM_LENGTH 15

typedef struct {
    char* key;
    char* value;
} query_param;

typedef struct {
    char *url;
    query_param *params;
    uint8_t num_params;
} request;


typedef enum {
    WAITING_FOR_PRESS,
    DEBOUNCING_PRESS,
    DEBOUNCING_RELEASE,
    WAITING_FOR_RELEASE
} debounce_state;

static uint8_t *uart_buffer;
static EventGroupHandle_t wifi_event_group;                 // Event group to signal when connected to the AP
static volatile int retry_count = 0;
static volatile bool timer_expired = true;
volatile uint8_t button_pressed = false;
static const char *TAG = "[tides]";
static esp_http_client_handle_t client;
static bool http_client_inited = false;
static volatile debounce_state current_state;

void button_isr_handler(void *arg);
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
esp_err_t http_event_handler(esp_http_client_event_t *event);
void timer_expired_callback(void *timer_args);
void reset_timer();

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

void init_gpio() {
    gpio_config_t input_config;
    input_config.intr_type = GPIO_INTR_ANYEDGE;
    input_config.mode = GPIO_MODE_INPUT;
    input_config.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    input_config.pull_down_en = 0;
    input_config.pull_up_en = 1;

    gpio_config(&input_config);
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(GPIO_BUTTON_PIN, &button_isr_handler, (void *)GPIO_BUTTON_PIN));
}

/**
 * Instantiate an event group and register 2 events on the default event handler,
 * both of which call `wifi_event_handler`.
 * THIS WILL BLOCK UNTIL EVENT RECEIVED OR `portMAX_DELAY` EXPIRES
 */
void init_wifi() {
    if (!wifi_event_group) {
        wifi_event_group = xEventGroupCreate();
    }
    tcpip_adapter_init();

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_config));

    // Register both connection and getting IP events with the
    // default loop with our single handler as the callback
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL
    ));

    // We want to connect to AP, not be one
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t sta_config = {
        .sta = {
            .ssid = SSID,
            .password = PASSWORD
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Block and wait for bits set from event handler
    EventBits_t wifi_bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if (wifi_bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 SSID, PASSWORD);
    } else if (wifi_bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 SSID, PASSWORD);
    } else {
        ESP_LOGI(TAG, "Unknown event: %x", wifi_bits);
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler
    ));

    ESP_ERROR_CHECK(esp_event_handler_unregister(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler
    ));

    vEventGroupDelete(wifi_event_group);
    ESP_LOGI(TAG, "Succesfully set up and connected to wifi");
}

void init_http() {
    if (http_client_inited) {
        ESP_LOGI(TAG, "http client already set up, no need to re-init");
        return;
    }

    ESP_LOGI(TAG, "initing http client...");

    // TODO :: build this URL with the same logic that
    // build_request uses to prevent wasting time forgetting
    // to update BASE_URL #define...
    esp_http_client_config_t http_config = {
        .url = "http://192.168.1.70/tides?spot_name=wedge&days=2",
        .event_handler = http_event_handler,
        .buffer_size = MAX_READ_BUFFER_SIZE
    };

    client = esp_http_client_init(&http_config);
    if (!client) {
        ESP_LOGI(TAG, "Error initing http client");
        return;
    }

    http_client_inited = true;
    ESP_LOGI(TAG, "Successful init of http client");
}

// Does NOT start timer, must use reset_timer to start count
void init_timer() {
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

    // If you want to pull the timer back in to test some stuff
    // and not use it for debouncing, enable it below with correct period
    // hw_timer_set_load_data(((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * (TIMER_PERIOD_MS * 1000));
    // hw_timer_enable(true);
    reset_timer();
}

// Reset timer and begin counting up to period again.
// Clears interrupt flag as well
void reset_timer() {
    hw_timer_set_load_data(((TIMER_BASE_CLK >> hw_timer_get_clkdiv()) / 1000000) * (TIMER_PERIOD_MS * 1000));
    timer_expired = false;

    // Start timer on our first call to reset
    if (!hw_timer_get_enable()) {
        hw_timer_enable(true);
    }
}

/*
 * request obj is optional, but highly recommended to ensure the
 * right url/params are set up. If not supplied, request will be
 * performed using whatever was last set.
 */
int perform_request(request *request_obj, char **read_buffer) {
    if (request_obj) {
        // assume we won't have that many query params. Could calc this too
        char req_url[strlen(request_obj->url) + 40];
        strcpy(req_url, request_obj->url);
        strcat(req_url, "?");
        for (int i = 0; i < request_obj->num_params; i++) {
            query_param param = request_obj->params[i];
            strcat(req_url, param.key);
            strcat(req_url, "=");
            strcat(req_url, param.value);
        }

        ESP_ERROR_CHECK(esp_http_client_set_url(client, req_url));
        ESP_LOGI(TAG, "Setting url to %s\n", req_url);
    }

    esp_err_t error = esp_http_client_perform(client);
    if (error != ESP_OK) {
        const char *err_text = esp_err_to_name(error);
        ESP_LOGI(TAG, "Error performing test GET, error: %s", err_text);

        // clean up and re-init client
        error = esp_http_client_cleanup(client);
        if (error != ESP_OK) {
            ESP_LOGI(TAG, "Error cleaning up  http client connection");
        }

        http_client_inited = false;
        return NULL;
    }

    int content_length = esp_http_client_get_content_length(client);
    int status = esp_http_client_get_status_code(client);
    if (status >= 200 && status <= 299) {
        ESP_LOGI(TAG, "GET success! Status=%d, Content-length=%d", status, content_length);
    } else {
        ESP_LOGI(TAG, "GET failed. Status=%d, Content-length=%d", status, content_length);
        error = esp_http_client_close(client);
        if (error != ESP_OK) {
            const char *err_str = esp_err_to_name(error);
            ESP_LOGI(TAG, "Error closing http client connection: %s", err_str);
            return 0;
        }
    }

    int alloced_space_used = 0;
    if (content_length < MAX_READ_BUFFER_SIZE) {
        // Did a lot of work here to try to read into buffer in chunks since default response buffer
        // inside client is inited to ~512 bytes but something's borked in the SDK. This is technically
        // double-allocating buffers of MAX_READ_BUFFER_SIZE since there's one internally and another
        // here, but hopefully the quick malloc/free shouldn't cause any issues
        *read_buffer = malloc(content_length + 1);
        int length_received = esp_http_client_read(client, *read_buffer, content_length);
        (*read_buffer)[length_received + 1] = '\0';
        alloced_space_used = length_received + 1;
    } else {
        ESP_LOGI(TAG, "Not enough room in read buffer: buffer=%d, content=%d", MAX_READ_BUFFER_SIZE, content_length);
    }

    // Close current connection but don't free http_client data and un-init with cleanup
    error = esp_http_client_close(client);
    if (error != ESP_OK) {
        const char *err_str = esp_err_to_name(error);
        ESP_LOGI(TAG, "Error closing http client connection: %s", err_str);
    }

    return alloced_space_used;
}

void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Got sta_start, initiating wifi_connect");
            ESP_ERROR_CHECK(esp_wifi_connect());
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (retry_count < MAX_RETRY) {
                retry_count++;
                ESP_ERROR_CHECK(esp_wifi_connect());
                ESP_LOGI(TAG, "Received discon, trying to reconnect");
            } else {
                ESP_LOGI(TAG, "Failed max retries, setting fail bit");
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            }
        } else {
            ESP_LOGI(TAG, "Got wifi event but not STA start or discon");
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "Got IP addr: %s", ip4addr_ntoa(&event->ip_info.ip));
            retry_count = 0;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        } else {
            ESP_LOGI(TAG, "Get sta event, but not got_ip one");
        }
    } else {
        ESP_LOGI(TAG, "Unknown event base: %s", event_base);
    }
}

esp_err_t http_event_handler(esp_http_client_event_t *event) {
    switch(event->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", event->header_key, event->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
            if (!esp_http_client_is_chunked_response(event->client)) {
                // Write out data
                // printf("%.*s", evt->data_len, (char*)evt->data);
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

volatile int timer_count = 0;
void timer_expired_callback(void *timer_args) {
    // timer_expired = true;
    timer_count += 1;
}

void button_isr_handler(void *arg) {
    button_pressed = !(bool)gpio_get_level(GPIO_BUTTON_PIN);
}

bool button_was_released() {
    switch (current_state) {
        case WAITING_FOR_PRESS:
            if (button_pressed) {
                reset_timer();
                current_state = DEBOUNCING_PRESS;
                ESP_LOGI(TAG, "Waiting for press to debounce press");
            }
            break;
        case DEBOUNCING_PRESS:
            if (timer_expired) {
                if (button_pressed) {
                    current_state = WAITING_FOR_RELEASE;
                    ESP_LOGI(TAG, "successfully debounced PRESS");
                } else {
                    current_state = WAITING_FOR_PRESS;
                    ESP_LOGI(TAG, "Timer timed out while debouncing press");
                }
            }
        break;
        case WAITING_FOR_RELEASE:
            if (!button_pressed) {
                reset_timer();
                current_state = DEBOUNCING_RELEASE;
                ESP_LOGI(TAG, "Waiting for RELEASE to debounce release");
            }
            break;
        case DEBOUNCING_RELEASE:
            if (timer_expired) {
                if (button_pressed) {
                    current_state = WAITING_FOR_RELEASE;
                    ESP_LOGI(TAG, "Timer timed out while debouncing release");
                } else {
                    current_state = WAITING_FOR_PRESS;
                    ESP_LOGI(TAG, "successfully debounced RELEASE");
                    return true;
                }
            }
            break;
    }

    return false;
}

// Caller passes in endpoint (tides/swell) the values for the 2 query params,
// a pointer to a block of already-allocated memory for the base url + endpoint,
// and a pointer to a block of already-allocated memory to hold the query params structs
request build_request(char* endpoint, char *spot, char *days, char *url_buf, query_param *params) {
    query_param temp_params[] = {
        {
            .key = "days",
            .value = days
        },
        {
            .key = "spot",
            .value = spot
        }
    };

    memcpy(params, temp_params, sizeof(temp_params));

    strcpy(url_buf, URL_BASE);
    strcat(url_buf, endpoint);
    request tide_request = {
        .url = url_buf,
        .params = params,
        .num_params = sizeof(temp_params) / sizeof(query_param)
    };

    return tide_request;
}

cJSON* parse_json(char *server_response) {
    cJSON *json = cJSON_Parse(server_response);
    if (json == NULL) {
        const char *err_ptr = cJSON_GetErrorPtr();
        if (err_ptr) {
            ESP_LOGI(TAG, "JSON parsing err: %s\n", err_ptr);
        }
    }

    return json;
}

/*
 * Takes in a cJSON pointer pointing to a json list object.
 * This will be sent serially using the command format of
 * [start command], [data]$, [data]$, [end command]
 * For example:
 * START_LIST%
 * This is a tide string$
 * This is another tide string$
 * .....
 * END_LIST%
 */
int send_json_list(cJSON *list_json) {
    int num_sent = 0;

    // Write our command to signal to the arduino we're about to start
    // sending a list of strings to display
    // uart_write_bytes(UART_NUM_1, START_LIST_TRANSMISSION_COMMAND, sizeof(START_LIST_TRANSMISSION_COMMAND) - 1);
    printf("%s\n", START_LIST_TRANSMISSION_COMMAND);

    // ESP_LOGI(TAG, "Sending string: %s\n", START_LIST_TRANSMISSION_COMMAND);
    cJSON *data_list_value = NULL;
    cJSON_ArrayForEach(data_list_value, list_json) {
        char *text = cJSON_GetStringValue(data_list_value);

        // ESP_LOGI(TAG, text);
        // Write string and '$' terminator to tell arduino to store everything
        // received so far as a new array element
        // uart_write_bytes(UART_NUM_1, (const char *)text, strlen(text));
        // uart_write_bytes(UART_NUM_1, "$", 1);
        printf("%s$\n", text);
        // ESP_LOGI(TAG, text);
        cJSON_free(text);
        num_sent++;
    }

    // Arduino knows it can stop looking for '$' terminated strings and
    // display what it's stored in its array
    // uart_write_bytes(UART_NUM_1, END_LIST_TRANSMISSION_COMMAND, sizeof(END_LIST_TRANSMISSION_COMMAND) - 1);
    printf("%s\n", END_LIST_TRANSMISSION_COMMAND);
    // ESP_LOGI(TAG, "Sending string: %s\n", END_LIST_TRANSMISSION_COMMAND);


    return num_sent;
}

volatile bool tides = true;
void app_main(void)
{
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    current_state = WAITING_FOR_PRESS;

    // Create default event loop - handle hidden from user so no return
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    init_uart();
    init_gpio();
    init_wifi();
    init_http();
    init_timer();

    while (1) {
        esp_task_wdt_reset();
        // if (button_was_released()) {
        if (timer_count >= 4) {
            timer_count = 0;
            // timer_expired = false;
            // Sometimes we get stuff screwy and run out of sockets. When that happens
            // we fully cleanup our http_client and re-init it
            if (!http_client_inited) {
                ESP_LOGI(TAG, "http_client not yet inited, doing it now before request");
                init_wifi();
                init_http();
            }

            request request;
            // Space for base url, endpoint, and some extra
            char url_buf[strlen(URL_BASE) + 20];
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
