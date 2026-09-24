#include "display_manager.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "GUI_BMPfile.h"
#include "GUI_EPDGZfile.h"
#include "GUI_PNGfile.h"
#include "GUI_Paint.h"
#include "GUI_RawBuffer.h"
#include "album_manager.h"
#include "board_hal.h"
#include "config.h"
#include "config_manager.h"
#include "epaper.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "storage.h"
#include "utils.h"

static const char *TAG = "display_manager";
#define NVS_LAST_IMAGE_KEY "last_image"

static SemaphoreHandle_t display_mutex = NULL;
static char current_image[64] = {0};
static char last_displayed_image[256] = {0};  // Internal state: last displayed image path

static uint8_t *epd_image_buffer = NULL;
static uint32_t image_buffer_size;

// Load last displayed image from NVS
static void load_last_displayed_image(void)
{
    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle) == ESP_OK) {
        size_t len = sizeof(last_displayed_image);
        if (nvs_get_str(nvs_handle, NVS_LAST_IMAGE_KEY, last_displayed_image, &len) == ESP_OK) {
            ESP_LOGI(TAG, "Loaded last displayed image: %s", last_displayed_image);
        } else {
            last_displayed_image[0] = '\0';
        }
        nvs_close(nvs_handle);
    }
}

// Save last displayed image to NVS
static void save_last_displayed_image(const char *filename)
{
    if (filename == NULL) {
        return;
    }

    strncpy(last_displayed_image, filename, sizeof(last_displayed_image) - 1);
    last_displayed_image[sizeof(last_displayed_image) - 1] = '\0';

    nvs_handle_t nvs_handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_str(nvs_handle, NVS_LAST_IMAGE_KEY, last_displayed_image);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "Saved last displayed image: %s", last_displayed_image);
}

// Helper function to create link file pointing to current image
static void create_image_link(const char *target_path)
{
    FILE *fp = fopen(CURRENT_IMAGE_LINK, "w");
    if (fp) {
        fprintf(fp, "%s", target_path);
        fclose(fp);
        ESP_LOGD(TAG, "Created link file pointing to: %s", target_path);
    } else {
        ESP_LOGE(TAG, "Failed to create link file");
    }
}

// Bus arrival screen layout. Font24 (17x24) is the only Latin font, so text sizes are whole
// multiples of it.
#define BUS_MARGIN 10
#define BUS_PANEL_GAP 14
#define BUS_BAND_HEIGHT 36
#define BUS_GAP 6
#define BUS_ROW_MAX_HEIGHT 84
#define BUS_SERVICE_WIDTH 116
#define BUS_CELL_PADDING 3
// Font24 digits are inked on glyph rows 2-16: their visual centre is 9.5 rows below the glyph
// top and the baseline is the 17th row.
#define BUS_FONT_INK_CENTER_X2 19
#define BUS_FONT_BASELINE_ROWS 17

static uint16_t bus_text_width(const char *text, uint8_t scale)
{
    return text ? strlen(text) * Font24.Width * scale : 0;
}

// Largest scale up to max_scale at which text fits in max_width
static uint8_t bus_fit_scale(const char *text, uint16_t max_width, uint8_t max_scale)
{
    uint8_t scale = max_scale;
    while (scale > 1 && bus_text_width(text, scale) > max_width) {
        scale--;
    }
    return scale;
}

// Top y that visually centres digits and capitals on center_y
static uint16_t bus_text_top(uint16_t center_y, uint8_t scale)
{
    uint16_t offset = (BUS_FONT_INK_CENTER_X2 * scale) / 2;
    return center_y > offset ? center_y - offset : 0;
}

static void bus_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint8_t scale)
{
    // Paint_DrawString_EN_Scaled() hands its two colours to Paint_DrawChar_Scaled() swapped (a
    // quirk inherited from the Waveshare library), so the glyph colour goes in the second slot.
    Paint_DrawString_EN_Scaled(x, y, text, &Font24, EPD_7IN3E_WHITE, color, scale, true);
}

// Draw as many characters of text as fit in max_width
static void bus_draw_text_clipped(uint16_t x, uint16_t y, uint16_t max_width, const char *text,
                                  uint16_t color, uint8_t scale)
{
    char clipped[48];
    int max_chars = max_width / (Font24.Width * scale);
    if (!text || max_chars <= 0) {
        return;
    }
    snprintf(clipped, sizeof(clipped), "%.*s", max_chars, text);
    bus_draw_text(x, y, clipped, color, scale);
}

