#include "split_pool2_task.h"

#include "split_log.h"
#include "nvs_config.h"
#include "system.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "split_pool2";
static char s_acc[8192];

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static esp_transport_handle_t s_transport = NULL;
static mining_notify *s_notify = NULL;
static char s_extranonce[128];
static int s_extranonce2_len = 0;
static bool s_have_extranonce = false;
static double s_difficulty = 0;
static char s_user[128];
static int s_uid = 1;
static bool s_connected = false;

#define PENDING_SHARES 16

typedef struct {
    int id;
    uint8_t chip;
    char job_id[32];
    bool used;
} pending_share_t;

static pending_share_t s_pending[PENDING_SHARES];

typedef struct {
    bool used;
    uint8_t chip;
    char job_id[32];
    char extranonce2[18];
    uint32_t ntime;
    uint32_t nonce;
    uint32_t version_bits;
} queued_share_t;

static queued_share_t s_queue[PENDING_SHARES];

static void clear_pending(void)
{
    memset(s_pending, 0, sizeof(s_pending));
    memset(s_queue, 0, sizeof(s_queue));
}

static void fail_outstanding_locked(void)
{
    for (int i = 0; i < PENDING_SHARES; i++) {
        if (s_pending[i].used) {
            split_log_share(TAG, s_pending[i].chip, 2, s_pending[i].job_id, false);
            s_pending[i].used = false;
        }
        if (s_queue[i].used) {
            split_log_share(TAG, s_queue[i].chip, 2, s_queue[i].job_id, false);
            s_queue[i].used = false;
        }
    }
}

static void remember_share(int id, uint8_t chip, const char *job_id)
{
    pending_share_t *slot = &s_pending[0];
    for (int i = 0; i < PENDING_SHARES; i++) {
        if (!s_pending[i].used) {
            slot = &s_pending[i];
            break;
        }
    }
    slot->id = id;
    slot->chip = chip;
    slot->used = true;
    snprintf(slot->job_id, sizeof(slot->job_id), "%s", job_id ? job_id : "");
}

static bool take_pending(int id, uint8_t *chip, char *job_id, size_t job_id_len)
{
    for (int i = 0; i < PENDING_SHARES; i++) {
        if (s_pending[i].used && s_pending[i].id == id) {
            if (chip) {
                *chip = s_pending[i].chip;
            }
            if (job_id && job_id_len > 0) {
                snprintf(job_id, job_id_len, "%s", s_pending[i].job_id);
            }
            s_pending[i].used = false;
            return true;
        }
    }
    return false;
}

static void drop_session_locked(GlobalState *gs)
{
    fail_outstanding_locked();
    s_connected = false;
    s_difficulty = 0;
    s_extranonce[0] = '\0';
    s_extranonce2_len = 0;
    s_have_extranonce = false;
    s_user[0] = '\0';
    if (s_notify) {
        STRATUM_V1_free_mining_notify(s_notify);
        s_notify = NULL;
    }
    clear_pending();
    gs->SYSTEM_MODULE.split_pool2_connected = false;
    gs->SYSTEM_MODULE.split_pool2_job_id[0] = '\0';
}

/* Split on, Pool 1 still the primary session, Pool 2 filled with Stratum V1. */
static bool pool2_listen_enabled(GlobalState *gs)
{
    if (!nvs_config_get_bool(NVS_CONFIG_SPLIT_MODE)) {
        return false;
    }
    if (gs->SYSTEM_MODULE.is_using_fallback) {
        return false;
    }

    char *proto = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PROTOCOL);
    bool v1 = proto != NULL && strcmp(proto, STRATUM_V1) == 0;
    free(proto);
    if (!v1) {
        return false;
    }

    char *url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);
    char *user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);
    uint16_t port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT);
    bool ok = url && url[0] && user && user[0] && port != 0;
    free(url);
    free(user);
    return ok;
}

bool split_pool2_has_job(void)
{
    pthread_mutex_lock(&s_lock);
    bool ready = s_connected && s_notify != NULL && s_have_extranonce && s_difficulty > 0;
    pthread_mutex_unlock(&s_lock);
    return ready;
}

