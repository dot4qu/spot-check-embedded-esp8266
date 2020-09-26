#ifndef CONSTANTS_H
#define CONSTANTS_H

// Set true if flashing to small ESP-01 module on custom PCB,
// false if flashing to large NodeMCU LiLo dev board
#define ESP_01 true

// Set to true to send requests on button interrupt,
// false to send periodically every X seconds
#define BUTTON_FOR_REQUESTS false

// Logging tag prepended to all serial output from ESP_LOGI
#define TAG "[tides]"

// Set default minimum level logged to output as WARN if we're on-board to
// prevent spamming the arduino serial connection with all of the logs
#ifndef ESP_01
#assert "must define ESP_01 as true or false depending on if running on-board or with dev board"
#endif

#if ESP_01
#define LOG_LOCAL_LEVEL ESP_LOG_WARN
#else
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#endif


#endif
