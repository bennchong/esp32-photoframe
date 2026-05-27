/**
 * @file ota_manager.h
 * @brief Firmware update discovery and installation.
 * @ingroup ota
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief OTA update state machine.
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_UPDATE_AVAILABLE,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_INSTALLING,
    OTA_STATE_SUCCESS,
    OTA_STATE_ERROR
} ota_state_t;

typedef struct {
    ota_state_t state;
    char current_version[32];
    char latest_version[32];
    char error_message[128];
    int progress_percent;
} ota_status_t;

/**
 * @brief Initialize OTA status and periodic check scheduling.
 */
esp_err_t ota_manager_init(void);

/**
 * @brief Check for available firmware updates.
 * @see flow_ota
 */
esp_err_t ota_check_for_update(bool *update_available, int timeout);

/**
 * @brief Start downloading and installing the update.
 * @see flow_ota
 */
esp_err_t ota_start_update(void);

/**
 * @brief Get the current OTA status snapshot.
 */
void ota_get_status(ota_status_t *status);

/**
 * @brief Return the running firmware version string.
 */
const char *ota_get_current_version(void);

/**
 * @brief Determine whether the daily OTA check is due.
 */
bool ota_should_check_daily(void);

/**
 * @brief Persist the last OTA check timestamp.
 */
void ota_update_last_check_time(void);

#endif  // OTA_MANAGER_H
