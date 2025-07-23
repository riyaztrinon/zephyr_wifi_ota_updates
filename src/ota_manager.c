#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <string.h>

#include "ota_manager.h"

LOG_MODULE_REGISTER(ota_manager);

#define FLASH_AREA_IMAGE_SECONDARY FIXED_PARTITION_ID(slot1_partition)

static const struct flash_area *flash_area;
static size_t bytes_written = 0;
static bool update_in_progress = false;

int ota_manager_init(void)
{
    int ret = flash_area_open(FLASH_AREA_IMAGE_SECONDARY, &flash_area);
    if (ret) {
        LOG_ERR("Failed to open flash area: %d", ret);
        return ret;
    }
    
    LOG_INF("OTA manager initialized");
    return 0;
}

int ota_manager_start_update(void)
{
    if (update_in_progress) {
        LOG_WRN("Update already in progress");
        return -EBUSY;
    }
    
    if (!flash_area) {
        int ret = ota_manager_init();
        if (ret) {
            return ret;
        }
    }
    
    // Erase the secondary slot
    int ret = flash_area_erase(flash_area, 0, flash_area->fa_size);
    if (ret) {
        LOG_ERR("Failed to erase flash area: %d", ret);
        return ret;
    }
    
    bytes_written = 0;
    update_in_progress = true;
    
    LOG_INF("OTA update started");
    return 0;
}

int ota_manager_write_data(const uint8_t *data, size_t len)
{
    if (!update_in_progress) {
        LOG_ERR("No update in progress");
        return -EINVAL;
    }
    
    if (!flash_area) {
        LOG_ERR("Flash area not initialized");
        return -EINVAL;
    }
    
    if (bytes_written + len > flash_area->fa_size) {
        LOG_ERR("Data too large for flash area");
        return -ENOSPC;
    }
    
    int ret = flash_area_write(flash_area, bytes_written, data, len);
    if (ret) {
        LOG_ERR("Failed to write to flash: %d", ret);
        return ret;
    }
    
    bytes_written += len;
    
    if (bytes_written % 4096 == 0) {  // Log every 4KB
        LOG_INF("Written %zu bytes", bytes_written);
    }
    
    return 0;
}

int ota_manager_finish_update(void)
{
    if (!update_in_progress) {
        LOG_ERR("No update in progress");
        return -EINVAL;
    }
    
    update_in_progress = false;
    
    if (bytes_written == 0) {
        LOG_ERR("No data written");
        return -EINVAL;
    }
    
    // Mark the image for test (MCUboot will try it on next boot)
    int ret = boot_request_upgrade(BOOT_UPGRADE_TEST);
    if (ret) {
        LOG_ERR("Failed to request upgrade: %d", ret);
        return ret;
    }
    
    LOG_INF("OTA update finished, %zu bytes written", bytes_written);
    return 0;
}

int ota_manager_update_from_url(const char *url)
{
    // This is a simplified implementation
    // In a real implementation, you would:
    // 1. Parse the URL
    // 2. Create HTTP client
    // 3. Download the firmware
    // 4. Write it using the functions above
    
    LOG_INF("URL update not fully implemented: %s", url);
    return -ENOSYS;
}

int ota_manager_get_status(char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) {
        return -EINVAL;
    }
    
    if (update_in_progress) {
        snprintf(buf, buf_len, 
                "{\"status\":\"updating\",\"bytes_written\":%zu}", 
                bytes_written);
    } else {
        snprintf(buf, buf_len, "{\"status\":\"ready\"}");
    }
    
    return 0;
}