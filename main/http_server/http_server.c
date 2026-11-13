#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <esp_heap_caps.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_vfs.h"

#include "dns_server.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include <arpa/inet.h>
#include "lwip/lwip_napt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "cJSON.h"
#include "global_state.h"
#include "nvs_config.h"
#include "system.h"
#include "connect.h"
#include "statistics_task.h"
#include "theme_api.h"
#include "axe-os/api/system/asic_settings.h"
#include "display.h"
#include "mdns.h"
#include "http_server.h"
#include "websocket.h"
#include "websocket_log.h"
#include "websocket_api.h"
#include "system_api_json.h"
#include "log_buffer.h"
#include "cjson_utils.h"
#include "utils.h"

static const char * TAG = "http_server";
static const char * CORS_TAG = "CORS";

static const char * STATS_LABEL_HASHRATE = "hashrate";
static const char * STATS_LABEL_HASHRATE_1m = "hashrate_1m";
static const char * STATS_LABEL_HASHRATE_10m = "hashrate_10m";
static const char * STATS_LABEL_HASHRATE_1h = "hashrate_1h";
static const char * STATS_LABEL_ERROR_PERCENTAGE = "errorPercentage";
static const char * STATS_LABEL_TIMESTAMP = "timestamp";
static const char * STATS_LABEL_ASIC_TEMP = "asicTemp";
static const char * STATS_LABEL_ASIC_TEMP2 = "asicTemp2";
static const char * STATS_LABEL_VR_TEMP = "vrTemp";
static const char * STATS_LABEL_ASIC_VOLTAGE = "asicVoltage";
static const char * STATS_LABEL_VOLTAGE = "voltage";
static const char * STATS_LABEL_POWER = "power";
static const char * STATS_LABEL_CURRENT = "current";
static const char * STATS_LABEL_FAN_SPEED = "fanSpeed";
static const char * STATS_LABEL_FAN_RPM = "fanRpm";
static const char * STATS_LABEL_FAN2_RPM = "fan2Rpm";
static const char * STATS_LABEL_WIFI_RSSI = "wifiRssi";
static const char * STATS_LABEL_FREE_HEAP = "freeHeap";
static const char * STATS_LABEL_RESPONSE_TIME = "responseTime";

static int system_info_prebuffer_len = 256;
static int system_statistics_prebuffer_len = 256;
static int system_wifi_scan_prebuffer_len = 256;
static int api_common_prebuffer_len = 256;

typedef enum
{
    SRC_HASHRATE,
    SRC_HASHRATE_1m,
    SRC_HASHRATE_10m,
    SRC_HASHRATE_1h,
    SRC_ERROR_PERCENTAGE,
    SRC_ASIC_TEMP,
    SRC_ASIC_TEMP2,
    SRC_VR_TEMP,
    SRC_ASIC_VOLTAGE,
    SRC_VOLTAGE,
    SRC_POWER,
    SRC_CURRENT,
    SRC_FAN_SPEED,
    SRC_FAN_RPM,
    SRC_FAN2_RPM,
    SRC_WIFI_RSSI,
    SRC_FREE_HEAP,
    SRC_RESPONSE_TIME,
    SRC_NONE // last
} DataSource;

DataSource strToDataSource(const char * sourceStr)
{
    if (NULL != sourceStr) {
        if (strcmp(sourceStr, STATS_LABEL_HASHRATE) == 0)     return SRC_HASHRATE;
        if (strcmp(sourceStr, STATS_LABEL_HASHRATE_1m) == 0)  return SRC_HASHRATE_1m;
        if (strcmp(sourceStr, STATS_LABEL_HASHRATE_10m) == 0) return SRC_HASHRATE_10m;
        if (strcmp(sourceStr, STATS_LABEL_HASHRATE_1h) == 0)  return SRC_HASHRATE_1h;
        if (strcmp(sourceStr, STATS_LABEL_ERROR_PERCENTAGE) == 0)  return SRC_ERROR_PERCENTAGE;
        if (strcmp(sourceStr, STATS_LABEL_VOLTAGE) == 0)      return SRC_VOLTAGE;
        if (strcmp(sourceStr, STATS_LABEL_POWER) == 0)        return SRC_POWER;
        if (strcmp(sourceStr, STATS_LABEL_CURRENT) == 0)      return SRC_CURRENT;
        if (strcmp(sourceStr, STATS_LABEL_ASIC_TEMP) == 0)    return SRC_ASIC_TEMP;
        if (strcmp(sourceStr, STATS_LABEL_ASIC_TEMP2) == 0)   return SRC_ASIC_TEMP2;
        if (strcmp(sourceStr, STATS_LABEL_VR_TEMP) == 0)      return SRC_VR_TEMP;
        if (strcmp(sourceStr, STATS_LABEL_ASIC_VOLTAGE) == 0) return SRC_ASIC_VOLTAGE;
        if (strcmp(sourceStr, STATS_LABEL_FAN_SPEED) == 0)    return SRC_FAN_SPEED;
        if (strcmp(sourceStr, STATS_LABEL_FAN_RPM) == 0)      return SRC_FAN_RPM;
        if (strcmp(sourceStr, STATS_LABEL_FAN2_RPM) == 0)     return SRC_FAN2_RPM;
        if (strcmp(sourceStr, STATS_LABEL_WIFI_RSSI) == 0)    return SRC_WIFI_RSSI;
        if (strcmp(sourceStr, STATS_LABEL_FREE_HEAP) == 0)    return SRC_FREE_HEAP;
        if (strcmp(sourceStr, STATS_LABEL_RESPONSE_TIME) == 0) return SRC_RESPONSE_TIME;
    }
    return SRC_NONE;
}

static esp_err_t GET_system_logs(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"bitaxe-logs.txt\"");

    uint64_t abs_pos = 0; /* Request reading from the absolute beginning */
    char chunk[4096];
    size_t read_bytes;
    esp_err_t res = ESP_OK;

    while ((read_bytes = log_buffer_read_absolute(&abs_pos, chunk, sizeof(chunk))) > 0) {
        res = httpd_resp_send_chunk(req, chunk, read_bytes);
        if (res != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk: %s", esp_err_to_name(res));
            break;
        }
    }

    /* Send empty chunk to terminate transfer */
    if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, NULL, 0);
    }

    return res;
}

static GlobalState * GLOBAL_STATE;
static httpd_handle_t server = NULL;

esp_err_t HTTP_send_json(httpd_req_t * req, const cJSON * item, int * prebuffer_len)
{
    const char * response = cJSON_PrintBuffered(item, *prebuffer_len, false);
    if (response != NULL) {
        int len = strlen(response);
        esp_err_t res = httpd_resp_send(req, response, len);
        if (len > *prebuffer_len) *prebuffer_len = len * 1.2;
        free((void *)response);
        return res;
    }
    return ESP_ERR_NO_MEM;
}

/* Handler for WiFi scan endpoint */
static esp_err_t GET_wifi_scan(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Give some time for the connected flag to take effect
    vTaskDelay(100 / portTICK_PERIOD_MS);
    
    wifi_ap_record_simple_t ap_records[20];
    uint16_t ap_count = 0;

    esp_err_t err = wifi_scan(ap_records, &ap_count);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WiFi scan failed");
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();

    for (int i = 0; i < ap_count; i++) {
        cJSON *network = cJSON_CreateObject();
        cJSON_AddStringToObject(network, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(network, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(network, "authmode", ap_records[i].authmode);
        cJSON_AddItemToArray(networks, network);
    }

    cJSON_AddItemToObject(root, "networks", networks);

    esp_err_t res = HTTP_send_json(req, root, &system_wifi_scan_prebuffer_len);

    cJSON_Delete(root);

    return res;
}


#define REST_CHECK(a, str, goto_tag, ...)                                                                                          \
    do {                                                                                                                           \
        if (!(a)) {                                                                                                                \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__);                                                  \
            goto goto_tag;                                                                                                         \
        }                                                                                                                          \
    } while (0)

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)
#define MESSAGE_QUEUE_SIZE (128)

