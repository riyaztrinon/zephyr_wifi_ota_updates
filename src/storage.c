#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "storage.h"

LOG_MODULE_REGISTER(storage);

#define WIFI_CREDS_KEY "wifi/creds"

int storage_init(void)
{
    int ret = settings_subsys_init();
    if (ret) {
        LOG_ERR("Failed to initialize settings: %d", ret);
        return ret;
    }
    
    LOG_INF("Storage initialized");
    return 0;
}

int storage_save_wifi_credentials(const struct wifi_credentials *creds)
{
    if (!creds) {
        return -EINVAL;
    }
    
    int ret = settings_save_one(WIFI_CREDS_KEY, creds, sizeof(*creds));
    if (ret) {
        LOG_ERR("Failed to save WiFi credentials: %d", ret);
    } else {
        LOG_INF("WiFi credentials saved");
    }
    
    return ret;
}

int storage_load_wifi_credentials(struct wifi_credentials *creds)
{
    if (!creds) {
        return -EINVAL;
    }
    
    size_t len = sizeof(*creds);
    int ret = settings_load_one(WIFI_CREDS_KEY, creds, &len);
    if (ret) {
        LOG_DBG("No saved WiFi credentials found");
        memset(creds, 0, sizeof(*creds));
        return ret;
    }
    
    if (len != sizeof(*creds) || !creds->valid) {
        LOG_WRN("Invalid WiFi credentials in storage");
        memset(creds, 0, sizeof(*creds));
        return -EINVAL;
    }
    
    LOG_INF("WiFi credentials loaded");
    return 0;
}

int storage_clear_wifi_credentials(void)
{
    int ret = settings_delete(WIFI_CREDS_KEY);
    if (ret) {
        LOG_ERR("Failed to clear WiFi credentials: %d", ret);
    } else {
        LOG_INF("WiFi credentials cleared");
    }
    
    return ret;
}