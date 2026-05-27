#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_manager_init(void);
esp_err_t display_manager_show_image(const char *filename);

esp_err_t display_manager_show_calibration(void);
esp_err_t display_manager_clear(void);
esp_err_t display_manager_show_setup_screen(void);
bool display_manager_is_busy(void);
void display_manager_rotate_from_storage(void);
const char *display_manager_get_current_image(void);
void display_manager_initialize_paint(void);

/**
 * @brief Display an RGB buffer directly on the e-paper display
 *
 * This function takes an already-processed RGB buffer (with colors matching
 * the 6-color palette) and displays it directly. This is more efficient for
 * SD-card-less systems where no file I/O is needed.
 *
 * @param rgb_buffer RGB888 buffer (3 bytes per pixel, already dithered to palette)
 * @param width Image width
 * @param height Image height
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_manager_show_rgb_buffer(const uint8_t *rgb_buffer, int width, int height);

// ============================================================================
// Bus Arrival Timing Screen
// ============================================================================

#define DISPLAY_MANAGER_BUS_MAX_STOPS 2
#define DISPLAY_MANAGER_BUS_MAX_SERVICES 3
#define DISPLAY_MANAGER_BUS_MAX_ARRIVALS 2

typedef struct {
    const char *service_no;
    const char *arrivals[DISPLAY_MANAGER_BUS_MAX_ARRIVALS];
} display_manager_bus_service_t;

typedef struct {
    const char *stop_name;
    const char *stop_desc;
    const char *stop_id;
    const display_manager_bus_service_t *services;
    size_t service_count;
} display_manager_bus_stop_t;

typedef struct {
    const char *title;
    const char *last_updated;
    const display_manager_bus_stop_t *stops;
    size_t stop_count;
} display_manager_bus_screen_t;

/**
 * @brief Display a bus arrival timing screen rendered from structured data.
 *
 * @param screen Screen layout + timing data to render
 * @return esp_err_t ESP_OK on success
 */
esp_err_t display_manager_show_bus_timing_screen(const display_manager_bus_screen_t *screen);

#endif
