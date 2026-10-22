/**
 * @file http_server.h
 * @brief REST API server for configuration and control.
 * @ingroup webapi
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"

esp_err_t http_server_init(void);
esp_err_t http_server_stop(void);
void http_server_set_ready(void);

#endif
