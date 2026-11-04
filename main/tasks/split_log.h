#pragma once

#include "esp_log.h"
#include "esp_timer.h"

#include <stdbool.h>
#include <stdint.h>

/* Pool 1 is the primary form. Pool 2 is the fallback form. */
static inline uint8_t split_pool_slot(bool using_fallback)
{
    return using_fallback ? 2 : 1;
}

static inline void split_log_job(const char *tag, uint8_t pool_slot, const char *job_id)
{
    ESP_LOGI(tag, "split job time=%lld pool=%u job=%s",
             (long long)esp_timer_get_time(), (unsigned)pool_slot, job_id ? job_id : "");
}

static inline void split_log_share(const char *tag, uint8_t chip, uint8_t pool_slot, const char *job_id, bool accepted)
{
    ESP_LOGI(tag, "split share time=%lld chip=%u pool=%u job=%s result=%s",
             (long long)esp_timer_get_time(), (unsigned)chip, (unsigned)pool_slot,
             job_id ? job_id : "", accepted ? "accepted" : "rejected");
}
