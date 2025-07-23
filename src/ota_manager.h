#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stddef.h>

int ota_manager_init(void);
int ota_manager_start_update(void);
int ota_manager_write_data(const uint8_t *data, size_t len);
int ota_manager_finish_update(void);
int ota_manager_update_from_url(const char *url);
int ota_manager_get_status(char *buf, size_t buf_len);

#endif