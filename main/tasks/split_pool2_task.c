#include "split_pool2_task.h"

#include "global_state.h"
#include "split_log.h"
#include "nvs_config.h"
#include "stratum_api.h"

#include "esp_log.h"
#include "esp_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "split_pool2";
static char s_acc[8192];

/* Split on, Pool 1 still mining, Pool 2 filled with Stratum V1. */
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
        strncpy(gs->SYSTEM_MODULE.split_pool2_job_id, msg.mining_notification->job_id,
                sizeof(gs->SYSTEM_MODULE.split_pool2_job_id) - 1);
        gs->SYSTEM_MODULE.split_pool2_job_id[sizeof(gs->SYSTEM_MODULE.split_pool2_job_id) - 1] = '\0';
        gs->SYSTEM_MODULE.split_pool2_connected = true;
        split_log_pooljob(TAG, 2, gs->SYSTEM_MODULE.split_pool2_job_id);
    } else if (msg.method == MINING_PING) {
        STRATUM_V1_pong(transport, msg.message_id);
    }

    STRATUM_V1_reset_message(&msg);
}

static void read_session(GlobalState *gs, esp_transport_handle_t transport)
{
    size_t acc_len = 0;
    s_acc[0] = '\0';

    while (pool2_listen_enabled(gs)) {
        if (acc_len >= sizeof(s_acc) - 1) {
            acc_len = 0;
            s_acc[0] = '\0';
        }

        int n = esp_transport_read(transport, s_acc + acc_len, sizeof(s_acc) - 1 - acc_len, 5000);
        if (n < 0) {
            break;
        }
        if (n == 0) {
            continue;
        }

        acc_len += (size_t) n;
        s_acc[acc_len] = '\0';

        char *nl;
        while ((nl = strchr(s_acc, '\n')) != NULL) {
            *nl = '\0';
            if (nl > s_acc && nl[-1] == '\r') {
                nl[-1] = '\0';
            }
            handle_line(gs, transport, s_acc);
            size_t used = (size_t) (nl + 1 - s_acc);
            size_t rest = acc_len - used;
            memmove(s_acc, nl + 1, rest + 1);
            acc_len = rest;
        }
    }
}

void split_pool2_task(void *pvParameters)
{
    GlobalState *gs = (GlobalState *) pvParameters;

    while (1) {
        if (!pool2_listen_enabled(gs)) {
            gs->SYSTEM_MODULE.split_pool2_connected = false;
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

        esp_transport_handle_t transport = STRATUM_V1_transport_init((tls_mode) tls, cert);
        if (!transport || url == NULL || user == NULL) {
            free(url);
            free(user);
            free(pass);
            free(cert);
            gs->SYSTEM_MODULE.split_pool2_connected = false;
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
            gs->SYSTEM_MODULE.split_pool2_connected = false;
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        int uid = 1;
        const char *asic_name = gs->DEVICE_CONFIG.family.asic.name;
        STRATUM_V1_subscribe(transport, uid++, asic_name ? asic_name : "bitaxe");
        STRATUM_V1_authorize(transport, uid++, user, pass ? pass : "");
        uint32_t version_mask = 0;
        STRATUM_V1_configure_version_rolling(transport, uid++, &version_mask);
        if (diff != 0) {
            STRATUM_V1_suggest_difficulty(transport, uid++, diff);
        }
        if (extranonce) {
            STRATUM_V1_extranonce_subscribe(transport, uid++);
        }

        ESP_LOGI(TAG, "Pool 2 session open, jobs stay on Pool 1");
        read_session(gs, transport);

        gs->SYSTEM_MODULE.split_pool2_connected = false;
        gs->SYSTEM_MODULE.split_pool2_job_id[0] = '\0';
        esp_transport_close(transport);
        esp_transport_destroy(transport);
        free(url);
        free(user);
        free(pass);
        free(cert);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
