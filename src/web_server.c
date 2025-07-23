#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/server.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <string.h>
#include <stdio.h>

#include "web_server.h"
#include "wifi_manager.h"
#include "ota_manager.h"

LOG_MODULE_REGISTER(web_server);

#define HTTP_PORT 80
#define MAX_RECV_BUF_LEN 1024
#define MAX_SEND_BUF_LEN 2048

static struct http_resource_detail_static index_html_gz;
static struct http_resource_detail_static style_css_gz;
static struct http_resource_detail_static script_js_gz;

// HTML content embedded as string literals
static const char index_html[] = 
"<!DOCTYPE html>\n"
"<html>\n"
"<head>\n"
"    <title>ESP32 WiFi & OTA Manager</title>\n"
"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"    <link rel=\"stylesheet\" href=\"/style.css\">\n"
"</head>\n"
"<body>\n"
"    <div class=\"container\">\n"
"        <h1>ESP32 WiFi & OTA Manager</h1>\n"
"        \n"
"        <div class=\"section\">\n"
"            <h2>WiFi Configuration</h2>\n"
"            <div id=\"wifi-status\">Loading...</div>\n"
"            <form id=\"wifi-form\">\n"
"                <input type=\"text\" id=\"ssid\" placeholder=\"WiFi SSID\" required>\n"
"                <input type=\"password\" id=\"password\" placeholder=\"WiFi Password\">\n"
"                <button type=\"submit\">Connect</button>\n"
"            </form>\n"
"            <button id=\"scan-btn\">Scan Networks</button>\n"
"            <div id=\"networks-list\"></div>\n"
"        </div>\n"
"        \n"
"        <div class=\"section\">\n"
"            <h2>OTA Update</h2>\n"
"            <div id=\"ota-status\">Ready</div>\n"
"            <form id=\"ota-form\" enctype=\"multipart/form-data\">\n"
"                <input type=\"file\" id=\"firmware-file\" accept=\".bin,.hex\" required>\n"
"                <button type=\"submit\">Upload Firmware</button>\n"
"            </form>\n"
"            <div class=\"progress-container\">\n"
"                <div id=\"progress-bar\" class=\"progress-bar\"></div>\n"
"            </div>\n"
"            <div>Or update from URL:</div>\n"
"            <form id=\"url-form\">\n"
"                <input type=\"url\" id=\"firmware-url\" placeholder=\"https://example.com/firmware.bin\">\n"
"                <button type=\"submit\">Update from URL</button>\n"
"            </form>\n"
"        </div>\n"
"        \n"
"        <div class=\"section\">\n"
"            <h2>System Info</h2>\n"
"            <div id=\"system-info\">Loading...</div>\n"
"            <button id=\"reboot-btn\">Reboot</button>\n"
"        </div>\n"
"    </div>\n"
"    <script src=\"/script.js\"></script>\n"
"</body>\n"
"</html>";

static const char style_css[] = 
"body {\n"
"    font-family: Arial, sans-serif;\n"
"    margin: 0;\n"
"    padding: 20px;\n"
"    background-color: #f0f0f0;\n"
"}\n"
".container {\n"
"    max-width: 800px;\n"
"    margin: 0 auto;\n"
"    background-color: white;\n"
"    padding: 20px;\n"
"    border-radius: 8px;\n"
"    box-shadow: 0 2px 10px rgba(0,0,0,0.1);\n"
"}\n"
"h1 {\n"
"    color: #333;\n"
"    text-align: center;\n"
"    margin-bottom: 30px;\n"
"}\n"
".section {\n"
"    margin-bottom: 30px;\n"
"    padding: 20px;\n"
"    border: 1px solid #ddd;\n"
"    border-radius: 5px;\n"
"}\n"
"h2 {\n"
"    color: #555;\n"
"    margin-top: 0;\n"
"}\n"
"input, button {\n"
"    display: block;\n"
"    width: 100%;\n"
"    padding: 10px;\n"
"    margin: 10px 0;\n"
"    border: 1px solid #ddd;\n"
"    border-radius: 4px;\n"
"    box-sizing: border-box;\n"
"}\n"
"button {\n"
"    background-color: #4CAF50;\n"
"    color: white;\n"
"    border: none;\n"
"    cursor: pointer;\n"
"}\n"
"button:hover {\n"
"    background-color: #45a049;\n"
"}\n"
".progress-container {\n"
"    width: 100%;\n"
"    background-color: #f0f0f0;\n"
"    border-radius: 4px;\n"
"    margin: 10px 0;\n"
"}\n"
".progress-bar {\n"
"    width: 0%;\n"
"    height: 20px;\n"
"    background-color: #4CAF50;\n"
"    border-radius: 4px;\n"
"    transition: width 0.3s;\n"
"}\n"
"#networks-list {\n"
"    margin-top: 10px;\n"
"}\n"
".network-item {\n"
"    padding: 8px;\n"
"    margin: 5px 0;\n"
"    background-color: #f9f9f9;\n"
"    border-radius: 3px;\n"
"    cursor: pointer;\n"
"}\n"
".network-item:hover {\n"
"    background-color: #e0e0e0;\n"
"}\n"
"#system-info {\n"
"    background-color: #f9f9f9;\n"
"    padding: 10px;\n"
"    border-radius: 3px;\n"
"    font-family: monospace;\n"
"    white-space: pre-line;\n"
"}";

