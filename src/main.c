#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/drivers/gpio.h>

#include "wifi_manager.h"
#include "web_server.h"
#include "storage.h"

LOG_MODULE_REGISTER(main);

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("WiFi connected");
        gpio_pin_set_dt(&led, 1);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected");
        gpio_pin_set_dt(&led, 0);
        break;
    default:
        break;
    }
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char buf[NET_IPV4_ADDR_LEN];
        
        LOG_INF("IPv4 address added");
        
        if (net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[0].address.in_addr,
                         buf, sizeof(buf))) {
            LOG_INF("Address: %s", buf);
            
            // Start web server once we have IP
            web_server_start();
        }
    }
}

int main(void)
{
    int ret;
    
    LOG_INF("ESP32 WiFi Provisioning & OTA Update Demo");
    LOG_INF("Zephyr version: %s", KERNEL_VERSION_STRING);
    
    // Initialize LED
    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED device not ready");
        return -1;
    }
    
    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Failed to configure LED pin");
        return ret;
    }
    
    // Initialize storage
    ret = storage_init();
    if (ret) {
        LOG_ERR("Failed to initialize storage: %d", ret);
        return ret;
    }
    
    // Set up network event callbacks
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                NET_EVENT_WIFI_CONNECT_RESULT |
                                NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);
    
    net_mgmt_init_event_callback(&ipv4_cb, ipv4_mgmt_event_handler,
                                NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ipv4_cb);
    
    // Initialize WiFi manager
    ret = wifi_manager_init();
    if (ret) {
        LOG_ERR("Failed to initialize WiFi manager: %d", ret);
        return ret;
    }
    
    // Try to connect with saved credentials
    wifi_manager_connect_saved();
    
    LOG_INF("System initialized. Connect to AP mode or configure WiFi.");
    
    return 0;
}