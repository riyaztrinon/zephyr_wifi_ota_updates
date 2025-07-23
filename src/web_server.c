#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/service.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include <stdio.h>

#include "web_server.h"
#include "wifi_manager.h"
#include "ota_manager.h"

LOG_MODULE_REGISTER(web_server);

#define HTTP_PORT 80

// Simple HTML content
static const char index_html[] = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>ESP32 WiFi & OTA Manager</title>\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <style>\n"
"        body { font-family: Arial, sans-serif; margin: 20px; }\n"
"        .section { margin: 20px 0; padding: 20px; border: 1px solid #ddd; }\n"
"        input, button { padding: 10px; margin: 5px; }\n"
"        button { background: #4CAF50; color: white; border: none; cursor: pointer; }\n"
"    </style>\n"
"</head>\n"
"<body>\n"
"    <h1>ESP32 WiFi & OTA Manager</h1>\n"
"    <div class=\"section\">\n"
"        <h2>WiFi Configuration</h2>\n"
"        <div id=\"wifi-status\">Loading...</div>\n"
"        <form id=\"wifi-form\">\n"
"            <input type=\"text\" id=\"ssid\" placeholder=\"WiFi SSID\" required>\n"
"            <input type=\"password\" id=\"password\" placeholder=\"WiFi Password\">\n"
"            <button type=\"submit\">Connect</button>\n"
"        </form>\n"
"    </div>\n"
"    <div class=\"section\">\n"
"        <h2>System Info</h2>\n"
"        <div id=\"system-info\">Loading...</div>\n"
"        <button onclick=\"rebootDevice()\">Reboot</button>\n"
"    </div>\n"
"    <script>\n"
"        function loadSystemInfo() {\n"
"            fetch('/api/system/info')\n"
"                .then(response => response.json())\n"
"                .then(data => {\n"
"                    document.getElementById('system-info').innerHTML = \n"
"                        'Version: ' + data.version + '<br>' +\n"
"                        'Free Memory: ' + data.free_memory + ' bytes<br>' +\n"
"                        'Uptime: ' + data.uptime + ' seconds';\n"
"                });\n"
"        }\n"
"        function rebootDevice() {\n"
"            if (confirm('Reboot?')) {\n"
"                fetch('/api/system/reboot', { method: 'POST' });\n"
"            }\n"
"        }\n"
"        loadSystemInfo();\n"
"    </script>\n"
"</body>\n"
"</html>";

// Handler for index page
static int index_handler(struct http_client_ctx *client, enum http_data_status status,
                        const struct http_request_ctx *request_ctx,
                        struct http_response_ctx *response_ctx, void *user_data)
{
    if (status == HTTP_SERVER_DATA_FINAL) {
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "text/html"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = index_html;
        response_ctx->body_len = strlen(index_html);
        response_ctx->final_chunk = true;
    }
    return 0;
}

// Handler for system info API
static int api_system_info_handler(struct http_client_ctx *client, enum http_data_status status,
                                   const struct http_request_ctx *request_ctx,
                                   struct http_response_ctx *response_ctx, void *user_data)
{
    if (status == HTTP_SERVER_DATA_FINAL) {
        static char response_buf[512];
        uint32_t uptime = k_uptime_get() / 1000;
        
        snprintf(response_buf, sizeof(response_buf),
                 "{"
                 "\"version\":\"1.0.0\","
                 "\"build_date\":\"%s %s\","
                 "\"free_memory\":%zu,"
                 "\"uptime\":%u"
                 "}",
                 __DATE__, __TIME__,
                 k_mem_free_get(),
                 uptime);
        
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "application/json"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = response_buf;
        response_ctx->body_len = strlen(response_buf);
        response_ctx->final_chunk = true;
    }
    return 0;
}

// Handler for system reboot API
static int api_system_reboot_handler(struct http_client_ctx *client, enum http_data_status status,
                                     const struct http_request_ctx *request_ctx,
                                     struct http_response_ctx *response_ctx, void *user_data)
{
    if (status == HTTP_SERVER_DATA_FINAL) {
        const char *response = "{\"success\":true,\"message\":\"Rebooting...\"}";
        
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "application/json"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = response;
        response_ctx->body_len = strlen(response);
        response_ctx->final_chunk = true;
        
        // Reboot after a short delay
        k_sleep(K_MSEC(1000));
        sys_reboot(SYS_REBOOT_WARM);
    }
    return 0;
}

// Handler for WiFi status API
static int api_wifi_status_handler(struct http_client_ctx *client, enum http_data_status status,
                                   const struct http_request_ctx *request_ctx,
                                   struct http_response_ctx *response_ctx, void *user_data)
{
    if (status == HTTP_SERVER_DATA_FINAL) {
        static char response_buf[256];
        
        wifi_manager_get_status(response_buf, sizeof(response_buf));
        
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "application/json"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = response_buf;
        response_ctx->body_len = strlen(response_buf);
        response_ctx->final_chunk = true;
    }
    return 0;
}

// Handler for WiFi connect API
static int api_wifi_connect_handler(struct http_client_ctx *client, enum http_data_status status,
                                    const struct http_request_ctx *request_ctx,
                                    struct http_response_ctx *response_ctx, void *user_data)
{
    static char request_buffer[512];
    static size_t total_received = 0;
    