static const char script_js[] = 
"document.addEventListener('DOMContentLoaded', function() {\n"
"    loadWiFiStatus();\n"
"    loadSystemInfo();\n"
"    \n"
"    document.getElementById('wifi-form').addEventListener('submit', connectWiFi);\n"
"    document.getElementById('scan-btn').addEventListener('click', scanNetworks);\n"
"    document.getElementById('ota-form').addEventListener('submit', uploadFirmware);\n"
"    document.getElementById('url-form').addEventListener('submit', updateFromURL);\n"
"    document.getElementById('reboot-btn').addEventListener('click', rebootDevice);\n"
"});\n"
"\n"
"function loadWiFiStatus() {\n"
"    fetch('/api/wifi/status')\n"
"        .then(response => response.json())\n"
"        .then(data => {\n"
"            document.getElementById('wifi-status').textContent = \n"  
"                'Status: ' + data.status + (data.ssid ? ' (' + data.ssid + ')' : '');\n"
"        })\n"
"        .catch(error => {\n"
"            document.getElementById('wifi-status').textContent = 'Error loading status';\n"
"        });\n"
"}\n"
"\n"
"function connectWiFi(event) {\n"
"    event.preventDefault();\n"
"    const ssid = document.getElementById('ssid').value;\n"
"    const password = document.getElementById('password').value;\n"
"    \n"
"    fetch('/api/wifi/connect', {\n"
"        method: 'POST',\n"
"        headers: { 'Content-Type': 'application/json' },\n"
"        body: JSON.stringify({ ssid: ssid, password: password })\n"
"    })\n"
"    .then(response => response.json())\n"
"    .then(data => {\n"
"        alert(data.message || 'Connection initiated');\n"
"        setTimeout(loadWiFiStatus, 3000);\n"
"    });\n"
"}\n"
"\n"
"function scanNetworks() {\n"
"    document.getElementById('networks-list').innerHTML = 'Scanning...';\n"
"    \n"
"    fetch('/api/wifi/scan')\n"
"        .then(response => response.json())\n"
"        .then(data => {\n"
"            const list = document.getElementById('networks-list');\n"
"            list.innerHTML = '';\n"
"            \n"
"            if (data.networks && data.networks.length > 0) {\n"
"                data.networks.forEach(network => {\n"
"                    const item = document.createElement('div');\n"
"                    item.className = 'network-item';\n"
"                    item.textContent = network.ssid + ' (' + network.rssi + ' dBm)';\n"
"                    item.onclick = () => {\n"
"                        document.getElementById('ssid').value = network.ssid;\n"
"                    };\n"
"                    list.appendChild(item);\n"
"                });\n"
"            } else {\n"
"                list.innerHTML = 'No networks found';\n"
"            }\n"
"        });\n"
"}\n"
"\n"
"function uploadFirmware(event) {\n"
"    event.preventDefault();\n"
"    const fileInput = document.getElementById('firmware-file');\n"
"    const file = fileInput.files[0];\n"
"    \n"
"    if (!file) {\n"
"        alert('Please select a firmware file');\n"
"        return;\n"
"    }\n"
"    \n"
"    const formData = new FormData();\n"
"    formData.append('firmware', file);\n"
"    \n"
"    const progressBar = document.getElementById('progress-bar');\n"
"    const statusDiv = document.getElementById('ota-status');\n"
"    \n"
"    statusDiv.textContent = 'Uploading...';\n"
"    progressBar.style.width = '0%';\n"
"    \n"
"    const xhr = new XMLHttpRequest();\n"
"    \n"
"    xhr.upload.addEventListener('progress', function(e) {\n"
"        if (e.lengthComputable) {\n"
"            const percentComplete = (e.loaded / e.total) * 100;\n"
"            progressBar.style.width = percentComplete + '%';\n"
"        }\n"
"    });\n"
"    \n"
"    xhr.addEventListener('load', function() {\n"
"        if (xhr.status === 200) {\n"
"            statusDiv.textContent = 'Upload successful! Rebooting...';\n"
"            progressBar.style.width = '100%';\n"
"            setTimeout(() => {\n"
"                location.reload();\n"
"            }, 3000);\n"
"        } else {\n"
"            statusDiv.textContent = 'Upload failed';\n"
"            progressBar.style.width = '0%';\n"
"        }\n"
"    });\n"
"    \n"
"    xhr.addEventListener('error', function() {\n"
"        statusDiv.textContent = 'Upload error';\n"
"        progressBar.style.width = '0%';\n"
"    });\n"
"    \n"
"    xhr.open('POST', '/api/ota/upload');\n"
"    xhr.send(formData);\n"
"}\n"
"\n"
"function updateFromURL(event) {\n"
"    event.preventDefault();\n"
"    const url = document.getElementById('firmware-url').value;\n"
"    \n"
"    if (!url) {\n"
"        alert('Please enter a firmware URL');\n"
"        return;\n"
"    }\n"
"    \n"
"    const statusDiv = document.getElementById('ota-status');\n"
"    statusDiv.textContent = 'Downloading firmware...';\n"
"    \n"
"    fetch('/api/ota/url', {\n"
"        method: 'POST',\n"
"        headers: { 'Content-Type': 'application/json' },\n"
"        body: JSON.stringify({ url: url })\n"
"    })\n"
"    .then(response => response.json())\n"
"    .then(data => {\n"
"        if (data.success) {\n"
"            statusDiv.textContent = 'Update successful! Rebooting...';\n"
"            setTimeout(() => {\n"
"                location.reload();\n"
"            }, 3000);\n"
"        } else {\n"
"            statusDiv.textContent = 'Update failed: ' + (data.error || 'Unknown error');\n"
"        }\n"
"    })\n"
"    .catch(error => {\n"
"        statusDiv.textContent = 'Update error';\n"
"    });\n"
"}\n"
"\n"
"function loadSystemInfo() {\n"
"    fetch('/api/system/info')\n"
"        .then(response => response.json())\n"
"        .then(data => {\n"
"            let info = 'Firmware Version: ' + (data.version || 'Unknown') + '\\n';\n"
"            info += 'Build Date: ' + (data.build_date || 'Unknown') + '\\n';\n"
"            info += 'Free Memory: ' + (data.free_memory || 'Unknown') + ' bytes\\n';\n"
"            info += 'Uptime: ' + (data.uptime || 'Unknown') + ' seconds';\n"
"            document.getElementById('system-info').textContent = info;\n"
"        })\n"
"        .catch(error => {\n"
"            document.getElementById('system-info').textContent = 'Error loading system info';\n"
"        });\n"
"}\n"
"\n"
"function rebootDevice() {\n"
"    if (confirm('Are you sure you want to reboot the device?')) {\n"
"        fetch('/api/system/reboot', { method: 'POST' })\n"
"            .then(() => {\n"
"                alert('Device is rebooting...');\n"
"                setTimeout(() => {\n"
"                    location.reload();\n"
"                }, 5000);\n"
"            });\n"
"    }\n"
"}";