static void bus_draw_text_centered(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                   const char *text, uint16_t color, uint8_t max_scale)
{
    uint16_t max_width = width - 2 * BUS_CELL_PADDING;
    uint8_t scale = bus_fit_scale(text, max_width, max_scale);
    uint16_t text_width = bus_text_width(text, scale);
    if (text_width > max_width) {
        text_width = max_width - max_width % (Font24.Width * scale);
    }
    bus_draw_text_clipped(x + (width - text_width) / 2, bus_text_top(y + height / 2, scale),
                          max_width, text, color, scale);
}

// Paint_DrawRectangle() leaves out the last row when filling, so pass an exclusive end
static void bus_fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    Paint_DrawRectangle(x, y, x + width - 1, y + height, color, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

static void bus_draw_cell(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t fill)
{
    bus_fill(x, y, width, height, fill);
    Paint_DrawRectangle(x, y, x + width - 1, y + height - 1, EPD_7IN3E_BLACK, DOT_PIXEL_1X1,
                        DRAW_FILL_EMPTY);
}

static void bus_draw_service_cell(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  const char *service_no)
{
    bus_draw_cell(x, y, width, height, EPD_7IN3E_BLACK);
    bus_draw_text_centered(x, y, width, height, (service_no && service_no[0]) ? service_no : "?",
                           EPD_7IN3E_WHITE, 2);
}

// arrival is "ARR" or whole minutes; anything else is shown as-is and NULL/empty as "-"
static void bus_draw_arrival_cell(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                  const char *arrival)
{
    const char *text = "-";
    const char *unit = NULL;
    uint16_t fill = EPD_7IN3E_WHITE;
    uint16_t ink = EPD_7IN3E_BLACK;

    if (arrival && arrival[0]) {
        char *end = NULL;
        long minutes = strtol(arrival, &end, 10);
        bool numeric = end != arrival && *end == '\0';

        text = arrival;
        if (strcasecmp(arrival, "ARR") == 0 || (numeric && minutes <= 0)) {
            text = "Arr";
            fill = EPD_7IN3E_RED;
            ink = EPD_7IN3E_WHITE;
        } else if (numeric) {
            unit = "min";
            // White reads better than black on the panel's dark green
            fill = (minutes <= 5) ? EPD_7IN3E_GREEN : EPD_7IN3E_YELLOW;
            ink = (minutes <= 5) ? EPD_7IN3E_WHITE : EPD_7IN3E_BLACK;
        }
    }

    bus_draw_cell(x, y, width, height, fill);

    // "<n> min": the number at 2x with the unit at 1x on its baseline, dropping the unit when the
    // pair doesn't fit
    uint16_t max_width = width - 2 * BUS_CELL_PADDING;
    uint8_t scale = bus_fit_scale(text, max_width, 2);
    uint16_t text_width = bus_text_width(text, scale);
    uint16_t unit_gap = 2;
    if (unit && (scale < 2 || text_width + unit_gap + bus_text_width(unit, 1) > max_width)) {
        unit = NULL;
    }
    if (!unit) {
        bus_draw_text_centered(x, y, width, height, text, ink, 2);
        return;
    }

    uint16_t group_width = text_width + unit_gap + bus_text_width(unit, 1);
    uint16_t text_x = x + (width - group_width) / 2;
    uint16_t text_y = bus_text_top(y + height / 2, scale);
    bus_draw_text(text_x, text_y, text, ink, scale);
    bus_draw_text(text_x + text_width + unit_gap, text_y + BUS_FONT_BASELINE_ROWS * (scale - 1),
                  unit, ink, 1);
}

// Black band with the stop code in a white badge, followed by the stop name
static void bus_draw_stop_band(uint16_t x, uint16_t y, uint16_t width,
                               const display_manager_bus_stop_t *stop)
{
    bool has_id = stop->stop_id && stop->stop_id[0];
    const char *name = (stop->stop_name && stop->stop_name[0]) ? stop->stop_name : stop->stop_desc;
    if ((!name || !name[0]) && !has_id) {
        name = "Bus stop";
    }

    uint16_t text_x = x + 8;
    uint16_t text_y = y + (BUS_BAND_HEIGHT - Font24.Height) / 2;

    bus_fill(x, y, width, BUS_BAND_HEIGHT, EPD_7IN3E_BLACK);

    if (has_id) {
        uint16_t badge_width = bus_text_width(stop->stop_id, 1) + 12;
        if (badge_width > width / 2) {
            badge_width = width / 2;
        }
        bus_fill(text_x, y + 5, badge_width, BUS_BAND_HEIGHT - 10, EPD_7IN3E_WHITE);
        bus_draw_text_clipped(text_x + 6, text_y, badge_width - 12, stop->stop_id, EPD_7IN3E_BLACK,
                              1);
        text_x += badge_width + 10;
    }

    if (name && name[0] && x + width > text_x + 8) {
        bus_draw_text_clipped(text_x, text_y, x + width - 8 - text_x, name, EPD_7IN3E_WHITE, 1);
    }
}

// One stop: its band, then a row per service with the service number and next arrivals
static void bus_draw_stop_panel(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                const display_manager_bus_stop_t *stop, uint16_t row_height)
{
    bus_draw_stop_band(x, y, width, stop);

    uint16_t rows_y = y + BUS_BAND_HEIGHT + BUS_GAP;
    size_t service_count = stop->services ? stop->service_count : 0;
    if (service_count > DISPLAY_MANAGER_BUS_MAX_SERVICES) {
        service_count = DISPLAY_MANAGER_BUS_MAX_SERVICES;
    }
    if (service_count == 0) {
        bus_draw_text_centered(x, rows_y, width, y + height - rows_y, "No services",
                               EPD_7IN3E_BLACK, 1);
        return;
    }

    uint16_t arrival_width =
        (width - BUS_SERVICE_WIDTH - BUS_GAP * DISPLAY_MANAGER_BUS_MAX_ARRIVALS) /
        DISPLAY_MANAGER_BUS_MAX_ARRIVALS;
    for (size_t row = 0; row < service_count; row++) {
        const display_manager_bus_service_t *service = &stop->services[row];
        uint16_t row_y = rows_y + row * (row_height + BUS_GAP);

        bus_draw_service_cell(x, row_y, BUS_SERVICE_WIDTH, row_height, service->service_no);
        for (size_t col = 0; col < DISPLAY_MANAGER_BUS_MAX_ARRIVALS; col++) {
            uint16_t cell_x = x + BUS_SERVICE_WIDTH + BUS_GAP + col * (arrival_width + BUS_GAP);
            bus_draw_arrival_cell(cell_x, row_y, arrival_width, row_height, service->arrivals[col]);
        }
    }
}

esp_err_t display_manager_init(void)
{
    display_mutex = xSemaphoreCreateMutex();
    if (!display_mutex) {
        ESP_LOGE(TAG, "Failed to create display mutex");
        return ESP_FAIL;
    }

    // epaper_port_init() is now called by board_hal_init()

    image_buffer_size = ((BOARD_HAL_DISPLAY_WIDTH % 2 == 0) ? (BOARD_HAL_DISPLAY_WIDTH / 2)
                                                            : (BOARD_HAL_DISPLAY_WIDTH / 2 + 1)) *
                        BOARD_HAL_DISPLAY_HEIGHT;
    epd_image_buffer = (uint8_t *) heap_caps_malloc(image_buffer_size, MALLOC_CAP_SPIRAM);
    if (!epd_image_buffer) {
        ESP_LOGE(TAG, "Failed to allocate image buffer");
        return ESP_FAIL;
    }

    display_manager_initialize_paint();

    ESP_LOGI(TAG, "Display manager initialized");
    ESP_LOGI(TAG, "Auto-rotate uses timer-based wake-up (only works during sleep cycles)");
    return ESP_OK;
}

void display_manager_initialize_paint(void)
{
    Paint_NewImage(epd_image_buffer, BOARD_HAL_DISPLAY_WIDTH, BOARD_HAL_DISPLAY_HEIGHT,
                   config_manager_get_display_rotation_deg() % 360, EPD_7IN3E_WHITE);
    Paint_SetScale(6);
    Paint_SelectImage(epd_image_buffer);
}

esp_err_t display_manager_show_image(const char *filename)
{
    if (!filename || strlen(filename) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire display mutex");
        return ESP_FAIL;
    }

    // Expect absolute path from caller
    ESP_LOGI(TAG, "Displaying image: %s", filename);
    ESP_LOGI(TAG, "Free heap before display: %lu bytes", esp_get_free_heap_size());

    ESP_LOGI(TAG, "Clearing display buffer");
    Paint_Clear(EPD_7IN3E_WHITE);

    // Detect file type by extension
    const char *ext = strrchr(filename, '.');
    bool is_png = (ext != NULL && strcasecmp(ext, ".png") == 0);
    // Check for .epdgz extension
    bool is_epdgz = (ext != NULL && strcasecmp(ext, ".epdgz") == 0);

    if (is_epdgz) {
        ESP_LOGI(TAG, "Reading EPDGZ file into buffer");
        if (GUI_ReadEPDGZ(filename) != 0) {
            ESP_LOGE(TAG, "Failed to read EPDGZ file");
            xSemaphoreGive(display_mutex);
            return ESP_FAIL;
        }
    } else if (is_png) {
        ESP_LOGI(TAG, "Reading PNG file into buffer");
        if (GUI_ReadPng_RGB_6Color(filename, 0, 0) != 0) {
            ESP_LOGE(TAG, "Failed to read PNG file");
            xSemaphoreGive(display_mutex);
            return ESP_FAIL;
        }
    } else {
        ESP_LOGI(TAG, "Reading BMP file into buffer");
        if (GUI_ReadBmp_RGB_6Color(filename, 0, 0) != 0) {
            ESP_LOGE(TAG, "Failed to read BMP file");
            xSemaphoreGive(display_mutex);
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "Starting e-paper display update (this takes ~30 seconds)");
    ESP_LOGI(TAG, "Free heap before epaper_display: %lu bytes", esp_get_free_heap_size());

    // 4. Update E-Paper Display
    // This is a blocking call that takes ~25-30 seconds for 7-color e-paper
    // It handles: Power On -> Send Data -> Refresh -> Power Off
    ESP_LOGI(TAG, "Calling epaper_display...");
    epaper_display(epd_image_buffer);
    ESP_LOGI(TAG, "epaper_display returned successfully");

    ESP_LOGI(TAG, "E-paper display update complete");
    ESP_LOGI(TAG, "Free heap after display: %lu bytes", esp_get_free_heap_size());

    strncpy(current_image, filename, sizeof(current_image) - 1);

    create_image_link(filename);
    ESP_LOGD(TAG, "Created link to: %s", filename);

    xSemaphoreGive(display_mutex);

    ESP_LOGI(TAG, "Image displayed successfully");
    return ESP_OK;
}

esp_err_t display_manager_show_rgb_buffer(const uint8_t *rgb_buffer, int width, int height)
{
    if (!rgb_buffer || width <= 0 || height <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire display mutex");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Displaying RGB buffer: %dx%d", width, height);
    ESP_LOGI(TAG, "Free heap before display: %lu bytes", esp_get_free_heap_size());

    ESP_LOGI(TAG, "Clearing display buffer");
    Paint_Clear(EPD_7IN3E_WHITE);

    ESP_LOGI(TAG, "Painting RGB buffer to display");
    if (GUI_DisplayRGBBuffer_6Color(rgb_buffer, width, height, 0, 0) != 0) {
        ESP_LOGE(TAG, "Failed to paint RGB buffer");
        xSemaphoreGive(display_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Starting e-paper display update (this takes ~30 seconds)");
    ESP_LOGI(TAG, "Free heap before epaper_display: %lu bytes", esp_get_free_heap_size());

    ESP_LOGI(TAG, "Calling epaper_display...");
    epaper_display(epd_image_buffer);
    ESP_LOGI(TAG, "epaper_display returned successfully");

    ESP_LOGI(TAG, "E-paper display update complete");
    ESP_LOGI(TAG, "Free heap after display: %lu bytes", esp_get_free_heap_size());

    // Clear current_image since we displayed from buffer, not file
    current_image[0] = '\0';

    xSemaphoreGive(display_mutex);

    ESP_LOGI(TAG, "RGB buffer displayed successfully");
    return ESP_OK;
}

esp_err_t display_manager_clear(void)
{
    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_FAIL;
    }

    epaper_clear(epd_image_buffer, EPD_7IN3E_WHITE);
    epaper_display(epd_image_buffer);

    // Remove the current image link so API returns 404
    unlink(CURRENT_IMAGE_LINK);
    current_image[0] = '\0';
    save_last_displayed_image("");

    xSemaphoreGive(display_mutex);
    return ESP_OK;
}

esp_err_t display_manager_show_calibration(void)
{
    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire display mutex for calibration");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Displaying calibration pattern");

    // Re-initialize paint with current orientation
    display_manager_initialize_paint();

    // Draw the calibration pattern directly to the buffer
    Paint_DrawCalibrationPattern();

    // Display the buffer
    epaper_display(epd_image_buffer);

    xSemaphoreGive(display_mutex);

    ESP_LOGI(TAG, "Calibration pattern displayed successfully");
    return ESP_OK;
}

esp_err_t display_manager_show_setup_screen(void)
{
    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire display mutex for setup screen");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Displaying setup screen");

    // Re-initialize paint with current orientation
    display_manager_initialize_paint();

    // Display a purple background with simple checkboard dithering pattern
    uint16_t width = Paint.Width;
    uint16_t height = Paint.Height;

    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            // Checkerboard
            if ((x + y) % 2 == 0) {
                Paint_SetPixel(x, y, EPD_7IN3E_RED);
            } else {
                Paint_SetPixel(x, y, EPD_7IN3E_BLUE);
            }
        }
    }

    // 2. Draw Text
    sFONT *font = &Font24;

    char ssid_line[64];
    snprintf(ssid_line, sizeof(ssid_line), "1. Connect WiFi: %s", get_setup_ap_ssid());
    const char *body_lines[] = {"", "Setup required!", ssid_line,
                                "2. Visit URL   : http://192.168.4.1", NULL};

    // Calculate longest body line dynamically
    size_t longest_body_len = 0;
    int num_body_lines = 0;
    while (body_lines[num_body_lines]) {
        size_t len = strlen(body_lines[num_body_lines]);
        if (len > longest_body_len) {
            longest_body_len = len;
        }
        num_body_lines++;
    }

    // Scaling Logic: Best fit for Body Text
    // We check against width with some padding
    uint8_t body_scale = 4;

    while (body_scale > 1) {
        uint16_t required_width = longest_body_len * font->Width * body_scale + (40 * body_scale);
        if (required_width < width) {
            break;
        }
        body_scale--;
    }

    // Title Scale: Try 2x body scale, but ensure it fits
    const char *title_text = "ESP32-PhotoFrame";
    uint8_t title_scale = body_scale * 2;
    size_t title_len = strlen(title_text);

    while (title_scale > body_scale) {  // Don't go smaller than body scale ideally
        uint16_t required_width = title_len * font->Width * title_scale + (20 * title_scale);
        if (required_width < width) {
            break;
        }
        title_scale--;
    }

    ESP_LOGI(TAG, "Setup screen scale - Title: %d, Body: %d", title_scale, body_scale);

    // Calculate Layout Heights
    uint16_t title_height = font->Height * title_scale;
    uint16_t body_line_height = font->Height * body_scale;
    uint16_t padding = 20 * body_scale;  // Base padding

    uint16_t total_content_height = title_height + padding + (num_body_lines * body_line_height) +
                                    ((num_body_lines - 1) * padding);

    uint16_t start_y = (total_content_height < height) ? (height - total_content_height) / 2 : 0;
    uint16_t current_y = start_y;

    // Draw Title
    uint16_t title_width = title_len * font->Width * title_scale;
    uint16_t title_x = (width > title_width) ? (width - title_width) / 2 : 0;

    Paint_DrawString_EN_Scaled(title_x, current_y, title_text, font, EPD_7IN3E_WHITE, WHITE,
                               title_scale, true);

    current_y += title_height + padding;

    // Draw Body Lines
    for (int i = 0; i < num_body_lines; i++) {
        const char *text = body_lines[i];
        size_t len = strlen(text);
        uint16_t text_width = len * font->Width * body_scale;

        uint16_t text_x = (width > text_width) ? (width - text_width) / 2 : 0;

        Paint_DrawString_EN_Scaled(text_x, current_y, text, font, EPD_7IN3E_WHITE, WHITE,
                                   body_scale, true);

        current_y += body_line_height + padding;
    }

    // Display the buffer
    epaper_display(epd_image_buffer);

    xSemaphoreGive(display_mutex);

    ESP_LOGI(TAG, "Setup screen displayed successfully");
    return ESP_OK;
}