    if (status == HTTP_SERVER_DATA_MORE) {
        // Accumulate POST data
        if (total_received + request_ctx->data_len < sizeof(request_buffer)) {
            memcpy(request_buffer + total_received, request_ctx->data, request_ctx->data_len);
            total_received += request_ctx->data_len;
        }
        return 0;
    }
    
    if (status == HTTP_SERVER_DATA_FINAL) {
        // Complete request received
        if (total_received + request_ctx->data_len < sizeof(request_buffer)) {
            memcpy(request_buffer + total_received, request_ctx->data, request_ctx->data_len);
            total_received += request_ctx->data_len;
            request_buffer[total_received] = '\0';
        }
        
        // Process WiFi connection request (simple JSON parsing)
        static char response_buf[256];
        char ssid[64] = {0};
        char password[64] = {0};
        
        // Simple JSON parsing for SSID and password
        char *ssid_start = strstr(request_buffer, "\"ssid\":\"");
        char *password_start = strstr(request_buffer, "\"password\":\"");
        
        if (ssid_start) {
            ssid_start += 8; // Move past "ssid":"
            char *ssid_end = strchr(ssid_start, '"');
            if (ssid_end && (ssid_end - ssid_start) < sizeof(ssid) - 1) {
                strncpy(ssid, ssid_start, ssid_end - ssid_start);
            }
        }
        
        if (password_start) {
            password_start += 12; // Move past "password":"
            char *password_end = strchr(password_start, '"');
            if (password_end && (password_end - password_start) < sizeof(password) - 1) {
                strncpy(password, password_start, password_end - password_start);
            }
        }
        
        int ret = wifi_manager_connect(ssid, password);
        
        if (ret == 0) {
            snprintf(response_buf, sizeof(response_buf),
                     "{\"success\":true,\"message\":\"Connecting to WiFi...\"}");
        } else {
            snprintf(response_buf, sizeof(response_buf),
                     "{\"success\":false,\"message\":\"Failed to connect\"}");
        }
        
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "application/json"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = response_buf;
        response_ctx->body_len = strlen(response_buf);
        response_ctx->final_chunk = true;
        
        total_received = 0; // Reset for next request
    }
    
    return 0;
}

// Handler for WiFi scan API
static int api_wifi_scan_handler(struct http_client_ctx *client, enum http_data_status status,
                                 const struct http_request_ctx *request_ctx,
                                 struct http_response_ctx *response_ctx, void *user_data)
{
    if (status == HTTP_SERVER_DATA_FINAL) {
        static char response_buf[1024];
        
        // Trigger WiFi scan and return simple response
        int ret = wifi_manager_scan();
        if (ret == 0) {
            snprintf(response_buf, sizeof(response_buf),
                     "{\"success\":true,\"message\":\"WiFi scan started\"}");
        } else {
            snprintf(response_buf, sizeof(response_buf),
                     "{\"success\":false,\"message\":\"Failed to start scan\"}");
        }
        
        response_ctx->status = 200;
        response_ctx->headers = (struct http_header[]){
            {"Content-Type", "application/json"}
        };
        response_ctx->header_count = 1;
        response_ctx->body = response_buf;
        response_ctx->body_len = strlen(response_buf);
        response_ctx->final_chunk = true;
    }
    return 0;
}

// Resource definitions
static struct http_resource_detail_dynamic index_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = index_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_system_info_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = api_system_info_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_system_reboot_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = api_system_reboot_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_wifi_status_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = api_wifi_status_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_wifi_connect_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_POST),
    },
    .cb = api_wifi_connect_handler,
    .user_data = NULL,
};

static struct http_resource_detail_dynamic api_wifi_scan_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
    },
    .cb = api_wifi_scan_handler,
    .user_data = NULL,
};

// HTTP resources - defined in a special section
HTTP_RESOURCE_DEFINE(index_resource, my_service, "/", &index_resource_detail);
HTTP_RESOURCE_DEFINE(api_system_info_resource, my_service, "/api/system/info", &api_system_info_resource_detail);
HTTP_RESOURCE_DEFINE(api_system_reboot_resource, my_service, "/api/system/reboot", &api_system_reboot_resource_detail);
HTTP_RESOURCE_DEFINE(api_wifi_status_resource, my_service, "/api/wifi/status", &api_wifi_status_resource_detail);
HTTP_RESOURCE_DEFINE(api_wifi_connect_resource, my_service, "/api/wifi/connect", &api_wifi_connect_resource_detail);
HTTP_RESOURCE_DEFINE(api_wifi_scan_resource, my_service, "/api/wifi/scan", &api_wifi_scan_resource_detail);

// HTTP service
static uint16_t http_service_port = HTTP_PORT;
HTTP_SERVICE_DEFINE(my_service, "0.0.0.0", &http_service_port, 1, 10, NULL);

int web_server_start(void)
{
    // In Zephyr 4.2.0, HTTP services start automatically when defined
    // No explicit start function is needed
    LOG_INF("HTTP server configured on port %d", HTTP_PORT);
    return 0;
}

int web_server_stop(void)
{
    // In Zephyr 4.2.0, HTTP services cannot be stopped dynamically
    // They are controlled by the system
    LOG_INF("HTTP server stop requested (not supported in Zephyr 4.2.0)");
    return 0;
}