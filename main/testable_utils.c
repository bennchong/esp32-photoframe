#include "testable_utils.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int calculate_next_wakeup_interval(const struct tm *timeinfo, int rotate_interval, bool aligned,
                                   const sleep_schedule_config_t *sleep_schedule)
{
    int current_seconds_of_day =
        timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
    int seconds_until_next;

    if (aligned) {
        int next_aligned_seconds =
            ((current_seconds_of_day / rotate_interval) + 1) * rotate_interval;
        seconds_until_next = next_aligned_seconds - current_seconds_of_day;

        // If next wakeup is too soon (less than 60s), skip to the following interval.
        // This prevents immediate re-wakeup due to time drift.
        if (seconds_until_next < 60) {
            next_aligned_seconds += rotate_interval;
            seconds_until_next = next_aligned_seconds - current_seconds_of_day;
        }
    } else {
        seconds_until_next = rotate_interval;
    }

    // Check if sleep schedule is enabled
    if (sleep_schedule == NULL || !sleep_schedule->enabled) {
        return seconds_until_next;
    }

    // Calculate the wake-up time in seconds since midnight
    int wake_seconds_of_day = current_seconds_of_day + seconds_until_next;

    // Normalize wake_seconds_of_day to handle day overflow
    while (wake_seconds_of_day >= 86400) {
        wake_seconds_of_day -= 86400;
    }

    int sleep_start_seconds = sleep_schedule->start_minutes * 60;
    int sleep_end_seconds = sleep_schedule->end_minutes * 60;

    // Check if wake-up time falls within or at the start of sleep schedule
    bool wake_in_schedule = false;
    if (sleep_start_seconds > sleep_end_seconds) {
        // Schedule crosses midnight (e.g., 23:00 - 07:00)
        // Wake is in schedule if >= start OR < end
        wake_in_schedule =
            (wake_seconds_of_day >= sleep_start_seconds || wake_seconds_of_day < sleep_end_seconds);
    } else {
        // Schedule within same day (e.g., 12:00 - 14:00)
        // Wake is in schedule if >= start AND < end
        wake_in_schedule =
            (wake_seconds_of_day >= sleep_start_seconds && wake_seconds_of_day < sleep_end_seconds);
    }

    if (!wake_in_schedule) {
        // Wake-up is outside sleep schedule, use normal interval
        return seconds_until_next;
    }

    // Wake-up would be in sleep schedule, calculate next wake-up time at or after schedule ends.
    long long next_wake_seconds_of_day;
    if (aligned) {
        // Find the first aligned time >= sleep_end (sleep_end is exclusive).
        next_wake_seconds_of_day = ((long long) sleep_end_seconds + rotate_interval - 1) /
                                   rotate_interval * rotate_interval;
    } else {
        // For non-aligned rotation, just wake up exactly when the sleep schedule ends
        next_wake_seconds_of_day = sleep_end_seconds;
    }

    // Calculate seconds from current time to next wake-up
    int seconds_until_wake;
    if (sleep_start_seconds > sleep_end_seconds) {
        // Overnight schedule (e.g., 23:00 - 07:00)
        if (current_seconds_of_day >= sleep_start_seconds ||
            current_seconds_of_day < sleep_end_seconds) {
            // Currently in the schedule
            if (current_seconds_of_day >= sleep_start_seconds) {
                // Before midnight - wake after schedule ends next day
                seconds_until_wake =
                    (86400 - current_seconds_of_day) + (int) next_wake_seconds_of_day;
            } else {
                // After midnight - wake at next aligned time today
                seconds_until_wake = (int) next_wake_seconds_of_day - current_seconds_of_day;
            }
        } else {
            // Currently between schedule end and start
            // But wake time is in schedule, so skip to next day
            seconds_until_wake = (86400 - current_seconds_of_day) + (int) next_wake_seconds_of_day;
        }
    } else {
        // Same-day schedule
        seconds_until_wake = (int) next_wake_seconds_of_day - current_seconds_of_day;
        if (seconds_until_wake < 0) {
            seconds_until_wake += 86400;  // Wrap to next day
        }
    }

    return seconds_until_wake;
}

