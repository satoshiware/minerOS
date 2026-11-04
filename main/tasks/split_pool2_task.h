#pragma once

#include "global_state.h"
#include "stratum_api.h"

#include <stdbool.h>
#include <stdint.h>

void split_pool2_task(void *pvParameters);

/* True when Split is on and Pool 2 has a template, extranonce, and difficulty. */
bool split_pool2_has_job(void);

/*
 * Copy the latest Pool 2 template. The caller frees it with
 * STRATUM_V1_free_mining_notify. Returns false when Pool 2 has no job.
 */
bool split_pool2_copy_job(mining_notify **notify, char *extranonce, size_t extranonce_len, int *extranonce2_len, double *difficulty);

/* Submit one share on the Pool 2 session. Logs a rejected share if the write fails. */
bool split_pool2_submit_share(GlobalState *gs, const char *job_id, const char *extranonce2,
                              uint32_t ntime, uint32_t nonce, uint32_t version_bits, uint8_t chip);