esp_err_t display_manager_show_bus_timing_screen(const display_manager_bus_screen_t *screen)
{
    if (!screen) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(display_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire display mutex for bus screen");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Displaying bus arrival timing screen");

    display_manager_initialize_paint();
    Paint_Clear(EPD_7IN3E_WHITE);

    const uint16_t width = Paint.Width;
    const uint16_t height = Paint.Height;

    // Header: title on the left, update time on the right. The title is clipped so the two never
    // overlap on the narrower portrait canvas.
    char updated_text[40] = "";
    if (screen->last_updated && screen->last_updated[0]) {
        snprintf(updated_text, sizeof(updated_text), "Updated %s", screen->last_updated);
    }
    uint16_t header_width = width - 2 * BUS_MARGIN;
    uint16_t updated_width = bus_text_width(updated_text, 1);
    if (updated_width > header_width) {
        updated_width = header_width - header_width % Font24.Width;
    }
    if (updated_width > 0) {
        bus_draw_text_clipped(width - BUS_MARGIN - updated_width, BUS_MARGIN, updated_width,
                              updated_text, EPD_7IN3E_BLACK, 1);
    }
    const char *title = (screen->title && screen->title[0]) ? screen->title : "Bus arrivals";
    int title_width = header_width - (updated_width > 0 ? updated_width + Font24.Width : 0);
    if (title_width > 0) {
        bus_draw_text_clipped(BUS_MARGIN, BUS_MARGIN, title_width, title, EPD_7IN3E_BLACK, 1);
    }

    uint16_t rule_y = BUS_MARGIN + Font24.Height + 6;
    Paint_DrawLine(BUS_MARGIN, rule_y, width - BUS_MARGIN, rule_y, EPD_7IN3E_BLACK, DOT_PIXEL_1X1,
                   LINE_STYLE_SOLID);

    uint16_t content_y = rule_y + 8;
    uint16_t content_height = height - BUS_MARGIN - content_y;

    size_t stop_count = screen->stops ? screen->stop_count : 0;
    if (stop_count > DISPLAY_MANAGER_BUS_MAX_STOPS) {
        stop_count = DISPLAY_MANAGER_BUS_MAX_STOPS;
    }

    if (stop_count == 0) {
        bus_draw_text_centered(0, content_y, width, content_height, "No bus data", EPD_7IN3E_BLACK,
                               2);
    } else {
        // Two stops sit side by side in landscape and stack in portrait
        bool stacked = stop_count > 1 && width < height;
        uint16_t panel_width = width - 2 * BUS_MARGIN;
        uint16_t panel_height = content_height;
        if (stop_count > 1 && stacked) {
            panel_height = (content_height - BUS_PANEL_GAP) / 2;
        } else if (stop_count > 1) {
            panel_width = (panel_width - BUS_PANEL_GAP) / 2;
        }

        // Size the rows for the busiest stop so both panels share one grid
        size_t rows = 1;
        for (size_t i = 0; i < stop_count; i++) {
            const display_manager_bus_stop_t *stop = &screen->stops[i];
            size_t service_count = stop->services ? stop->service_count : 0;
            if (service_count > rows) {
                rows = service_count;
            }
        }
        if (rows > DISPLAY_MANAGER_BUS_MAX_SERVICES) {
            rows = DISPLAY_MANAGER_BUS_MAX_SERVICES;
        }
        uint16_t rows_height = panel_height - BUS_BAND_HEIGHT - BUS_GAP;
        uint16_t row_height = (rows_height - (rows - 1) * BUS_GAP) / rows;
        if (row_height > BUS_ROW_MAX_HEIGHT) {
            row_height = BUS_ROW_MAX_HEIGHT;
        }

        for (size_t i = 0; i < stop_count; i++) {
            uint16_t panel_x = BUS_MARGIN + (stacked ? 0 : i * (panel_width + BUS_PANEL_GAP));
            uint16_t panel_y = content_y + (stacked ? i * (panel_height + BUS_PANEL_GAP) : 0);
            bus_draw_stop_panel(panel_x, panel_y, panel_width, panel_height, &screen->stops[i],
                                row_height);
        }
    }

    epaper_display(epd_image_buffer);
    current_image[0] = '\0';
    xSemaphoreGive(display_mutex);

    ESP_LOGI(TAG, "Bus arrival timing screen displayed successfully");
    return ESP_OK;
}

