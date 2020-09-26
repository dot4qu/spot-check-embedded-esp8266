#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_err.h"

#include "constants.h"
#include "network.h"

// Must included below constants.h where we overwite the define of LOG_LOCAL_LEVEL
#include "esp_log.h"

#define MAX_QUERY_PARAM_LENGTH 15
#define SSID CONFIG_ESP_WIFI_SSID
#define PASSWORD CONFIG_ESP_WIFI_PASSWORD
#define MAX_RETRY CONFIG_ESP_MAXIMUM_RETRY
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define MAX_READ_BUFFER_SIZE 4096

// Event group to signal when connected to the AP
static EventGroupHandle_t wifi_event_group;
static volatile int retry_count = 0;
static esp_http_client_handle_t client;

bool http_client_inited = false;

// Forward declarations for handlers used in init functions
void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
esp_err_t http_event_handler(esp_http_client_event_t *event);

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
