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

// Check if LED is defined in devicetree, if not define a dummy
#if DT_NODE_EXISTS(DT_ALIAS(led0))
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
#define LED_AVAILABLE 1
#else
#define LED_AVAILABLE 0
#endif

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        LOG_INF("WiFi connected");
#if LED_AVAILABLE
        if (device_is_ready(led.port)) {
            gpio_pin_set_dt(&led, 1);
        }
#endif
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_INF("WiFi disconnected");
#if LED_AVAILABLE
        if (device_is_ready(led.port)) {
            gpio_pin_set_dt(&led, 0);
        }
#endif
        break;
    default:
        break;
    }
}

static void ipv4_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char buf[NET_IPV4_ADDR_LEN];
        
        LOG_INF("IPv4 address added");
        
        // Get the first IPv4 address from the interface
        struct net_if_ipv4 *ipv4 = iface->config.ip.ipv4;
        if (ipv4 && ipv4->unicast) {
            struct in_addr *addr = &ipv4->unicast[0].address.in_addr;
            if (net_addr_ntop(AF_INET, addr, buf, sizeof(buf))) {
                LOG_INF("Address: %s", buf);
                
                // Start web server once we have IP
                web_server_start();
            }
        }
    }
}

int main(void)
{
    int ret;
    
    LOG_INF("ESP32 WiFi Provisioning & OTA Update Demo");
    LOG_INF("Zephyr-based WiFi OTA system initialized");
    
    // Initialize LED if available
#if LED_AVAILABLE
    if (device_is_ready(led.port)) {
        ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_WRN("Failed to configure LED pin");
        }
    } else {
        LOG_WRN("LED device not ready");
    }
#else
    LOG_INF("No LED configured");
#endif
    
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