bool display_manager_is_busy(void)
{
    // Try to take the mutex without blocking
    if (xSemaphoreTake(display_mutex, 0) == pdTRUE) {
        // Mutex was available, give it back
        xSemaphoreGive(display_mutex);
        return false;
    }
    // Mutex is held by another task
    return true;
}

const char *display_manager_get_current_image(void)
{
    return current_image;
}

static void rotate_sequential(char **enabled_albums, int album_count)
{
    ESP_LOGI(TAG, "Sequential rotation mode");
    int32_t last_idx = config_manager_get_last_index();
    int32_t target_idx = last_idx + 1;
    int32_t current_idx = 0;
    char first_image[512] = {0};
    bool found_target = false;

    for (int i = 0; i < album_count; i++) {
        char album_path[256];
        album_manager_get_album_path(enabled_albums[i], album_path, sizeof(album_path));

        DIR *dir = opendir(album_path);
        if (!dir) {
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                if (entry->d_name[0] == '.' && entry->d_name[1] == '_') {
                    continue;
                }

                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".png") == 0 ||
                            strcasecmp(ext, ".epdgz") == 0)) {
                    char fullpath[512];
                    snprintf(fullpath, sizeof(fullpath), "%s/%s", album_path, entry->d_name);
                    ESP_LOGD(TAG, "  Found image [%ld]: %s", (long) current_idx, fullpath);

                    // Keep track of the very first image in case we need to wrap
                    if (first_image[0] == '\0') {
                        strncpy(first_image, fullpath, sizeof(first_image) - 1);
                    }

                    if (current_idx == target_idx) {
                        ESP_LOGI(TAG, "Found target index %ld: %s", (long) target_idx, fullpath);
                        display_manager_show_image(fullpath);
                        save_last_displayed_image(fullpath);
                        config_manager_set_last_index(target_idx);
                        found_target = true;
                        closedir(dir);
                        return;
                    }
                    current_idx++;
                }
            }
        }
        closedir(dir);
    }

    ESP_LOGI(
        TAG,
        "Sequential rotation finished traversal. current_idx=%ld, target_idx=%ld, found_target=%d",
        (long) current_idx, (long) target_idx, found_target);

    // If we reached here, we didn't find the target index (or the list has changed and is
    // shorter) Wrap around to the first image
    if (!found_target) {
        if (first_image[0] != '\0') {
            ESP_LOGI(TAG, "Wrapping around to start. Displaying: %s", first_image);
            display_manager_show_image(first_image);
            save_last_displayed_image(first_image);
            config_manager_set_last_index(0);  // Reset index to 0
        } else {
            ESP_LOGW(TAG, "No images found in any enabled albums.");
        }
    }
}