bool split_pool2_copy_job(mining_notify **notify, char *extranonce, size_t extranonce_len, int *extranonce2_len, double *difficulty)
{
    pthread_mutex_lock(&s_lock);
    if (!s_connected || s_notify == NULL || !s_have_extranonce || s_difficulty <= 0 ||
        s_notify->job_id == NULL || s_notify->coinbase_1 == NULL || s_notify->coinbase_2 == NULL ||
        s_notify->prev_block_hash == NULL) {
        pthread_mutex_unlock(&s_lock);
        return false;
    }

    mining_notify *copy = calloc(1, sizeof(*copy));
    if (copy == NULL) {
        pthread_mutex_unlock(&s_lock);
        return false;
    }
    copy->job_id = strdup(s_notify->job_id);
    copy->prev_block_hash = strdup(s_notify->prev_block_hash);
    copy->coinbase_1 = strdup(s_notify->coinbase_1);
    copy->coinbase_2 = strdup(s_notify->coinbase_2);
    copy->n_merkle_branches = s_notify->n_merkle_branches;
    if (copy->n_merkle_branches > 0 && s_notify->merkle_branches != NULL) {
        copy->merkle_branches = malloc(HASH_SIZE * copy->n_merkle_branches);
        if (copy->merkle_branches != NULL) {
            memcpy(copy->merkle_branches, s_notify->merkle_branches, HASH_SIZE * copy->n_merkle_branches);
        }
    }
    copy->version = s_notify->version;
    copy->target = s_notify->target;
    copy->ntime = s_notify->ntime;
    copy->clean_jobs = s_notify->clean_jobs;
    snprintf(extranonce, extranonce_len, "%s", s_extranonce);
    *extranonce2_len = s_extranonce2_len;
    *difficulty = s_difficulty;
    pthread_mutex_unlock(&s_lock);

    if (copy->job_id == NULL || copy->prev_block_hash == NULL || copy->coinbase_1 == NULL || copy->coinbase_2 == NULL ||
        (copy->n_merkle_branches > 0 && copy->merkle_branches == NULL)) {
        STRATUM_V1_free_mining_notify(copy);
        return false;
    }
    *notify = copy;
    return true;
}

bool split_pool2_submit_share(GlobalState *gs, const char *job_id, const char *extranonce2,
                              uint32_t ntime, uint32_t nonce, uint32_t version_bits, uint8_t chip)
{
    (void)gs;
    pthread_mutex_lock(&s_lock);
    if (s_transport == NULL || s_user[0] == '\0') {
        pthread_mutex_unlock(&s_lock);
        split_log_share(TAG, chip, 2, job_id, false);
        return false;
    }
    queued_share_t *slot = NULL;
    for (int i = 0; i < PENDING_SHARES; i++) {
        if (!s_queue[i].used) {
            slot = &s_queue[i];
            break;
        }
    }
    if (slot == NULL) {
        pthread_mutex_unlock(&s_lock);
        split_log_share(TAG, chip, 2, job_id, false);
        return false;
    }
    slot->used = true;
    slot->chip = chip;
    slot->ntime = ntime;
    slot->nonce = nonce;
    slot->version_bits = version_bits;
    snprintf(slot->job_id, sizeof(slot->job_id), "%s", job_id ? job_id : "");
    snprintf(slot->extranonce2, sizeof(slot->extranonce2), "%s", extranonce2 ? extranonce2 : "");
    pthread_mutex_unlock(&s_lock);
    return true;
}