// HTTP server instance
static struct http_service_ctx http_ctx;

// API handlers
static int api_wifi_status_handler(struct http_client_ctx *client,
                                   enum http_data_status status,
                                   uint8_t *data, size_t len,
                                   struct http_response_ctx *response_ctx)
{
    char response_buf[256];
    
    wifi_manager_get_status(response_buf, sizeof(response_buf));
    
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    
    http_response_send(client, response_ctx, response_buf, strlen(response_buf));
    return 0;
}

static int api_wifi_connect_handler(struct http_client_ctx *client,
                                    enum http_data_status status,
                                    uint8_t *data, size_t len,
                                    struct http_response_ctx *response_ctx)
{
    static char json_buf[512];
    static size_t json_len = 0;
    
    if (status == HTTP_SERVER_DATA_RECV_MORE) {
        if (json_len + len < sizeof(json_buf)) {
            memcpy(json_buf + json_len, data, len);
            json_len += len;
        }
        return 0;
    }
    
    if (status == HTTP_SERVER_DATA_RECV_FINAL) {
        json_buf[json_len] = '\0';
        
        // Simple JSON parsing for SSID and password
        char *ssid_start = strstr(json_buf, "\"ssid\":\"");
        char *pass_start = strstr(json_buf, "\"password\":\"");
        
        if (ssid_start) {
            ssid_start += 8; // Skip "ssid":"
            char *ssid_end = strchr(ssid_start, '"');
            if (ssid_end) {
                *ssid_end = '\0';
                
                char *password = NULL;
                if (pass_start) {
                    pass_start += 12; // Skip "password":"
                    char *pass_end = strchr(pass_start, '"');
                    if (pass_end) {
                        *pass_end = '\0';
                        password = pass_start;
                    }
                }
                
                int ret = wifi_manager_connect(ssid_start, password);
                
                const char *response = ret == 0 ? 
                    "{\"success\":true,\"message\":\"Connection initiated\"}" :
                    "{\"success\":false,\"message\":\"Connection failed\"}";
                
                http_response_header_add(response_ctx,
                                        HTTP_HEADER_CONTENT_TYPE,
                                        "application/json");
                
                http_response_send(client, response_ctx, response, strlen(response));
            }
        }
        
        json_len = 0;  // Reset for next request
    }
    