typedef struct rest_server_context
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static esp_err_t ip_in_private_range(uint32_t address) {
    uint32_t ip_address = ntohl(address);

    // 10.0.0.0 - 10.255.255.255 (Class A)
    if ((ip_address >= 0x0A000000) && (ip_address <= 0x0AFFFFFF)) {
        return ESP_OK;
    }

    // 172.16.0.0 - 172.31.255.255 (Class B)
    if ((ip_address >= 0xAC100000) && (ip_address <= 0xAC1FFFFF)) {
        return ESP_OK;
    }

    // 192.168.0.0 - 192.168.255.255 (Class C)
    if ((ip_address >= 0xC0A80000) && (ip_address <= 0xC0A8FFFF)) {
        return ESP_OK;
    }

    return ESP_FAIL;
}

static uint32_t extract_origin_ip_addr(char *origin)
{
    char host_str[128];
    uint32_t origin_ip_addr = 0;

    // Find the start of the hostname in the Origin header
    const char *prefix = "http://";
    char *host_start = strstr(origin, prefix);
    if (host_start) {
        host_start += strlen(prefix); // Move past "http://"

        // Extract the hostname portion (up to the next '/')
        char *host_end = strchr(host_start, '/');
        size_t host_len = host_end ? (size_t)(host_end - host_start) : strlen(host_start);
        if (host_len < sizeof(host_str)) {
            strncpy(host_str, host_start, host_len);
            host_str[host_len] = '\0'; // Null-terminate the string

            // Check if it's an IP address or hostname
            struct in_addr addr;
            if (inet_pton(AF_INET, host_str, &addr) == 1) {
                origin_ip_addr = addr.s_addr;
                ESP_LOGD(CORS_TAG, "Extracted IP address %lu", origin_ip_addr);
            } else {
                ESP_LOGD(CORS_TAG, "Origin contains hostname: %s (not an IP)", host_str);
                // For hostnames, return 0 to indicate it's not an IP address
                origin_ip_addr = 0;
            }
        } else {
            ESP_LOGW(CORS_TAG, "Hostname string is too long: %s", host_start);
        }
    }

    return origin_ip_addr;
}

// Helper function to normalize hostname by stripping ".local" suffix if present
// This prevents Avahi from creating duplicate ".local.local" hostnames
static void normalize_hostname(char *hostname, size_t max_len) {
    if (hostname == NULL || strlen(hostname) == 0) {
        return;
    }

    size_t len = strlen(hostname);
    const char *suffix = ".local";
    size_t suffix_len = strlen(suffix);

    // Check if hostname ends with ".local" (case-insensitive)
    if (len > suffix_len &&
        strcasecmp(hostname + len - suffix_len, suffix) == 0) {
        // Strip the ".local" suffix
        hostname[len - suffix_len] = '\0';
        ESP_LOGD(TAG, "Normalized hostname from '%s.local' to '%s'", hostname, hostname);
    }
}

esp_err_t is_network_allowed(httpd_req_t * req)
{
    if (GLOBAL_STATE->SYSTEM_MODULE.ap_enabled == true) {
        ESP_LOGI(CORS_TAG, "Device in AP mode. Allowing CORS.");
        return ESP_OK;
    }

    int sockfd = httpd_req_to_sockfd(req);
    char ipstr[INET6_ADDRSTRLEN];
    struct sockaddr_in6 addr;   // esp_http_server uses IPv6 addressing
    socklen_t addr_size = sizeof(addr);

    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
        ESP_LOGE(CORS_TAG, "Error getting client IP");
        return ESP_FAIL;
    }

    uint32_t request_ip_addr = addr.sin6_addr.un.u32_addr[3];

    // // Convert to IPv6 string
    // inet_ntop(AF_INET, &addr.sin6_addr, ipstr, sizeof(ipstr));

    // Convert to IPv4 string
    inet_ntop(AF_INET, &request_ip_addr, ipstr, sizeof(ipstr));

    // Attempt to get the Origin header.
    char origin[128];
    uint32_t origin_ip_addr;
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin)) == ESP_OK) {
        ESP_LOGD(CORS_TAG, "Origin header: %s", origin);
        origin_ip_addr = extract_origin_ip_addr(origin);
    } else {
        ESP_LOGD(CORS_TAG, "No origin header found.");
        origin_ip_addr = request_ip_addr;
    }

    if (origin_ip_addr != 0 && ip_in_private_range(origin_ip_addr) == ESP_OK && ip_in_private_range(request_ip_addr) == ESP_OK) {
        ESP_LOGD(CORS_TAG, "Origin and IP both in private range. Allowing.");
        return ESP_OK;
    }
    
    // If origin contains hostname (origin_ip_addr == 0), proceed to hostname validation
    if (origin_ip_addr == 0) {
        ESP_LOGD(CORS_TAG, "Origin contains hostname, proceeding to hostname validation");
    }

    // Check if Origin header matches the avahi hostname or is a local-network hostname
    if (httpd_req_get_hdr_value_len(req, "Origin") > 0) {
        httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
        ESP_LOGD(CORS_TAG, "Origin header: %s", origin);

        // Extract the host portion from the origin for local-hostname validation
        char host_str[128] = {0};
        const char *prefix = "http://";
        char *host_start = strstr(origin, prefix);
        bool is_local_hostname = false;
        if (host_start) {
            host_start += strlen(prefix);
            // Strip port if present
            char *colon = strchr(host_start, ':');
            char *slash = strchr(host_start, '/');
            size_t host_len = 0;
            if (colon) {
                host_len = colon - host_start;
            } else if (slash) {
                host_len = slash - host_start;
            } else {
                host_len = strlen(host_start);
            }
            if (host_len > 0 && host_len < sizeof(host_str)) {
                strncpy(host_str, host_start, host_len);
                host_str[host_len] = '\0';

                // Allow any .local hostname (mDNS, inherently local network)
                size_t hlen = strlen(host_str);
                if (hlen > 6 && strcasecmp(host_str + hlen - 6, ".local") == 0) {
                    is_local_hostname = true;
                    ESP_LOGD(CORS_TAG, "Origin host '%s' is a .local mDNS hostname - allowing", host_str);
                }
                // Allow any bare hostname (no dots, only resolvable on local network)
                else if (strchr(host_str, '.') == NULL) {
                    is_local_hostname = true;
                    ESP_LOGD(CORS_TAG, "Origin host '%s' is a bare local hostname - allowing", host_str);
                }
            }
        }

        if (is_local_hostname) {
            ESP_LOGD(CORS_TAG, "Request from local hostname - allowing access");
            return ESP_OK;
        }

        // Fall back to exact match against this device's configured hostname
        char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        ESP_LOGD(CORS_TAG, "Configured hostname: %s", hostname);
        // Match origin as http://<hostname>.local[:port] or http://<hostname>[:port]
        const char *patterns[] = { "http://%s.local", "http://%s" };
        bool matched = false;
        for (int i = 0; i < 2 && !matched; i++) {
            char expected[256];
            snprintf(expected, sizeof(expected), patterns[i], hostname);
            size_t len = strlen(expected);
            // Origin must start with expected, followed by end-of-string or ':port'
            if (strncmp(origin, expected, len) == 0 &&
                (origin[len] == '\0' || origin[len] == ':')) {
                matched = true;
            }
        }
        free(hostname);
        if (matched) {
            ESP_LOGD(CORS_TAG, "Request from hostname - allowing access");
            return ESP_OK;
        }
    } else {
        ESP_LOGD(CORS_TAG, "No Origin header found");
    }

    ESP_LOGI(CORS_TAG, "Client is NOT in the private ip ranges or same range as server.");
    return ESP_FAIL;
}