/* Called on the Pool 2 task so a share write cannot race the socket read. */
static void drain_shares(esp_transport_handle_t transport)
{
    queued_share_t batch[PENDING_SHARES];
    int ids[PENDING_SHARES];
    int count = 0;
    char user[128];

    pthread_mutex_lock(&s_lock);
    snprintf(user, sizeof(user), "%s", s_user);
    for (int i = 0; i < PENDING_SHARES; i++) {
        if (!s_queue[i].used) {
            continue;
        }
        batch[count] = s_queue[i];
        s_queue[i].used = false;
        ids[count] = s_uid++;
        remember_share(ids[count], batch[count].chip, batch[count].job_id);
        count++;
    }
    pthread_mutex_unlock(&s_lock);

    for (int i = 0; i < count; i++) {
        char submit_msg[1024];
        snprintf(submit_msg, sizeof(submit_msg),
                 "{\"id\":%d,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%08lx\",\"%08lx\",\"%08lx\"]}\n",
                 ids[i], user, batch[i].job_id, batch[i].extranonce2,
                 (unsigned long)batch[i].ntime, (unsigned long)batch[i].nonce, (unsigned long)batch[i].version_bits);
        ESP_LOGI(TAG, "tx pool=2 user=%s job=%s", user, batch[i].job_id);
        int ret = esp_transport_write(transport, submit_msg, strlen(submit_msg), 5000);
        if (ret < 0) {
            uint8_t chip = batch[i].chip;
            char job_id[32];
            pthread_mutex_lock(&s_lock);
            take_pending(ids[i], &chip, job_id, sizeof(job_id));
            pthread_mutex_unlock(&s_lock);
            split_log_share(TAG, chip, 2, job_id[0] ? job_id : batch[i].job_id, false);
        }
    }
}

static void handle_line(GlobalState *gs, esp_transport_handle_t transport, const char *line)
{
    if (line[0] == '\0') {
        return;
    }

    StratumApiV1Message msg;
    memset(&msg, 0, sizeof(msg));
    if (!STRATUM_V1_parse(&msg, line)) {
        STRATUM_V1_reset_message(&msg);
        return;
    }

    if (msg.method == MINING_NOTIFY && msg.mining_notification && msg.mining_notification->job_id) {
        pthread_mutex_lock(&s_lock);
        if (s_notify) {
            STRATUM_V1_free_mining_notify(s_notify);
        }
        s_notify = msg.mining_notification;
        msg.mining_notification = NULL;
        snprintf(gs->SYSTEM_MODULE.split_pool2_job_id, sizeof(gs->SYSTEM_MODULE.split_pool2_job_id),
                 "%s", s_notify->job_id);
        s_connected = true;
        gs->SYSTEM_MODULE.split_pool2_connected = true;
        pthread_mutex_unlock(&s_lock);
        split_log_pooljob(TAG, 2, gs->SYSTEM_MODULE.split_pool2_job_id);
    } else if (msg.method == MINING_SET_DIFFICULTY) {
        pthread_mutex_lock(&s_lock);
        s_difficulty = msg.new_difficulty;
        pthread_mutex_unlock(&s_lock);
        ESP_LOGI(TAG, "Pool 2 difficulty %.2f", msg.new_difficulty);
    } else if (msg.method == MINING_SET_EXTRANONCE || msg.method == STRATUM_RESULT_SUBSCRIBE) {
        pthread_mutex_lock(&s_lock);
        if (msg.extranonce_str) {
            snprintf(s_extranonce, sizeof(s_extranonce), "%s", msg.extranonce_str);
        } else {
            s_extranonce[0] = '\0';
        }
        s_extranonce2_len = msg.extranonce_2_len;
        s_have_extranonce = true;
        pthread_mutex_unlock(&s_lock);
    } else if (msg.method == MINING_PING) {
        STRATUM_V1_pong(transport, msg.message_id);
    } else if (msg.method == STRATUM_RESULT) {
        uint8_t chip = 0;
        char job_id[32] = "";
        pthread_mutex_lock(&s_lock);
        bool share = take_pending(msg.message_id, &chip, job_id, sizeof(job_id));
        pthread_mutex_unlock(&s_lock);
        if (share) {
            if (msg.response_success) {
                SYSTEM_notify_accepted_share(gs);
            } else {
                SYSTEM_notify_rejected_share(gs, msg.error_str);
            }
            split_log_share(TAG, chip, 2, job_id, msg.response_success);
        }
    }

    STRATUM_V1_reset_message(&msg);
}