static void rotate_random(char **enabled_albums, int album_count)
{
    ESP_LOGI(TAG, "Random rotation mode");

    // Count total images across all enabled albums
    int total_image_count = 0;
    for (int i = 0; i < album_count; i++) {
        char album_path[256];
        album_manager_get_album_path(enabled_albums[i], album_path, sizeof(album_path));

        DIR *dir = opendir(album_path);
        if (!dir) {
            ESP_LOGW(TAG, "Failed to open album: %s", enabled_albums[i]);
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                if (entry->d_name[0] == '.' && entry->d_name[1] == '_') {
                    continue;
                }
                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".png") == 0 ||
                            strcasecmp(ext, ".epdgz") == 0)) {
                    total_image_count++;
                }
            }
        }
        closedir(dir);
    }

    if (total_image_count == 0) {
        ESP_LOGW(TAG, "No images found in enabled albums");
        return;
    }

    // Build image list with absolute paths from all enabled albums
    char **image_list = malloc(total_image_count * sizeof(char *));
    if (!image_list) {
        ESP_LOGE(TAG, "Failed to allocate image list");
        return;
    }
    int idx = 0;

    for (int i = 0; i < album_count; i++) {
        char album_path[256];
        album_manager_get_album_path(enabled_albums[i], album_path, sizeof(album_path));

        DIR *dir = opendir(album_path);
        if (!dir) {
            continue;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && idx < total_image_count) {
            if (entry->d_type == DT_REG) {
                if (entry->d_name[0] == '.' && entry->d_name[1] == '_') {
                    continue;
                }

                const char *ext = strrchr(entry->d_name, '.');
                if (ext && (strcasecmp(ext, ".bmp") == 0 || strcasecmp(ext, ".png") == 0 ||
                            strcasecmp(ext, ".epdgz") == 0)) {
                    char *fullpath = malloc(512);
                    if (!fullpath) {
                        ESP_LOGE(TAG, "Failed to allocate path buffer");
                        continue;
                    }
                    snprintf(fullpath, 512, "%s/%s", album_path, entry->d_name);
                    image_list[idx] = fullpath;
                    idx++;
                }
            }
        }
        closedir(dir);
    }

    // Update total_image_count to actual number of images found
    total_image_count = idx;

    if (total_image_count == 0) {
        ESP_LOGW(TAG, "No displayable images found in enabled albums");
        free(image_list);
        return;
    }

    // Load last displayed image if not already loaded
    if (last_displayed_image[0] == '\0') {
        load_last_displayed_image();
    }

    // Select random image, avoiding the last displayed image if possible
    int random_index = esp_random() % total_image_count;

    // If we have more than one image and the random selection matches the last image,
    // try to pick a different one (up to 10 attempts)
    if (total_image_count > 1 && last_displayed_image[0] != '\0') {
        int attempts = 0;
        while (attempts < 10 && strcmp(image_list[random_index], last_displayed_image) == 0) {
            random_index = esp_random() % total_image_count;
            attempts++;
        }

        if (strcmp(image_list[random_index], last_displayed_image) == 0) {
            ESP_LOGW(TAG, "Could not avoid repeating last image after 10 attempts");
        } else {
            ESP_LOGI(TAG, "Successfully avoided repeating last image");
        }
    }

    // Display random image
    ESP_LOGI(TAG, "Auto-rotate: Displaying random image %d/%d: %s", random_index + 1,
             total_image_count, image_list[random_index]);
    display_manager_show_image(image_list[random_index]);

    // Store the displayed image filename in NVS
    save_last_displayed_image(image_list[random_index]);

    // Free image list
    for (int i = 0; i < total_image_count; i++) {
        free(image_list[i]);
    }
    free(image_list);
}

