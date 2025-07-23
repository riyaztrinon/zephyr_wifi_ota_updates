#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <zephyr/net/wifi_mgmt.h>

#define WIFI_SSID_MAX_LEN 32
#define WIFI_PSK_MAX_LEN 64

struct wifi_credentials {
    char ssid[WIFI_SSID_MAX_LEN + 1];
    char psk[WIFI_PSK_MAX_LEN + 1];
    bool valid;
};

int wifi_manager_init(void);
int wifi_manager_scan(void);
int wifi_manager_connect(const char *ssid, const char *psk);
int wifi_manager_connect_saved(void);
int wifi_manager_disconnect(void);
int wifi_manager_start_ap(void);
int wifi_manager_stop_ap(void);
bool wifi_manager_is_connected(void);
int wifi_manager_get_status(char *buf, size_t buf_len);

#endif