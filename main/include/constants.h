#ifndef CONSTANTS_H
#define CONSTANTS_H

// Logging tag prepended to all serial output from ESP_LOGI
#define TAG "[tides]"

// Set default minimum level logged to output
#define LOG_LOCAL_LEVEL ESP_LOG_WARN

// Set true if flashing to small ESP-01 module on custom PCB,
// false if flashing to large NodeMCU LiLo dev board
#define ESP_01 true

#endif
