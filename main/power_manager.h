/**
 * @file power_manager.h
 * @brief Deep sleep, wake sources, and power timers.
 * @ingroup power
 */

#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    WAKEUP_SOURCE_NONE,           // Not a deep sleep wakeup (cold boot, reset, etc.)
    WAKEUP_SOURCE_TIMER,          // Timer wakeup (auto-rotate)
    WAKEUP_SOURCE_BOOT_BUTTON,    // BOOT/WAKEUP button pressed
    WAKEUP_SOURCE_ROTATE_BUTTON,  // ROTATE button pressed
    WAKEUP_SOURCE_CLEAR_BUTTON,   // CLEAR button pressed
    WAKEUP_SOURCE_EXT1_UNKNOWN,   // EXT1 wakeup from unknown GPIO
} wakeup_source_t;

/**
 * @brief Initialize wake sources, timers, and power-saving features.
 */
esp_err_t power_manager_init(void);

/**
 * @brief Enter deep sleep with configured wake sources.
 * @see flow_deep_sleep
 */
void power_manager_enter_sleep(void);

/**
 * @brief Enter deep sleep for a fixed duration in seconds.
 */
void power_manager_enter_sleep_with_timer(uint32_t sleep_time_sec);

/**
 * @brief Reset the auto-sleep countdown.
 */
void power_manager_reset_sleep_timer(void);

/**
 * @brief Reset the auto-rotation timer.
 */
void power_manager_reset_rotate_timer(void);

/**
 * @brief Get the wakeup source for the current boot.
 */
wakeup_source_t power_manager_get_wakeup_source(void);

/**
 * @brief Enable or disable deep sleep and update indicators.
 */
void power_manager_set_deep_sleep_enabled(bool enabled);

#endif
