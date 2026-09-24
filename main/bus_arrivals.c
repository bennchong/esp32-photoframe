#include "bus_arrivals.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"
#include "config.h"
#include "config_manager.h"
#include "display_manager.h"
#include "esp_log.h"
#include "testable_utils.h"
#include "utils.h"

static const char *TAG = "bus_arrivals";

typedef struct {
    const char *(*number)(void);
    const char *(*services)(void);
} bus_stop_config_t;

static const bus_stop_config_t configured_stops[] = {
    {config_manager_get_bus_stop_number, config_manager_get_bus_services},
    {config_manager_get_bus_stop_number_2, config_manager_get_bus_services_2},
};

#define BUS_STOP_COUNT (sizeof(configured_stops) / sizeof(configured_stops[0]))
_Static_assert(BUS_STOP_COUNT <= DISPLAY_MANAGER_BUS_MAX_STOPS, "screen can't fit every stop");

// LTA's estimates for a service, soonest first
static const char *const next_bus_keys[] = {"NextBus", "NextBus2", "NextBus3"};
_Static_assert(DISPLAY_MANAGER_BUS_MAX_ARRIVALS <= 3, "LTA gives at most three arrivals");

typedef struct {
    char service_no[BUS_SERVICE_NO_LEN];
    char arrivals[DISPLAY_MANAGER_BUS_MAX_ARRIVALS][12];
} bus_row_text_t;

static bool clock_is_set(const struct tm *timeinfo)
{
    return timeinfo->tm_year >= (2025 - 1900);
}

bool bus_arrivals_is_enabled(void)
{
    if (!config_manager_get_bus_enabled() || config_manager_get_lta_account_key()[0] == '\0') {
        return false;
    }

    for (size_t i = 0; i < BUS_STOP_COUNT; i++) {
        if (configured_stops[i].number()[0] != '\0') {
            return true;
        }
    }
    return false;
}

bool bus_arrivals_is_active(void)
{
    if (!bus_arrivals_is_enabled() || config_manager_is_in_sleep_schedule()) {
        return false;
    }

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    return clock_is_set(&timeinfo) &&
           is_bus_window_active(&timeinfo, config_manager_get_bus_time_start(),
                                config_manager_get_bus_time_end(), BUS_WINDOW_LEAD_SEC);
}

// "ARR", whole minutes, or "" when LTA has no estimate
static void format_arrival(char *out, size_t out_len, const cJSON *service, const char *key,
                           time_t now)
{
    const cJSON *bus = cJSON_GetObjectItemCaseSensitive(service, key);
    const cJSON *estimate = cJSON_GetObjectItemCaseSensitive(bus, "EstimatedArrival");
    int minutes = lta_minutes_until(cJSON_GetStringValue(estimate), now);

    if (minutes == 0) {
        snprintf(out, out_len, "ARR");
    } else if (minutes > 0) {
        snprintf(out, out_len, "%d", minutes);
    } else {
        out[0] = '\0';
    }
}

static const cJSON *find_service(const cJSON *services, const char *service_no)
{
    const cJSON *service;
    cJSON_ArrayForEach(service, services)
    {
        const char *number =
            cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(service, "ServiceNo"));
        if (number && strcasecmp(number, service_no) == 0) {
            return service;
        }
    }
    return NULL;
}

// Fill in one stop: the configured services in the order given (a service with no buses right
// now keeps its row), or the first services LTA lists when none are configured
static void load_stop(const bus_stop_config_t *config, time_t now, display_manager_bus_stop_t *stop,
                      display_manager_bus_service_t *services, bus_row_text_t *rows)
{
    const char *number = config->number();
    char requested[DISPLAY_MANAGER_BUS_MAX_SERVICES][BUS_SERVICE_NO_LEN];
    int requested_count =
        parse_bus_service_list(config->services(), requested, DISPLAY_MANAGER_BUS_MAX_SERVICES);

    *stop = (display_manager_bus_stop_t){.stop_id = number, .services = services};

    cJSON *response = NULL;
    int http_status = 0;
    char err[128];
    if (fetch_lta_bus_arrivals(number, NULL, &response, &http_status, err, sizeof(err)) != ESP_OK) {
        ESP_LOGW(TAG, "Bus stop %s: %s", number, err);
        stop->message =
            (http_status == 401 || http_status == 403) ? "Check LTA AccountKey" : "Can't reach LTA";
        return;
    }

    const cJSON *lta_services = cJSON_GetObjectItemCaseSensitive(response, "Services");
    int count = requested_count > 0 ? requested_count : cJSON_GetArraySize(lta_services);
    if (count > DISPLAY_MANAGER_BUS_MAX_SERVICES) {
        count = DISPLAY_MANAGER_BUS_MAX_SERVICES;
    }

    for (int i = 0; i < count; i++) {
        const cJSON *service = requested_count > 0 ? find_service(lta_services, requested[i])
                                                   : cJSON_GetArrayItem(lta_services, i);
        const char *service_no =
            requested_count > 0
                ? requested[i]
                : cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(service, "ServiceNo"));

        strncpy(rows[i].service_no, service_no ? service_no : "?", sizeof(rows[i].service_no) - 1);
        rows[i].service_no[sizeof(rows[i].service_no) - 1] = '\0';
        services[i].service_no = rows[i].service_no;
        for (int j = 0; j < DISPLAY_MANAGER_BUS_MAX_ARRIVALS; j++) {
            format_arrival(rows[i].arrivals[j], sizeof(rows[i].arrivals[j]), service,
                           next_bus_keys[j], now);
            services[i].arrivals[j] = rows[i].arrivals[j];
        }
    }

    stop->service_count = count;
    if (count == 0) {
        stop->message = "No buses right now";
    }
    cJSON_Delete(response);
}

esp_err_t bus_arrivals_show(void)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    if (!clock_is_set(&timeinfo)) {
        ESP_LOGW(TAG, "Clock not set, can't work out arrival times");
        return ESP_ERR_INVALID_STATE;
    }

    display_manager_bus_stop_t stops[BUS_STOP_COUNT];
    display_manager_bus_service_t services[BUS_STOP_COUNT][DISPLAY_MANAGER_BUS_MAX_SERVICES];
    bus_row_text_t rows[BUS_STOP_COUNT][DISPLAY_MANAGER_BUS_MAX_SERVICES];
    size_t stop_count = 0;

    for (size_t i = 0; i < BUS_STOP_COUNT; i++) {
        if (configured_stops[i].number()[0] == '\0') {
            continue;
        }
        load_stop(&configured_stops[i], now, &stops[stop_count], services[stop_count],
                  rows[stop_count]);
        stop_count++;
    }

    char updated[8];
    strftime(updated, sizeof(updated), "%H:%M", &timeinfo);

    display_manager_bus_screen_t screen = {
        .last_updated = updated,
        .stops = stops,
        .stop_count = stop_count,
    };

    ESP_LOGI(TAG, "Showing bus arrivals for %zu stop(s)", stop_count);
    return display_manager_show_bus_timing_screen(&screen);
}