    return 0;
}

static int api_wifi_scan_handler(struct http_client_ctx *client,
                                 enum http_data_status status,
                                 uint8_t *data, size_t len,
                                 struct http_response_ctx *response_ctx)
{
    wifi_manager_scan();
    
    // For simplicity, return mock scan results
    const char *response = "{\"networks\":["
                          "{\"ssid\":\"Network1\",\"rssi\":-45},"
                          "{\"ssid\":\"Network2\",\"rssi\":-67}"
                          "]}";
    
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    
    http_response_send(client, response_ctx, response, strlen(response));
    return 0;
}

static int api_ota_upload_handler(struct http_client_ctx *client,
                                  enum http_data_status status,
                                  uint8_t *data, size_t len,
                                  struct http_response_ctx *response_ctx)
{
    static bool upload_started = false;
    
    if (status == HTTP_SERVER_DATA_RECV_MORE) {
        if (!upload_started) {
            ota_manager_start_update();
            upload_started = true;
        }
        
        ota_manager_write_data(data, len);
        return 0;
    }
    
    if (status == HTTP_SERVER_DATA_RECV_FINAL) {
        int ret = ota_manager_finish_update();
        upload_started = false;
        
        const char *response = ret == 0 ?
            "{\"success\":true,\"message\":\"Update successful\"}" :
            "{\"success\":false,\"message\":\"Update failed\"}";
        
        http_response_header_add(response_ctx,
                                HTTP_HEADER_CONTENT_TYPE,
                                "application/json");
        
        http_response_send(client, response_ctx, response, strlen(response));
        
        if (ret == 0) {
            // Reboot after successful update
            k_sleep(K_MSEC(1000));
            sys_reboot(SYS_REBOOT_WARM);
        }
    }
    
    return 0;
}

static int api_ota_url_handler(struct http_client_ctx *client,
                               enum http_data_status status,
                               uint8_t *data, size_t len,
                               struct http_response_ctx *response_ctx)
{
    static char json_buf[512];
    static size_t json_len = 0;
    
    if (status == HTTP_SERVER_DATA_RECV_MORE) {
        if (json_len + len < sizeof(json_buf)) {
            memcpy(json_buf + json_len, data, len);
            json_len += len;
        }
        return 0;
    }
    
    if (status == HTTP_SERVER_DATA_RECV_FINAL) {
        json_buf[json_len] = '\0';
        
        char *url_start = strstr(json_buf, "\"url\":\"");
        if (url_start) {
            url_start += 7; // Skip "url":"
            char *url_end = strchr(url_start, '"');
            if (url_end) {
                *url_end = '\0';
                
                int ret = ota_manager_update_from_url(url_start);
                
                const char *response = ret == 0 ?
                    "{\"success\":true,\"message\":\"Update successful\"}" :
                    "{\"success\":false,\"message\":\"Update failed\"}";
                
                http_response_header_add(response_ctx,
                                        HTTP_HEADER_CONTENT_TYPE,
                                        "application/json");
                
                http_response_send(client, response_ctx, response, strlen(response));
                
                if (ret == 0) {
                    k_sleep(K_MSEC(1000));
                    sys_reboot(SYS_REBOOT_WARM);
                }
            }
        }
        
        json_len = 0;
    }
    