static void read_session(GlobalState *gs, esp_transport_handle_t transport)
{
    size_t acc_len = 0;
    s_acc[0] = '\0';

    while (pool2_listen_enabled(gs)) {
        drain_shares(transport);
        if (acc_len >= sizeof(s_acc) - 1) {
            acc_len = 0;
            s_acc[0] = '\0';
        }

        int n = esp_transport_read(transport, s_acc + acc_len, sizeof(s_acc) - 1 - acc_len, 1000);
        if (n < 0) {
            ESP_LOGW(TAG, "Pool 2 read failed: %d", n);
            break;
        }
        if (n == 0) {
            continue;
        }

        acc_len += (size_t)n;
        s_acc[acc_len] = '\0';

        char *nl;
        while ((nl = strchr(s_acc, '\n')) != NULL) {
            *nl = '\0';
            if (nl > s_acc && nl[-1] == '\r') {
                nl[-1] = '\0';
            }
            handle_line(gs, transport, s_acc);
            size_t used = (size_t)(nl + 1 - s_acc);
            size_t rest = acc_len - used;
            memmove(s_acc, nl + 1, rest + 1);
            acc_len = rest;
        }
    }
}

void split_pool2_task(void *pvParameters)
{
    GlobalState *gs = (GlobalState *)pvParameters;

    while (1) {
        if (!pool2_listen_enabled(gs)) {
            pthread_mutex_lock(&s_lock);
            s_transport = NULL;
            drop_session_locked(gs);
            pthread_mutex_unlock(&s_lock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        char *url = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_URL);
        char *user = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_USER);
        char *pass = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_PASS);
        char *cert = nvs_config_get_string(NVS_CONFIG_FALLBACK_STRATUM_CERT);
        uint16_t port = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_PORT);
        uint16_t diff = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_DIFFICULTY);
        uint16_t tls = nvs_config_get_u16(NVS_CONFIG_FALLBACK_STRATUM_TLS);
        bool extranonce = nvs_config_get_bool(NVS_CONFIG_FALLBACK_STRATUM_EXTRANONCE_SUBSCRIBE);

        esp_transport_handle_t transport = STRATUM_V1_transport_init((tls_mode)tls, cert);
        if (!transport || url == NULL || user == NULL) {
            free(url);
            free(user);
            free(pass);
            free(cert);
            pthread_mutex_lock(&s_lock);
            drop_session_locked(gs);
            pthread_mutex_unlock(&s_lock);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        esp_err_t err = esp_transport_connect(transport, url, port, 5000);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Pool 2 connect failed");
            esp_transport_close(transport);
            esp_transport_destroy(transport);
            free(url);
            free(user);
            free(pass);
            free(cert);
            pthread_mutex_lock(&s_lock);
            drop_session_locked(gs);
            pthread_mutex_unlock(&s_lock);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        pthread_mutex_lock(&s_lock);
        s_transport = transport;
        s_uid = 1;
        s_difficulty = 0;
        s_extranonce[0] = '\0';
        s_have_extranonce = false;
        clear_pending();
        snprintf(s_user, sizeof(s_user), "%s", user);
        int uid = s_uid++;
        pthread_mutex_unlock(&s_lock);

        const char *asic_name = gs->DEVICE_CONFIG.family.asic.name;
        STRATUM_V1_subscribe(transport, uid, asic_name ? asic_name : "bitaxe");
        pthread_mutex_lock(&s_lock);
        uid = s_uid++;
        pthread_mutex_unlock(&s_lock);
        STRATUM_V1_authorize(transport, uid, user, pass ? pass : "");
        pthread_mutex_lock(&s_lock);
        uid = s_uid++;
        pthread_mutex_unlock(&s_lock);
        uint32_t version_mask = 0;
        STRATUM_V1_configure_version_rolling(transport, uid, &version_mask);
        if (diff != 0) {
            pthread_mutex_lock(&s_lock);
            uid = s_uid++;
            pthread_mutex_unlock(&s_lock);
            STRATUM_V1_suggest_difficulty(transport, uid, diff);
        }
        if (extranonce) {
            pthread_mutex_lock(&s_lock);
            uid = s_uid++;
            pthread_mutex_unlock(&s_lock);
            STRATUM_V1_extranonce_subscribe(transport, uid);
        }

        ESP_LOGI(TAG, "Pool 2 session open");
        read_session(gs, transport);

        pthread_mutex_lock(&s_lock);
        s_transport = NULL;
        drop_session_locked(gs);
        pthread_mutex_unlock(&s_lock);
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        free(url);
        free(user);
        free(pass);
        free(cert);
        (void)diff;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