/* Function for stopping the webserver */
void stop_webserver(httpd_handle_t server)
{
    if (server) {
        /* Stop the httpd server */
        httpd_stop(server);
    }
}

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t * req, const char * filepath)
{
    const char * type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "image/svg+xml";
    } else if (CHECK_FILE_EXTENSION(filepath, ".pdf")) {
        type = "application/pdf";
    } else if (CHECK_FILE_EXTENSION(filepath, ".woff2")) {
        type = "font/woff2";
    }
    return httpd_resp_set_type(req, type);
}

esp_err_t set_cors_headers(httpd_req_t * req)
{
    esp_err_t err;

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, DELETE, OPTIONS");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    err = httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    if (err != ESP_OK) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

/* Recovery handler */
static esp_err_t rest_recovery_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    extern const unsigned char recovery_page_start[] asm("_binary_recovery_page_html_start");
    extern const unsigned char recovery_page_end[] asm("_binary_recovery_page_html_end");
    const size_t recovery_page_size = (recovery_page_end - recovery_page_start);
    httpd_resp_send_chunk(req, (const char*)recovery_page_start, recovery_page_size);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* Send a 404 as JSON for unhandled api routes */
static esp_err_t rest_api_common_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_status(req, "404 Not Found");

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    cJSON * root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "error", "unknown route");

    esp_err_t res = HTTP_send_json(req, root, &api_common_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

static bool file_exists(const char *path) {
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

static esp_err_t send_setup_page(httpd_req_t * req);
static esp_err_t captive_portal_redirect(httpd_req_t * req);

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t * req)
{
    char filepath[FILE_PATH_MAX];
    char gz_file[FILE_PATH_MAX];
    uint8_t filePathLength = sizeof(filepath);

    rest_server_context_t * rest_context = (rest_server_context_t *) req->user_ctx;

    if (GLOBAL_STATE != NULL && GLOBAL_STATE->SYSTEM_MODULE.ap_enabled &&
        (strcmp(req->uri, "/") == 0 || strncmp(req->uri, "/index.html", 11) == 0)) {
        return send_setup_page(req);
    }

    strlcpy(filepath, rest_context->base_path, filePathLength);
    if (req->uri[strlen(req->uri) - 1] == '/') {
        strlcat(filepath, "/index.html", filePathLength);
    } else {
        strlcat(filepath, req->uri, filePathLength);
    }
    set_content_type_from_file(req, filepath);

    strlcpy(gz_file, filepath, filePathLength);
    strlcat(gz_file, ".gz", filePathLength);

    bool serve_gz = file_exists(gz_file);
    const char *file_to_open = serve_gz ? gz_file : filepath;

    int fd = open(file_to_open, O_RDONLY, 0);
    if (fd == -1) {
        if (GLOBAL_STATE != NULL && GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
            return captive_portal_redirect(req);
        }
        // Set status
        httpd_resp_set_status(req, "302 Temporary Redirect");
        // Redirect to the "/" root directory
        httpd_resp_set_hdr(req, "Location", "/");
        // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
        httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

        ESP_LOGI(TAG, "Redirecting to root");
        return ESP_OK;
    }
    if (req->uri[strlen(req->uri) - 1] != '/') {
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=2592000");
    }

    if (serve_gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    char * chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", file_to_open);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_OK;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handle_options_request(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers for OPTIONS request
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    // Send a blank response for OPTIONS request
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

bool check_settings_and_update(const cJSON * const root, char **redirect_url)
{
    bool result = true;
    char *old_hostname = NULL;
    bool hostname_changed = false;

    // Check for hostname change first
    cJSON *hostname_item = cJSON_GetObjectItem(root, "hostname");
    if (hostname_item && cJSON_IsString(hostname_item)) {
        old_hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        char normalized_new_hostname[64];
        strlcpy(normalized_new_hostname, hostname_item->valuestring, sizeof(normalized_new_hostname));
        normalize_hostname(normalized_new_hostname, sizeof(normalized_new_hostname));
        if (strcmp(old_hostname, normalized_new_hostname) != 0) {
            hostname_changed = true;
            ESP_LOGI(TAG, "Hostname change detected: %s -> %s", old_hostname, hostname_item->valuestring);
        }
    }

    for (NvsConfigKey key = 0; key < NVS_CONFIG_COUNT; key++) {
        Settings *setting = nvs_config_get_settings(key);
        if (!setting->rest_name) continue;

        cJSON * item = cJSON_GetObjectItem(root, setting->rest_name);
        if (!item) continue;

        switch (setting->type) {
            case TYPE_STR: {
                if (!cJSON_IsString(item)) {
                    ESP_LOGW(TAG, "Invalid type for '%s', expected string", setting->rest_name);                            
                    result = false;
                } else {
                    const size_t str_value_len = strlen(item->valuestring);
                    if ((str_value_len < setting->min) || (str_value_len > setting->max)) {
                        ESP_LOGW(TAG, "Value '%s' for '%s' is out of length (%d-%d)", item->valuestring, setting->rest_name, setting->min, setting->max);
                        result = false;
                    }
                }
                break;
            }
            case TYPE_U16:
            case TYPE_I32: {
                if (!cJSON_IsNumber(item)) {
                    ESP_LOGW(TAG, "Invalid type for '%s', expected number", setting->rest_name);                            
                    result = false;
                } else if ((item->valueint < setting->min) || (item->valueint > setting->max)) {
                    ESP_LOGW(TAG, "Value '%d' for '%s' is out of range", item->valueint, setting->rest_name);
                    result = false;
                }
                break;
            }
            case TYPE_U64: {
                if (!cJSON_IsNumber(item)) {
                    ESP_LOGW(TAG, "Invalid type for '%s', expected number", setting->rest_name);                            
                    result = false;
                } else if ((item->valuedouble < setting->min) || (item->valuedouble > setting->max)) {
                    ESP_LOGW(TAG, "Value '%lld' for '%s' is out of range", (long long)item->valuedouble, setting->rest_name);
                    result = false;
                }
                break;
            }
            case TYPE_FLOAT: {
                if (!cJSON_IsNumber(item)) {
                    ESP_LOGW(TAG, "Invalid type for '%s', expected number", setting->rest_name);                            
                    result = false;
                } else if ((item->valuedouble < setting->min) || (item->valuedouble > setting->max)) {
                    ESP_LOGW(TAG, "Value '%f' for '%s' is out of range", item->valuedouble, setting->rest_name);
                    result = false;
                }
                break;
            }
            case TYPE_BOOL: {
                if (!cJSON_IsNumber(item) && !cJSON_IsBool(item) && !cJSON_IsTrue(item) && !cJSON_IsFalse(item)) {
                    ESP_LOGW(TAG, "Invalid type for '%s', expected bool", setting->rest_name);                            
                    result = false;
                } else if ((item->valueint < setting->min) || (item->valueint > setting->max)) {
                    ESP_LOGW(TAG, "Value '%d' for '%s' is out of range", item->valueint, setting->rest_name);
                    result = false;
                }
                break;
            }
        }

        if (key == NVS_CONFIG_DISPLAY && cJSON_IsString(item) && get_display_config(item->valuestring) == NULL) {
            ESP_LOGW(TAG, "Invalid display config: '%s'", item->valuestring);
            result = false;
        }
        if (key == NVS_CONFIG_ROTATION && item->valueint != 0 && item->valueint != 90 && item->valueint != 180 && item->valueint != 270) {
            ESP_LOGW(TAG, "Invalid display rotation: '%d'", item->valueint);
            result = false;
        }
        if ((key == NVS_CONFIG_STRATUM_PROTOCOL || key == NVS_CONFIG_FALLBACK_STRATUM_PROTOCOL) && cJSON_IsString(item)) {
            if (stratum_protocol_from_string(item->valuestring) == STRATUM_PROTOCOL_UNKNOWN) {
                ESP_LOGW(TAG, "Invalid stratum protocol: '%s'", item->valuestring);
                result = false;
            }
        }
        if ((key == NVS_CONFIG_SV2_CHANNEL_TYPE || key == NVS_CONFIG_FALLBACK_SV2_CHANNEL_TYPE) && cJSON_IsString(item)) {
            if (sv2_channel_type_from_string(item->valuestring) == SV2_CHANNEL_UNKNOWN) {
                ESP_LOGW(TAG, "Invalid SV2 channel type: '%s'", item->valuestring);
                result = false;
            }
        }
    }

    if (result) {
        // update NVS (if result is okay) and clean up    
        for (NvsConfigKey key = 0; key < NVS_CONFIG_COUNT; key++) {
            Settings *setting = nvs_config_get_settings(key);
            if (!setting || !setting->rest_name) continue;

            cJSON * item = cJSON_GetObjectItem(root, setting->rest_name);
            if (!item) continue;

            switch(setting->type) {
                case TYPE_STR:

                    if (key == NVS_CONFIG_HOSTNAME)
                    {
                        char normalized_hostname[64];
                        strlcpy(normalized_hostname, item->valuestring, sizeof(normalized_hostname));
                        normalize_hostname(normalized_hostname, sizeof(normalized_hostname));
                        nvs_config_set_string(key, normalized_hostname);
                        update_mdns_hostname(normalized_hostname, GLOBAL_STATE);
                        ESP_LOGI(TAG, "Updated hostname to: %s", normalized_hostname);
                    }
                    else
                    {
                        nvs_config_set_string(key, item->valuestring);
                    }

                    break;
                case TYPE_U16:
                    nvs_config_set_u16(key, (uint16_t)item->valueint);
                    break;
                case TYPE_I32:
                    nvs_config_set_i32(key, item->valueint);
                    break;
                case TYPE_U64:
                    nvs_config_set_u64(key, (uint64_t)item->valuedouble);
                    break;
                case TYPE_BOOL:
                    nvs_config_set_bool(key, item->valueint != 0 || cJSON_IsTrue(item));
                    break;
                case TYPE_FLOAT:
                    nvs_config_set_float(key, (float)item->valuedouble);
                    break;
            }
        }
    }

    // Set redirect URL if hostname changed
    if (hostname_changed && redirect_url) {
        char *current_hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
        if (current_hostname) {
            *redirect_url = malloc(256);
            if (*redirect_url) {
                snprintf(*redirect_url, 256, "http://%s.local", current_hostname);
                ESP_LOGI(TAG, "Hostname redirect URL set: %s", *redirect_url);
            }
            free(current_hostname);
        }
    }

    if (old_hostname) {
        free(old_hostname);
    }

    return result;
}

static esp_err_t PATCH_update_settings(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char * buf = ((rest_server_context_t *) (req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_OK;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_OK;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    cJSON * root = cJSON_Parse(buf);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_OK;
    }

    cJSON *hostname_item = cJSON_GetObjectItem(root, "hostname");
    char *current_hostname = cJSON_IsString(hostname_item) ? nvs_config_get_string(NVS_CONFIG_HOSTNAME) : NULL;
    bool hostname_changed = cJSON_IsString(hostname_item) &&
                            (current_hostname == NULL || strcmp(current_hostname, hostname_item->valuestring) != 0);
    free(current_hostname);

    char *redirect_url = NULL;
    if (!check_settings_and_update(root, &redirect_url)) {
        cJSON_Delete(root);
        if (redirect_url) {
            free(redirect_url);
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Wrong API input");
        return ESP_OK;
    }

    if (hostname_changed) {
        esp_err_t err = wifi_apply_hostname(hostname_item->valuestring);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_IF_NOT_READY) {
            ESP_LOGW(TAG, "Failed to apply hostname live: %s", esp_err_to_name(err));
        }
    }

    // Create response JSON
    cJSON *response = cJSON_CreateObject();
    if (redirect_url) {
        cJSON_AddStringToObject(response, "status", "success");
        cJSON *redirect = cJSON_CreateObject();
        cJSON_AddStringToObject(redirect, "url", redirect_url);
        cJSON_AddNumberToObject(redirect, "delay", 2000);
        cJSON_AddStringToObject(redirect, "message", "Hostname updated. Redirecting to new address...");
        cJSON_AddItemToObject(response, "redirect", redirect);
        
        ESP_LOGI(TAG, "Sending hostname change redirect response");
        esp_err_t res = HTTP_send_json(req, response, &api_common_prebuffer_len);
        cJSON_Delete(response);
        free(redirect_url);
        cJSON_Delete(root);
        return res;
    } else {
        cJSON_Delete(root);
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
}

static esp_err_t POST_identify(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Identify mode enabled for 30s");

    httpd_resp_set_type(req, "application/json");

    cJSON * root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_OK;
    }

    if (GLOBAL_STATE->SYSTEM_MODULE.identify_mode_time_ms > 0) {
        GLOBAL_STATE->SYSTEM_MODULE.identify_mode_time_ms = 0;
        cJSON_AddStringToObject(root, "message", "The device no longer says \"Hi!\".");
    } else {
        GLOBAL_STATE->SYSTEM_MODULE.identify_mode_time_ms = 30000;
         cJSON_AddStringToObject(root, "message", "The device says \"Hi!\" for 30 seconds.");
    }

    esp_err_t res = HTTP_send_json(req, root, &api_common_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

static esp_err_t POST_restart(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Restarting System because of API Request");

    httpd_resp_set_type(req, "application/json");

    cJSON * root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_OK;
    }

    cJSON_AddStringToObject(root, "message", "System will restart shortly.");

    // Send HTTP response before restarting
    esp_err_t res = HTTP_send_json(req, root, &api_common_prebuffer_len);

    cJSON_Delete(root);

    // Delay to ensure the response is sent
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Restart the system
    esp_restart();

    // This return statement will never be reached, but it's good practice to include it
    return res;
}

static esp_err_t POST_dismiss_block_found(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Dismissing block found notification");

    httpd_resp_set_type(req, "application/json");

    cJSON * root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_OK;
    }

    GLOBAL_STATE->SYSTEM_MODULE.show_new_block = false;

    cJSON_AddNumberToObject(root, "blockFound", GLOBAL_STATE->SYSTEM_MODULE.block_found);
    cJSON_AddBoolToObject(root, "showNewBlock", GLOBAL_STATE->SYSTEM_MODULE.show_new_block);
    cJSON_AddStringToObject(root, "message", "Block found notification dismissed");

    esp_err_t res = HTTP_send_json(req, root, &api_common_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

static esp_err_t POST_mining_pause(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    GLOBAL_STATE->SYSTEM_MODULE.mining_paused = true;
    ESP_LOGI(TAG, "Mining paused by API request");

    httpd_resp_set_type(req, "application/json");
    cJSON * resp = cJSON_CreateObject();
    if (resp == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal error");
        return ESP_OK;
    }
    cJSON_AddStringToObject(resp, "message", "Mining paused");
    esp_err_t res = HTTP_send_json(req, resp, &api_common_prebuffer_len);
    cJSON_Delete(resp);
    return res;
}

static esp_err_t POST_mining_resume(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    GLOBAL_STATE->SYSTEM_MODULE.mining_paused = false;
    ESP_LOGI(TAG, "Mining resumed by API request");

    httpd_resp_set_type(req, "application/json");
    cJSON * resp = cJSON_CreateObject();
    if (resp == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal error");
        return ESP_OK;
    }
    cJSON_AddStringToObject(resp, "message", "Mining resumed");
    esp_err_t res = HTTP_send_json(req, resp, &api_common_prebuffer_len);
    cJSON_Delete(resp);
    return res;
}

/* Simple handler for getting system handler */
static esp_err_t GET_system_info(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    cJSON * root = system_api_get_full_json(GLOBAL_STATE);

    // Add mDNS-specific fields on top of the base system info
    char * hostname_base = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
    char * mdns_hostname = GLOBAL_STATE->SYSTEM_MODULE.mdns_hostname;
    char full_hostname[72]; // 64 (mdns_hostname max) + 6 (".local") + 2 (margin)
    
    if (mdns_hostname != NULL && strlen(mdns_hostname) > 0) {
        snprintf(full_hostname, sizeof(full_hostname), "%s.local", mdns_hostname);
    } else {
        snprintf(full_hostname, sizeof(full_hostname), "%s.local", hostname_base ? hostname_base : "unknown");
    }

    cJSON_AddStringToObject(root, "fullHostname", full_hostname);
    if (strlen(mdns_hostname) > 0) {
        cJSON_AddStringToObject(root, "mdnsHostname", mdns_hostname);
    }

    free(hostname_base);

    esp_err_t res = HTTP_send_json(req, root, &system_info_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

static esp_err_t GET_system_statistics(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    size_t bufLen = httpd_req_get_url_query_len(req) + 1;
    bool dataSelection[SRC_NONE] = {false};
    bool selectionCheck = false;

    // Check query parameters
    if (1 < bufLen) {
        char buf[bufLen];
        if (httpd_req_get_url_query_str(req, buf, bufLen) == ESP_OK) {
            char columns_enc[bufLen];
            if (httpd_query_key_value(buf, "columns", columns_enc, bufLen) == ESP_OK) {
                char columns[bufLen];
                url_decode(columns, columns_enc);
                char * param = strtok(columns, ",");
                while (NULL != param) {
                    DataSource sourceParam = strToDataSource(param);
                    if (SRC_NONE != sourceParam) {
                        dataSelection[sourceParam] = true;
                        selectionCheck = true;
                    }
                    param = strtok(NULL, ",");
                }
            }
        }
    }

    if (!selectionCheck) {
        // Enable all
        for (int i = 0; i < SRC_NONE; i++) {
            dataSelection[i] = true;
        }
    }

    // Create object for statistics
    cJSON * root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "currentTimestamp", (esp_timer_get_time() / 1000));

    cJSON * labelArray = cJSON_CreateArray();
    if (dataSelection[SRC_HASHRATE]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_HASHRATE)); }
    if (dataSelection[SRC_HASHRATE_1m]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_HASHRATE_1m)); }
    if (dataSelection[SRC_HASHRATE_10m]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_HASHRATE_10m)); }
    if (dataSelection[SRC_HASHRATE_1h]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_HASHRATE_1h)); }
    if (dataSelection[SRC_ERROR_PERCENTAGE]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_ERROR_PERCENTAGE)); }
    if (dataSelection[SRC_ASIC_TEMP]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_ASIC_TEMP)); }
    if (dataSelection[SRC_ASIC_TEMP2]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_ASIC_TEMP2)); }
    if (dataSelection[SRC_VR_TEMP]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_VR_TEMP)); }
    if (dataSelection[SRC_ASIC_VOLTAGE]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_ASIC_VOLTAGE)); }
    if (dataSelection[SRC_VOLTAGE]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_VOLTAGE)); }
    if (dataSelection[SRC_POWER]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_POWER)); }
    if (dataSelection[SRC_CURRENT]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_CURRENT)); }
    if (dataSelection[SRC_FAN_SPEED]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_FAN_SPEED)); }
    if (dataSelection[SRC_FAN_RPM]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_FAN_RPM)); }
    if (dataSelection[SRC_FAN2_RPM]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_FAN2_RPM)); }
    if (dataSelection[SRC_WIFI_RSSI]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_WIFI_RSSI)); }
    if (dataSelection[SRC_FREE_HEAP]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_FREE_HEAP)); }
    if (dataSelection[SRC_RESPONSE_TIME]) { cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_RESPONSE_TIME)); }
    cJSON_AddItemToArray(labelArray, cJSON_CreateString(STATS_LABEL_TIMESTAMP));

    cJSON_AddItemToObject(root, "labels", labelArray);

    cJSON * statsArray = cJSON_AddArrayToObject(root, "statistics");
    struct StatisticsData statsData;
    uint16_t index = 0;

    while (getStatisticData(index++, &statsData)) {
        cJSON * valueArray = cJSON_CreateArray();
        if (dataSelection[SRC_HASHRATE]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.hashrate)); }
        if (dataSelection[SRC_HASHRATE_1m]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.hashrate_1m)); }
        if (dataSelection[SRC_HASHRATE_10m]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.hashrate_10m)); }
        if (dataSelection[SRC_HASHRATE_1h]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.hashrate_1h)); }
        if (dataSelection[SRC_ERROR_PERCENTAGE]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.errorPercentage)); }
        if (dataSelection[SRC_ASIC_TEMP]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.chipTemperature)); }
        if (dataSelection[SRC_ASIC_TEMP2]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.chipTemperature2)); }
        if (dataSelection[SRC_VR_TEMP]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.vrTemperature)); }
        if (dataSelection[SRC_ASIC_VOLTAGE]) { cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.coreVoltageActual)); }
        if (dataSelection[SRC_VOLTAGE]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.voltage)); }
        if (dataSelection[SRC_POWER]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.power)); }
        if (dataSelection[SRC_CURRENT]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.current)); }
        if (dataSelection[SRC_FAN_SPEED]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.fanSpeed)); }
        if (dataSelection[SRC_FAN_RPM]) { cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.fanRPM)); }
        if (dataSelection[SRC_FAN2_RPM]) { cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.fan2RPM)); }
        if (dataSelection[SRC_WIFI_RSSI]) { cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.wifiRSSI)); }
        if (dataSelection[SRC_FREE_HEAP]) { cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.freeHeap)); }
        if (dataSelection[SRC_RESPONSE_TIME]) { cJSON_AddItemToArray(valueArray, cJSON_CreateFloat(statsData.responseTime)); }
        cJSON_AddItemToArray(valueArray, cJSON_CreateNumber(statsData.timestamp));

        cJSON_AddItemToArray(statsArray, valueArray);
    }

    esp_err_t res = HTTP_send_json(req, root, &system_statistics_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

static esp_err_t GET_scoreboard(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    Scoreboard *scoreboard = &GLOBAL_STATE->SYSTEM_MODULE.scoreboard;
    cJSON * root = cJSON_CreateArray();

    if (xSemaphoreTake(scoreboard->mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < scoreboard->count; i++) {
            const ScoreboardEntry *e = &scoreboard->entries[i];
            cJSON *entry = cJSON_CreateObject();

            char nonce_str[9], version_bits_str[9];
            snprintf(nonce_str, sizeof(nonce_str), "%08X", (unsigned int)e->nonce);
            snprintf(version_bits_str, sizeof(version_bits_str), "%08X", (unsigned int)e->version_bits);

            cJSON_AddNumberToObject(entry, "difficulty", e->difficulty);
            cJSON_AddStringToObject(entry, "job_id", e->job_id);
            cJSON_AddStringToObject(entry, "extranonce2", e->extranonce2);
            cJSON_AddNumberToObject(entry, "ntime", e->ntime);
            cJSON_AddStringToObject(entry, "nonce", nonce_str);
            cJSON_AddStringToObject(entry, "version_bits", version_bits_str);

            cJSON_AddItemToArray(root, entry);
        }
        xSemaphoreGive(scoreboard->mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex for JSON conversion");
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to take mutex for JSON conversion");
        return ESP_OK;
    }

    esp_err_t res = HTTP_send_json(req, root, &api_common_prebuffer_len);

    cJSON_Delete(root);

    return res;
}

esp_err_t POST_WWW_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }

    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "www.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Starting...");

    char buf[1000];
    int remaining = req->content_len;

    const esp_partition_t * www_partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "www");
    if (www_partition == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "WWW partition not found");
        return ESP_OK;
    }

    // Don't attempt to write more than what can be stored in the partition
    if (remaining > www_partition->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File provided is too large for device");
        return ESP_OK;
    }

    // Erase the entire www partition before writing, in chunks to prevent WDT timeout
    size_t erase_size = 65536; // 64KB chunks
    for (size_t offset = 0; offset < www_partition->size; offset += erase_size) {
        size_t size_to_erase = MIN(erase_size, www_partition->size - offset);
        ESP_ERROR_CHECK(esp_partition_erase_range(www_partition, offset, size_to_erase));
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    int chunks = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        } else if (recv_len <= 0) {
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        if (esp_partition_write(www_partition, www_partition->size - remaining, (const void *) buf, recv_len) != ESP_OK) {
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write Error");
            return ESP_OK;
        }


        uint8_t percentage = 100 - ((remaining * 100 / req->content_len));
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Working (%d%%)", percentage);

        remaining -= recv_len;

        chunks++;
        if (chunks % 16 == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "WWW update complete\n");

    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Finished...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = false;

    return ESP_OK;
}