    return 0;
}

static int api_system_info_handler(struct http_client_ctx *client,
                                   enum http_data_status status,
                                   uint8_t *data, size_t len,
                                   struct http_response_ctx *response_ctx)
{
    char response_buf[512];
    uint32_t uptime = k_uptime_get() / 1000;
    
    snprintf(response_buf, sizeof(response_buf),
             "{"
             "\"version\":\"1.0.0\","
             "\"build_date\":\"%s %s\","
             "\"free_memory\":%zu,"
             "\"uptime\":%u"
             "}",
             __DATE__, __TIME__,
             k_heap_free_get(&k_malloc_async),
             uptime);
    
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    
    http_response_send(client, response_ctx, response_buf, strlen(response_buf));
    return 0;
}

static int api_system_reboot_handler(struct http_client_ctx *client,
                                     enum http_data_status status,
                                     uint8_t *data, size_t len,
                                     struct http_response_ctx *response_ctx)
{
    const char *response = "{\"success\":true,\"message\":\"Rebooting...\"}";
    
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "application/json");
    
    http_response_send(client, response_ctx, response, strlen(response));
    
    // Reboot after a short delay
    k_sleep(K_MSEC(1000));
    sys_reboot(SYS_REBOOT_WARM);
    
    return 0;
}

// Static file handlers
static int index_html_handler(struct http_client_ctx *client,
                             enum http_data_status status,
                             uint8_t *data, size_t len,
                             struct http_response_ctx *response_ctx)
{
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "text/html");
    
    http_response_send(client, response_ctx, index_html, strlen(index_html));
    return 0;
}

static int style_css_handler(struct http_client_ctx *client,
                            enum http_data_status status,
                            uint8_t *data, size_t len,
                            struct http_response_ctx *response_ctx)
{
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "text/css");
    
    http_response_send(client, response_ctx, style_css, strlen(style_css));
    return 0;
}

static int script_js_handler(struct http_client_ctx *client,
                            enum http_data_status status,
                            uint8_t *data, size_t len,
                            struct http_response_ctx *response_ctx)
{
    http_response_header_add(response_ctx,
                            HTTP_HEADER_CONTENT_TYPE,
                            "application/javascript");
    
    http_response_send(client, response_ctx, script_js, strlen(script_js));
    return 0;
}

// HTTP resources
HTTP_RESOURCE_DEFINE(index_resource, NULL, "/", NULL,
                     index_html_handler);

HTTP_RESOURCE_DEFINE(style_resource, NULL, "/style.css", NULL,
                     style_css_handler);

HTTP_RESOURCE_DEFINE(script_resource, NULL, "/script.js", NULL,
                     script_js_handler);

// API resources
HTTP_RESOURCE_DEFINE(api_wifi_status_resource, NULL, "/api/wifi/status", NULL,
                     api_wifi_status_handler);

HTTP_RESOURCE_DEFINE(api_wifi_connect_resource, NULL, "/api/wifi/connect", NULL,
                     api_wifi_connect_handler);

HTTP_RESOURCE_DEFINE(api_wifi_scan_resource, NULL, "/api/wifi/scan", NULL,
                     api_wifi_scan_handler);

HTTP_RESOURCE_DEFINE(api_ota_upload_resource, NULL, "/api/ota/upload", NULL,
                     api_ota_upload_handler);

HTTP_RESOURCE_DEFINE(api_ota_url_resource, NULL, "/api/ota/url", NULL,
                     api_ota_url_handler);

HTTP_RESOURCE_DEFINE(api_system_info_resource, NULL, "/api/system/info", NULL,
                     api_system_info_handler);

HTTP_RESOURCE_DEFINE(api_system_reboot_resource, NULL, "/api/system/reboot", NULL,
                     api_system_reboot_handler);

int web_server_start(void)
{
    int ret;
    
    struct sockaddr_in addr;
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    ret = http_server_start(&http_ctx, &addr, NULL, NULL);
    if (ret < 0) {
        LOG_ERR("Failed to start HTTP server: %d", ret);
        return ret;
    }
    
    LOG_INF("HTTP server started on port %d", HTTP_PORT);
    return 0;
}

int web_server_stop(void)
{
    return http_server_stop(&http_ctx);
}