#include "freertos/FreeRTOS.h"
#include "cJSON.h"

#include "constants.h"
#include "json.h"

// Must included below constants.h where we overwite the define of LOG_LOCAL_LEVEL
#include "esp_log.h"

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
