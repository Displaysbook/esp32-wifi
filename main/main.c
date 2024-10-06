//cd C:\NAT
//idf.py build 
//idf.py -p COM3 flash monitor
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "lwip/lwip_napt.h"
#include "lwip/ip_addr.h"
#include "esp_ping.h"  // 用于调试网络连通性

static const char *TAG = "wifi_nat_example";

#define WIFI_SSID_STA      "BELL874"       // 替换为您的 STA SSID
#define WIFI_PASSWORD_STA  "D1757D911D4C"  // 替换为您的 STA 密码
#define WIFI_SSID_AP       "ESP32_NAT_AP"
#define WIFI_PASSWORD_AP   "12345678"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static esp_netif_t *sta_netif = NULL;
static esp_netif_t *ap_netif = NULL;

// WiFi事件处理函数
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi STA 启动成功，正在尝试连接到上级 AP...");
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi STA 断开连接，正在重试...");
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "获得 IP 地址:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void app_main(void)
{
    // 设置WiFi和LWIP日志级别为详细模式
    esp_log_level_set("wifi", ESP_LOG_DEBUG);
    esp_log_level_set("lwip", ESP_LOG_DEBUG);

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 初始化失败，擦除闪存...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_LOGI(TAG, "NVS 初始化成功");
    ESP_ERROR_CHECK(ret);

    // 初始化 TCP/IP
    ESP_LOGI(TAG, "正在初始化 TCP/IP...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "TCP/IP 初始化成功");

    // 创建默认事件循环
    ESP_LOGI(TAG, "正在创建默认事件循环...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "事件循环创建成功");

    // 创建事件组
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "创建事件组失败");
    } else {
        ESP_LOGI(TAG, "事件组创建成功");
    }

    // 创建默认的 WiFi AP 和 STA 接口
    ESP_LOGI(TAG, "正在创建默认的 WiFi AP 和 STA 接口...");
    ap_netif = esp_netif_create_default_wifi_ap();
    sta_netif = esp_netif_create_default_wifi_sta();
    ESP_LOGI(TAG, "WiFi AP 和 STA 接口创建成功");

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_LOGI(TAG, "正在初始化 WiFi...");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_LOGI(TAG, "WiFi 初始化成功");

    // 注册事件处理程序
    ESP_LOGI(TAG, "注册 WiFi 和 IP 事件处理程序...");
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    ESP_LOGI(TAG, "事件处理程序注册成功");

    // 设置 WiFi 模式为 AP+STA
    ESP_LOGI(TAG, "设置 WiFi 模式为 AP+STA...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_LOGI(TAG, "WiFi 模式设置成功");

    // 配置 STA 模式
    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = WIFI_SSID_STA,
            .password = WIFI_PASSWORD_STA,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_LOGI(TAG, "配置 STA 模式: SSID=%s, Password=%s", WIFI_SSID_STA, WIFI_PASSWORD_STA);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    // 配置 AP 模式
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = WIFI_SSID_AP,
            .ssid_len = strlen(WIFI_SSID_AP),
            .password = WIFI_PASSWORD_AP,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    if (strlen(WIFI_PASSWORD_AP) == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    ESP_LOGI(TAG, "配置 AP 模式: SSID=%s, Password=%s", WIFI_SSID_AP, WIFI_PASSWORD_AP);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    // 启动 WiFi
    ESP_LOGI(TAG, "启动 WiFi...");
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi 启动成功");

    // 等待 STA 连接
    ESP_LOGI(TAG, "等待 STA 连接...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "STA 已连接");

    // 获取 AP 接口的 IP 信息
    esp_netif_ip_info_t ip_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ip_info));
    ESP_LOGI(TAG, "获取 AP 接口的 IP 信息: " IPSTR, IP2STR(&ip_info.ip));

    // 启用 NAT 功能
    ESP_LOGI(TAG, "启用 NAT 功能...");
    ip_napt_enable(ip_info.ip.addr, 1);
    ESP_LOGI(TAG, "NAT 已启用");

    // 主任务
    while (1) {
        ESP_LOGI(TAG, "主任务正在运行...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