/*
 * Handle OTA file upload
 */
esp_err_t POST_OTA_update(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not allowed in AP mode");
        return ESP_OK;
    }
    
    GLOBAL_STATE->SYSTEM_MODULE.is_firmware_update = true;
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_filename, 20, "esp-miner.bin");
    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Starting...");

    char buf[1000];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t * ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    int chunks = 0;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

        // Timeout Error: Just retry
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;

            // Serious Error: Abort OTA
        } else if (recv_len <= 0) {
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Protocol Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_OK;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *) buf, recv_len) != ESP_OK) {
            esp_ota_abort(ota_handle);
            snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Write Error");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write Error");
            return ESP_OK;
        }

        uint8_t percentage = 100 - ((remaining * 100 / req->content_len));

        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Working (%d%%)", percentage);

        remaining -= recv_len;

        chunks++;
        if (chunks % 16 == 0) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
        snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Validation Error");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
        return ESP_OK;
    }

    snprintf(GLOBAL_STATE->SYSTEM_MODULE.firmware_update_status, 20, "Rebooting...");

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");
    ESP_LOGI(TAG, "Restarting System because of Firmware update complete");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

static esp_err_t send_setup_page(httpd_req_t * req)
{
    static const char page[] =
        "<!DOCTYPE html><html lang=\"en\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Satoshiware Wi-Fi</title>"
        "<style>"
        "*{box-sizing:border-box}"
        "body{margin:0;min-height:100vh;background:#0f1115;color:#f3f4f6;"
        "font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
        "display:flex;align-items:flex-start;justify-content:center;padding:20px 14px}"
        ".card{width:100%;max-width:460px;background:#171a21;border:1px solid #282d38;"
        "border-radius:12px;overflow:hidden;box-shadow:0 12px 32px rgba(0,0,0,.45)}"
        ".bar{display:flex;align-items:center;justify-content:space-between;gap:12px;"
        "padding:14px 18px;background:#1d212a;border-bottom:1px solid #282d38}"
        ".brand{display:flex;align-items:center;gap:10px;min-width:0}"
        ".emblem{height:46px;width:46px;display:block;flex-shrink:0}"
        ".wm{height:24px;width:auto;display:block;max-width:calc(100% - 56px)}"
        ".ip{font-size:12px;color:#8b93a5;font-family:ui-monospace,Menlo,monospace}"
        ".body{padding:18px}"
        "h1{margin:0 0 4px;font-size:20px;font-weight:600;color:#fff}"
        ".sub{margin:0 0 16px;font-size:14px;color:#9aa2b2}"
        ".field{margin-bottom:14px}"
        "label{display:block;margin-bottom:6px;font-size:13px;font-weight:500;color:#c7cdd9}"
        ".row{display:flex;gap:8px}"
        "input{width:100%;padding:10px 12px;font-size:15px;color:#f3f4f6;background:#0f1115;"
        "border:1px solid #323846;border-radius:8px;outline:none}"
        "input:focus{border-color:#f7931a}"
        ".pw{position:relative}"
        ".pw input{padding-right:58px}"
        ".eye{position:absolute;right:6px;top:50%;transform:translateY(-50%);background:none;"
        "border:0;color:#9aa2b2;font-size:12px;padding:6px 8px;cursor:pointer}"
        "button{font-family:inherit;font-size:14px;font-weight:600;border-radius:8px;"
        "padding:10px 15px;cursor:pointer;border:1px solid transparent}"
        ".pri{background:#f7931a;color:#111317}"
        ".pri:disabled{opacity:.45;cursor:default}"
        ".sec{background:#232834;color:#e5e7eb;border-color:#323846;white-space:nowrap}"
        ".sec:disabled{opacity:.45;cursor:default}"
        ".actions{display:flex;gap:10px;margin-top:18px}"
        ".actions button{flex:1}"
        ".list{margin:-6px 0 14px;border:1px solid #2c3240;border-radius:8px;background:#11141a;"
        "max-height:180px;overflow-y:auto;display:none}"
        ".net{display:flex;justify-content:space-between;align-items:center;width:100%;"
        "padding:9px 12px;background:none;border:0;border-bottom:1px solid #1f2430;"
        "color:#e5e7eb;text-align:left;font-weight:400;border-radius:0}"
        ".net:last-child{border-bottom:0}"
        ".net:active,.net:hover{background:#1e2430}"
        ".sig{font-size:12px;color:#8b93a5}"
        ".note{margin-top:14px;padding:10px 12px;border-radius:8px;font-size:13px;"
        "line-height:1.4;display:none}"
        ".info{background:#1c2433;border:1px solid #2e3d59;color:#d6e2ff}"
        ".err{background:#2d1a1e;border:1px solid #5c2b33;color:#fecdd3}"
        "</style></head><body>"
        "<div class=\"card\">"
        "<div class=\"bar\"><div class=\"brand\"><img class=\"emblem\" alt=\"\" src=\"data:image/png;base64,"
        "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"
        "\"><img class=\"wm\" alt=\"Satoshiware\" src=\"data:image/png;base64,"
        "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"
        "yPQzos8IqgAAAABJRU5ErkJggg=="
        "\"></div><span class=\"ip\">192.168.4.1</span></div>"
        "<div class=\"body\">"
        "<h1>Satoshiware Wi-Fi</h1>"
        "<p class=\"sub\">Enter the Wi-Fi this miner should join.</p>"
        "<form id=\"f\" autocomplete=\"off\">"
        "<div class=\"field\"><label for=\"host\">Hostname</label>"
        "<input id=\"host\" name=\"hostname\" value=\"Satoshiware-Miner\" required maxlength=\"32\"></div>"
        "<div class=\"field\"><label for=\"ssid\">Wi-Fi SSID</label>"
        "<div class=\"row\"><input id=\"ssid\" name=\"ssid\" placeholder=\"Network name\" required maxlength=\"32\">"
        "<button type=\"button\" id=\"scan\" class=\"sec\">Scan</button></div></div>"
        "<div id=\"list\" class=\"list\"></div>"
        "<div class=\"field\"><label for=\"pass\">Wi-Fi Password</label>"
        "<div class=\"pw\"><input id=\"pass\" name=\"wifiPass\" type=\"password\" placeholder=\"Enter Wi-Fi password\" maxlength=\"63\">"
        "<button type=\"button\" id=\"eye\" class=\"eye\">Show</button></div></div>"
        "<div class=\"actions\">"
        "<button type=\"submit\" id=\"save\" class=\"pri\">Save</button>"
        "<button type=\"button\" id=\"rst\" class=\"sec\" disabled>Restart</button>"
        "</div></form>"
        "<div id=\"msg\" class=\"note\"></div>"
        "</div></div>"
        "<script>"
        "var f=document.getElementById('f'),host=document.getElementById('host'),"
        "ssid=document.getElementById('ssid'),pass=document.getElementById('pass'),"
        "save=document.getElementById('save'),rst=document.getElementById('rst'),"
        "scan=document.getElementById('scan'),list=document.getElementById('list'),"
        "eye=document.getElementById('eye'),msg=document.getElementById('msg');"
        "function show(t,k){msg.textContent=t;msg.className='note '+(k||'info');msg.style.display='block';}"
        "fetch('/api/system/info').then(function(r){return r.json();}).then(function(d){"
        "if(d&&d.hostname)host.value=d.hostname;if(d&&d.ssid)ssid.value=d.ssid;}).catch(function(){});"
        "eye.onclick=function(){var p=pass.type==='password';pass.type=p?'text':'password';eye.textContent=p?'Hide':'Show';};"
        "scan.onclick=function(){scan.disabled=true;scan.textContent='Scanning...';list.style.display='none';list.innerHTML='';"
        "fetch('/api/system/wifi/scan').then(function(r){return r.json();}).then(function(d){"
        "var a=(d&&d.networks)||[],seen={};a.sort(function(x,y){return y.rssi-x.rssi;});"
        "a.forEach(function(n){if(!n.ssid||seen[n.ssid])return;seen[n.ssid]=1;"
        "var b=document.createElement('button');b.type='button';b.className='net';"
        "var nm=document.createElement('span');nm.textContent=n.ssid;"
        "var sg=document.createElement('span');sg.className='sig';sg.textContent=(n.authmode===0?'Open \u00b7 ':'')+n.rssi+' dBm';"
        "b.appendChild(nm);b.appendChild(sg);"
        "b.onclick=function(){ssid.value=n.ssid;list.style.display='none';pass.focus();};"
        "list.appendChild(b);});"
        "if(list.children.length)list.style.display='block';else show('No nearby 2.4 GHz networks found.','info');"
        "}).catch(function(){show('Scan could not finish. You can type the Wi-Fi name above.','err');})"
        ".finally(function(){scan.disabled=false;scan.textContent='Scan';});};"
        "f.onsubmit=function(e){e.preventDefault();var s=ssid.value.trim(),h=host.value.trim()||'Satoshiware-Miner';"
        "if(!s){show('Enter the Wi-Fi name first.','err');return;}"
        "save.disabled=true;show('Saving on the miner...','info');"
        "fetch('/api/system',{method:'PATCH',headers:{'Content-Type':'application/json'},"
        "body:JSON.stringify({hostname:h,ssid:s,wifiPass:pass.value})}).then(function(r){"
        "if(!r.ok)throw 0;rst.disabled=false;"
        "show('Saved on the miner. Tap Restart to join '+s+'.','info');"
        "}).catch(function(){show('Could not save. Try again.','err');})"
        ".finally(function(){save.disabled=false;});};"
        "rst.onclick=function(){rst.disabled=true;save.disabled=true;"
        "show('Restarting. Rejoin your home Wi-Fi, then check the miner screen for its new IP address.','info');"
        "fetch('/api/system/restart',{method:'POST'}).catch(function(){});};"
        "</script></body></html>";

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, sizeof(page) - 1);
}