void display_manager_rotate_from_storage(void)
{
    if (!config_manager_get_auto_rotate()) {
        ESP_LOGI(TAG, "Manual rotation triggered (auto-rotate is disabled)");
    } else {
        ESP_LOGI(TAG, "Rotating from storage");
    }

    if (!storage_has_persistent_storage()) {
        ESP_LOGI(TAG, "Storage not mounted - skipping rotation");
        return;
    }

    // Get enabled albums
    char **enabled_albums = NULL;
    int album_count = 0;
    if (album_manager_get_enabled_albums(&enabled_albums, &album_count) != ESP_OK ||
        album_count == 0) {
        ESP_LOGW(TAG, "No enabled albums for auto-rotate");
        return;
    }

    ESP_LOGD(TAG, "Collecting images from %d enabled album(s)", album_count);
    for (int i = 0; i < album_count; i++) {
        ESP_LOGD(TAG, "  Enabled album[%d]: %s", i, enabled_albums[i]);
    }

    // Check for stale albums (removed from SD card) and disable them
    bool found_stale_albums = false;
    for (int i = 0; i < album_count; i++) {
        if (!album_manager_album_exists(enabled_albums[i])) {
            ESP_LOGW(TAG, "Album '%s' no longer exists on SD card, disabling it",
                     enabled_albums[i]);
            album_manager_set_album_enabled(enabled_albums[i], false);
            found_stale_albums = true;
        }
    }

    // If we found stale albums, reload the enabled list
    if (found_stale_albums) {
        album_manager_free_album_list(enabled_albums, album_count);
        if (album_manager_get_enabled_albums(&enabled_albums, &album_count) != ESP_OK ||
            album_count == 0) {
            ESP_LOGW(TAG, "No enabled albums remaining after cleanup");
            return;
        }
        ESP_LOGI(TAG, "After cleanup: %d enabled album(s)", album_count);
    }

    // Get rotation mode
    sd_rotation_mode_t mode = config_manager_get_sd_rotation_mode();

    if (mode == SD_ROTATION_SEQUENTIAL) {
        rotate_sequential(enabled_albums, album_count);
    } else {
        rotate_random(enabled_albums, album_count);
    }

    album_manager_free_album_list(enabled_albums, album_count);
    ESP_LOGI(TAG, "Rotation complete");
}