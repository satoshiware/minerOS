#ifndef THEME_API_H
#define THEME_API_H

#include "esp_http_server.h"

#define DEFAULT_THEME "dark"
#define DEFAULT_COLORS "{ "\
        "\"--primary-color\":\"#E0A000\", "\
        "\"--primary-color-text\":\"#ffffff\", "\
        "\"--highlight-bg\":\"#E0A000\", "\
        "\"--highlight-text-color\":\"#ffffff\", "\
        "\"--focus-ring\":\"0 0 0 0.2rem rgba(224,160,0,0.2)\", "\
        "\"--slider-bg\":\"#dee2e6\", "\
        "\"--slider-range-bg\":\"#E0A000\", "\
        "\"--slider-handle-bg\":\"#E0A000\", "\
        "\"--progressbar-bg\":\"#dee2e6\", "\
        "\"--progressbar-value-bg\":\"#E0A000\", "\
        "\"--checkbox-border\":\"#E0A000\", "\
        "\"--checkbox-bg\":\"#E0A000\", "\
        "\"--checkbox-hover-bg\":\"#C58D00\", "\
        "\"--button-bg\":\"#E0A000\", "\
        "\"--button-hover-bg\":\"#C58D00\", "\
        "\"--button-focus-shadow\":\"0 0 0 2px #ffffff, 0 0 0 4px #E0A000\", "\
        "\"--togglebutton-bg\":\"#E0A000\", "\
        "\"--togglebutton-border\":\"1px solid #E0A000\", "\
        "\"--togglebutton-hover-bg\":\"#C58D00\", "\
        "\"--togglebutton-hover-border\":\"1px solid #C58D00\", "\
        "\"--togglebutton-text-color\":\"#ffffff\" "\
        "}"

// Register theme API endpoints
esp_err_t register_theme_api_endpoints(httpd_handle_t server, void* ctx);

#endif // THEME_API_H
