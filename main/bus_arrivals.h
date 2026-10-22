#ifndef BUS_ARRIVALS_H
#define BUS_ARRIVALS_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bus arrivals are turned on, with an LTA AccountKey and at least one bus stop configured
bool bus_arrivals_is_enabled(void);

// Bus arrivals replace photos right now: enabled, inside (or about to enter) the bus time
// window, outside the sleep schedule, and the clock is set
bool bus_arrivals_is_active(void);

// Fetch live arrivals for the configured stops from LTA DataMall and show them on the display.
// A stop that can't be fetched shows a short error in its panel. Returns an error without
// touching the display when the clock isn't set.
esp_err_t bus_arrivals_show(void);

#ifdef __cplusplus
}
#endif

#endif