int parse_bus_service_list(const char *csv, char services[][BUS_SERVICE_NO_LEN], int max_services)
{
    int count = 0;
    const char *cursor = csv;

    while (cursor && *cursor && count < max_services) {
        const char *end = strchr(cursor, ',');
        if (!end) {
            end = cursor + strlen(cursor);
        }

        const char *first = cursor;
        const char *last = end;
        while (first < last && isspace((unsigned char) *first)) {
            first++;
        }
        while (last > first && isspace((unsigned char) last[-1])) {
            last--;
        }

        size_t len = (size_t) (last - first);
        if (len > 0 && len < BUS_SERVICE_NO_LEN) {
            memcpy(services[count], first, len);
            services[count][len] = '\0';
            count++;
        }

        cursor = *end ? end + 1 : end;
    }

    return count;
}

// Days from 1970-01-01 to a proleptic Gregorian date (Howard Hinnant's days_from_civil)
static long long days_from_civil(int year, int month, int day)
{
    year -= month <= 2;
    long long era = (year >= 0 ? year : year - 399) / 400;
    int year_of_era = year - (int) (era * 400);
    int day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + day_of_era - 719468;
}

int lta_minutes_until(const char *estimated_arrival, time_t now)
{
    int year, month, day, hour, minute, second;
    char zone = 'Z';
    int offset_hours = 0;
    int offset_minutes = 0;

    if (!estimated_arrival ||
        sscanf(estimated_arrival, "%4d-%2d-%2dT%2d:%2d:%2d%c%2d:%2d", &year, &month, &day, &hour,
               &minute, &second, &zone, &offset_hours, &offset_minutes) < 6) {
        return -1;
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return -1;
    }

    long long offset = 0;
    if (zone == '+' || zone == '-') {
        offset = (zone == '-' ? -1 : 1) * (offset_hours * 3600LL + offset_minutes * 60LL);
    }

    long long arrival = days_from_civil(year, month, day) * 86400LL + hour * 3600LL +
                        minute * 60LL + second - offset;
    long long seconds = arrival - (long long) now;
    return seconds <= 0 ? 0 : (int) (seconds / 60);
}

bool is_minute_in_window(int minute_of_day, int start_minutes, int end_minutes)
{
    if (start_minutes > end_minutes) {
        // Window crosses midnight (e.g., 23:00 - 01:00)
        return minute_of_day >= start_minutes || minute_of_day < end_minutes;
    }
    return minute_of_day >= start_minutes && minute_of_day < end_minutes;
}

bool is_bus_window_active(const struct tm *timeinfo, int start_minutes, int end_minutes,
                          int lead_seconds)
{
    int now = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
    int soon = (now + lead_seconds) % 86400;
    return is_minute_in_window(now / 60, start_minutes, end_minutes) ||
           is_minute_in_window(soon / 60, start_minutes, end_minutes);
}

int calculate_bus_wakeup_interval(const struct tm *timeinfo, int next_rotation, int start_minutes,
                                  int end_minutes, int refresh_seconds, int lead_seconds)
{
    int wake;

    if (start_minutes == end_minutes) {
        // Empty window: buses are never shown
        wake = 86400;
    } else if (is_bus_window_active(timeinfo, start_minutes, end_minutes, lead_seconds)) {
        wake = refresh_seconds;
    } else {
        int now = timeinfo->tm_hour * 3600 + timeinfo->tm_min * 60 + timeinfo->tm_sec;
        wake = start_minutes * 60 - now;
        if (wake <= 0) {
            wake += 86400;
        }
    }

    return (next_rotation > 0 && next_rotation < wake) ? next_rotation : wake;
}
