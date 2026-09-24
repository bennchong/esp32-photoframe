#ifndef TESTABLE_UTILS_H
#define TESTABLE_UTILS_H

#include <stdbool.h>
#include <time.h>

#define BUS_SERVICE_NO_LEN 8

typedef struct {
    bool enabled;
    int start_minutes;  // Minutes since midnight
    int end_minutes;    // Minutes since midnight
} sleep_schedule_config_t;

#ifdef __cplusplus
extern "C" {
#endif

// Calculate next wake-up interval considering sleep schedule
// Returns seconds until next wake-up
// Takes into account:
// - Current time (via timeinfo)
// - Clock alignment (if aligned=true, aligns to rotation interval boundaries)
// - Sleep schedule (skips wake-ups that fall within sleep schedule)
// - Overnight schedules (handles schedules that cross midnight)
int calculate_next_wakeup_interval(const struct tm *timeinfo, int rotate_interval, bool aligned,
                                   const sleep_schedule_config_t *sleep_schedule);

// Split a comma-separated bus service list such as "12, 36,851" into at most max_services
// entries, trimming spaces and skipping empty or over-long entries. Returns the entry count.
int parse_bus_service_list(const char *csv, char services[][BUS_SERVICE_NO_LEN], int max_services);

// Whole minutes from now until an LTA EstimatedArrival timestamp such as
// "2026-09-24T07:45:12+08:00", rounded down and clamped at 0 for buses that are due.
// Returns -1 when the timestamp is empty or malformed.
int lta_minutes_until(const char *estimated_arrival, time_t now);

// Whether minute_of_day is in [start_minutes, end_minutes), wrapping past midnight when
// start_minutes > end_minutes. An empty window (start == end) contains nothing.
bool is_minute_in_window(int minute_of_day, int start_minutes, int end_minutes);

// Whether the bus arrivals window is open at timeinfo or opens within lead_seconds, so a wake-up
// that RTC drift brings forward slightly still counts
bool is_bus_window_active(const struct tm *timeinfo, int start_minutes, int end_minutes,
                          int lead_seconds);

// Seconds until the next wake-up when bus arrivals are shown in [start_minutes, end_minutes):
// refresh_seconds while the window is active, otherwise the time until it opens.
// next_rotation is the photo rotation wake-up in seconds, or <= 0 when photo rotation is off;
// the earlier of the two wins.
int calculate_bus_wakeup_interval(const struct tm *timeinfo, int next_rotation, int start_minutes,
                                  int end_minutes, int refresh_seconds, int lead_seconds);

#ifdef __cplusplus
}
#endif

#endif