// Android shows the sign-in notice when this check is redirected.
static esp_err_t captive_portal_redirect(httpd_req_t * req)
{
    if (GLOBAL_STATE == NULL || !GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, "Redirect", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req,
        "<html><body><a href=\"http://192.168.4.1/\">Sign in</a></body></html>",
        HTTPD_RESP_USE_STRLEN);
}

static esp_err_t captive_api_handler(httpd_req_t * req)
{
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/captive+json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");
    return httpd_resp_send(req,
        "{\"captive\":true,\"user-portal-url\":\"http://192.168.4.1/\"}",
        HTTPD_RESP_USE_STRLEN);
}

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t * req, httpd_err_code_t err)
{
    if (GLOBAL_STATE != NULL && GLOBAL_STATE->SYSTEM_MODULE.ap_enabled) {
        return captive_portal_redirect(req);
    }

    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static bool is_setup_ap_peer(int fd)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *) &addr, &len) != 0) {
        return false;
    }

    uint32_t ip = 0;
    if (addr.ss_family == AF_INET) {
        ip = ntohl(((struct sockaddr_in *) &addr)->sin_addr.s_addr);
    } else if (addr.ss_family == AF_INET6) {
        const uint8_t *b = ((struct sockaddr_in6 *) &addr)->sin6_addr.s6_addr;
        if (b[10] != 0xff || b[11] != 0xff) {
            return false;
        }
        ip = ((uint32_t) b[12] << 24) | ((uint32_t) b[13] << 16) | ((uint32_t) b[14] << 8) | b[15];
    } else {
        return false;
    }

    return (ip & 0xFFFFFF00) == 0xC0A80400;
}

