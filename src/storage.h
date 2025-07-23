#ifndef STORAGE_H
#define STORAGE_H

#include "wifi_manager.h"

int storage_init(void);
int storage_save_wifi_credentials(const struct wifi_credentials *creds);
int storage_load_wifi_credentials(struct wifi_credentials *creds);
int storage_clear_wifi_credentials(void);

#endif