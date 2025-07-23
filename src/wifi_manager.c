#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "wifi_manager.h"
#include "storage.h"

LOG_MODULE_REGISTER(wifi_manager);

static struct net_if *wifi_iface;
static bool connected = false;
static bool ap_mode = false;

int wifi_manager_init(void)
{
    wifi_iface = net_if_get_default();
    if (!wifi_iface) {
        LOG_ERR("No default network interface");
        return -ENODEV;
    }
    
    LOG_INF("WiFi manager initialized");
    return 0;
}

int wifi_manager_scan(void)
{
    if (!wifi_iface) {
        return -ENODEV;
    }
    
    return net_mgmt(NET_REQUEST_WIFI_SCAN, wifi_iface, NULL, 0);
}

int wifi_manager_connect(const char *ssid, const char *psk)
{
    struct wifi_connect_req_params params = {0};
    
    if (!wifi_iface || !ssid) {
        return -EINVAL;
    }
    
    params.ssid = (uint8_t *)ssid;
    params.ssid_length = strlen(ssid);
    params.security = WIFI_SECURITY_TYPE_PSK;
    
    if (psk && strlen(psk) > 0) {
        params.psk = (uint8_t *)psk;
        params.psk_length = strlen(psk);
    } else {
        params.security = WIFI_SECURITY_TYPE_NONE;
    }
    
    params.channel = WIFI_CHANNEL_ANY;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    params.mfp = WIFI_MFP_OPTIONAL;
    
    LOG_INF("Connecting to WiFi SSID: %s", ssid);
    
    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, wifi_iface, &params, sizeof(params));
    if (ret == 0) {
        // Save credentials
        struct wifi_credentials creds = {0};
        strncpy(creds.ssid, ssid, sizeof(creds.ssid) - 1);
        if (psk) {
            strncpy(creds.psk, psk, sizeof(creds.psk) - 1);
        }
        creds.valid = true;
        storage_save_wifi_credentials(&creds);
    }
    
    return ret;
}

int wifi_manager_connect_saved(void)
{
    struct wifi_credentials creds;
    
    if (storage_load_wifi_credentials(&creds) == 0 && creds.valid) {
        LOG_INF("Connecting with saved credentials");
        return wifi_manager_connect(creds.ssid, creds.psk);
    }
    
    LOG_INF("No saved credentials, starting AP mode");
    return wifi_manager_start_ap();
}

int wifi_manager_disconnect(void)
{
    if (!wifi_iface) {
        return -ENODEV;
    }
    
    connected = false;
    return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, wifi_iface, NULL, 0);
}

int wifi_manager_start_ap(void)
{
    struct wifi_connect_req_params params = {0};
    const char *ap_ssid = "ESP32-Config";
    const char *ap_psk = "12345678";
    
    if (!wifi_iface) {
        return -ENODEV;
    }
    
    params.ssid = (uint8_t *)ap_ssid;
    params.ssid_length = strlen(ap_ssid);
    params.psk = (uint8_t *)ap_psk;
    params.psk_length = strlen(ap_psk);
    params.security = WIFI_SECURITY_TYPE_PSK;
    params.channel = 6;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    
    LOG_INF("Starting AP mode: %s", ap_ssid);
    
    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, wifi_iface, &params, sizeof(params));
    if (ret == 0) {
        ap_mode = true;
    }
    
    return ret;
}

int wifi_manager_stop_ap(void)
{
    if (!wifi_iface) {
        return -ENODEV;
    }
    
    ap_mode = false;
    return net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, wifi_iface, NULL, 0);
}

bool wifi_manager_is_connected(void)
{
    return connected && !ap_mode;
}

int wifi_manager_get_status(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    
    if (ap_mode) {
        snprintf(buf, buf_len, "{\"status\":\"ap_mode\",\"ssid\":\"ESP32-Config\"}");
    } else if (connected) {
        snprintf(buf, buf_len, "{\"status\":\"connected\"}");
    } else {
        snprintf(buf, buf_len, "{\"status\":\"disconnected\"}");
    }
    
    return 0;
}