static void close_setup_ap_sessions(void * arg, esp_event_base_t base, int32_t id, void * data)
{
    if (server == NULL) {
        return;
    }

    int fds[CONFIG_LWIP_MAX_SOCKETS];
    size_t count = CONFIG_LWIP_MAX_SOCKETS;
    if (httpd_get_client_list(server, &count, fds) != ESP_OK) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (is_setup_ap_peer(fds[i])) {
            httpd_sess_trigger_close(server, fds[i]);
        }
    }
}

esp_err_t start_rest_server(void * pvParameters)
{
    GLOBAL_STATE = (GlobalState *) pvParameters;
    
    // Initialize the ASIC API with the global state
    asic_api_init(GLOBAL_STATE);
    const char * base_path = "";

    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t * rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;
    // Must stay below CONFIG_LWIP_MAX_ACTIVE_TCP minus the pool sockets, or
    // lwIP runs out of TCP slots before the LRU purge ever fires.
    config.max_open_sockets = 10;
    config.max_uri_handlers = 56;
    config.close_fn = websocket_close_fn;
    config.lru_purge_enable = true;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    config.send_wait_timeout = 3;

    ESP_LOGI(TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server, &config) == ESP_OK, "Start server failed", err_start);

    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, close_setup_ap_sessions, NULL);

    // Initialize the WebSocket registry with the valid server handle
    websocket_init(server);

    httpd_uri_t api_options_uri = {
        .uri = "/api/*", 
        .method = HTTP_OPTIONS, 
        .handler = handle_options_request, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_options_uri);

    httpd_uri_t recovery_explicit_get_uri = {
        .uri = "/recovery", 
        .method = HTTP_GET, 
        .handler = rest_recovery_handler, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &recovery_explicit_get_uri);
    
    // Register theme API endpoints
    ESP_ERROR_CHECK(register_theme_api_endpoints(server, rest_context));

    /* URI handler for fetching system info */
    httpd_uri_t system_info_get_uri = {
        .uri = "/api/system/info", 
        .method = HTTP_GET, 
        .handler = GET_system_info, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_info_get_uri);

    /* URI handler for fetching system asic values */
    httpd_uri_t system_asic_get_uri = {
        .uri = "/api/system/asic", 
        .method = HTTP_GET, 
        .handler = GET_system_asic, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_asic_get_uri);

    /* URI handler for fetching system statistic values */
    httpd_uri_t system_statistics_get_uri = {
        .uri = "/api/system/statistics", 
        .method = HTTP_GET, 
        .handler = GET_system_statistics, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_statistics_get_uri);

    httpd_uri_t scoreboard_get_uri = {
        .uri = "/api/system/scoreboard",
        .method = HTTP_GET,
        .handler = GET_scoreboard,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &scoreboard_get_uri);

    /* URI handler for WiFi scan */
    httpd_uri_t wifi_scan_get_uri = {
        .uri = "/api/system/wifi/scan",
        .method = HTTP_GET,
        .handler = GET_wifi_scan,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &wifi_scan_get_uri);

    /* URI handler for fetching system logs */
    httpd_uri_t system_logs_get_uri = {
        .uri = "/api/system/logs",
        .method = HTTP_GET,
        .handler = GET_system_logs,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_logs_get_uri);

    httpd_uri_t system_identify_uri = {
        .uri = "/api/system/identify", .method = HTTP_POST, 
        .handler = POST_identify, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_identify_uri);

    httpd_uri_t system_restart_uri = {
        .uri = "/api/system/restart", .method = HTTP_POST, 
        .handler = POST_restart, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_restart_uri);

    httpd_uri_t system_mining_pause_uri = {
        .uri = "/api/system/pause",
        .method = HTTP_POST,
        .handler = POST_mining_pause,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_mining_pause_uri);

    httpd_uri_t system_mining_resume_uri = {
        .uri = "/api/system/resume",
        .method = HTTP_POST,
        .handler = POST_mining_resume,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &system_mining_resume_uri);

    httpd_uri_t system_dismiss_block_found_uri = {
        .uri = "/api/system/blockFound/dismiss",
        .method = HTTP_POST, 
        .handler = POST_dismiss_block_found, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &system_dismiss_block_found_uri);

    httpd_uri_t update_system_settings_uri = {
        .uri = "/api/system", 
        .method = HTTP_PATCH, 
        .handler = PATCH_update_settings, 
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server, &update_system_settings_uri);

    httpd_uri_t update_post_ota_firmware = {
        .uri = "/api/system/OTA", 
        .method = HTTP_POST, 
        .handler = POST_OTA_update, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_post_ota_firmware);

    httpd_uri_t update_post_ota_www = {
        .uri = "/api/system/OTAWWW", 
        .method = HTTP_POST, 
        .handler = POST_WWW_update, 
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &update_post_ota_www);

    httpd_uri_t ws = {
        .uri = "/api/ws", 
        .method = HTTP_GET, 
        .handler = websocket_handler, 
        .user_ctx = (void *)WS_TYPE_LOGS, 
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &ws);

    httpd_uri_t ws_live = {
        .uri = "/api/ws/live", 
        .method = HTTP_GET, 
        .handler = websocket_handler, 
        .user_ctx = (void *)WS_TYPE_API, 
        .is_websocket = true
    };
    httpd_register_uri_handler(server, &ws_live);

    httpd_uri_t captive_api_get = {
        .uri = "/captive-api",
        .method = HTTP_GET,
        .handler = captive_api_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &captive_api_get);
    httpd_uri_t captive_api_head = {
        .uri = "/captive-api",
        .method = HTTP_HEAD,
        .handler = captive_api_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &captive_api_head);

    static const char *captive_uris[] = {
        "/hotspot-detect.html",
        "/library/test/success.html",
        "/generate_204",
        "/gen_204",
        "/connecttest.txt",
        "/ncsi.txt",
        "/success.txt",
        "/canonical.html",
        "/kindle-wifi/wifiredirect.html",
        "/fwlink",
    };
    for (size_t i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
        httpd_method_t methods[] = { HTTP_GET, HTTP_HEAD };
        for (size_t method = 0; method < sizeof(methods) / sizeof(methods[0]); method++) {
            httpd_uri_t captive_uri = {
                .uri = captive_uris[i],
                .method = methods[method],
                .handler = captive_portal_redirect,
                .user_ctx = NULL
            };
            httpd_register_uri_handler(server, &captive_uri);
        }
    }

    if (!GLOBAL_STATE->filesystem_is_available) {
        /* Make default route serve Recovery */
        httpd_uri_t recovery_implicit_get_uri = {
            .uri = "/*", .method = HTTP_GET, 
            .handler = rest_recovery_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &recovery_implicit_get_uri);
    } else {
        httpd_uri_t api_common_uri = {
            .uri = "/api/*",
            .method = HTTP_ANY,
            .handler = rest_api_common_handler,
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &api_common_uri);
        /* URI handler for getting web server files */
        httpd_uri_t common_get_uri = {
            .uri = "/*", 
            .method = HTTP_GET, 
            .handler = rest_common_get_handler, 
            .user_ctx = rest_context
        };
        httpd_register_uri_handler(server, &common_get_uri);
    }

    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);

    // Start websocket log handler thread
    TaskHandle_t ws_log_task_handle = NULL;
    if (xTaskCreateWithCaps(websocket_log_task, "ws_log_task", 8192, NULL, 2, &ws_log_task_handle, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Error creating websocket log task");
    }
    websocket_set_log_task_handle(ws_log_task_handle);

    // Start websocket API live data handler thread
    if (xTaskCreateWithCaps(websocket_api_task, "ws_api_task", 8192, GLOBAL_STATE, 2, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        ESP_LOGE(TAG, "Error creating ws api task");
    }

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    start_dns_server(&dns_config);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}
