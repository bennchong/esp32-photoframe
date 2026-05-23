/**
 * @file ha_integration.h
 * @brief Home Assistant notifications and integration helpers.
 * @ingroup integration
 */

#ifndef HA_INTEGRATION_H
#define HA_INTEGRATION_H

#include <stdbool.h>

#include "esp_err.h"

/**
 * @brief Check if Home Assistant URL is configured
 *
 * @return true if HA URL is configured, false otherwise
 */
bool ha_is_configured(void);

/**
 * @brief Notify Home Assistant that device is online (triggers HA to poll all data via REST API)
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ha_notify_online(void);

/**
 * @brief Notify Home Assistant that device is going offline (entering deep sleep)
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ha_notify_offline(void);

/**
 * @brief Notify Home Assistant that device has new data available (e.g., after OTA check)
 *
 * @return ESP_OK on success, ESP_FAIL on error
 */
esp_err_t ha_notify_update(void);

#endif
