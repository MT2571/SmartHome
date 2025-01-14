/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sys.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "dht22.h"
#include "driver/adc.h"
#include "esp_smartconfig.h"
#include "esp_mac.h"

static const char *TAG = "mqtt_example";
#define USER_SSID "d"
#define USER_PASSWORD "21521929"
#define STORAGE_NAMESPACE "storage"

#define DHT22_PIN GPIO_NUM_4
#define LD2410_PIN GPIO_NUM_5
#define LDR_PIN ADC1_CHANNEL_6

static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *WIFI_TAG = "smartconfig_example";
static EventGroupHandle_t s_wifi_event_group;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

int retry_num=0;
float temperature, humidity;
char sensorTopic[50] = "esp32/Sensor";
char epoch_time_topic[50] = "esp32/Epoch_time";
char deviceName[20] = "SensorNode-1";
char data[256];
float temp = 0;
int qos = 1;
uint8_t uwifi_ssid[32] = "d";
uint8_t uwifi_password[64] = "21521929";

static void smartconfig_example_task(void * parm);

esp_err_t get_wifi_user_config(void)
{
    nvs_handle_t my_handle;
    esp_err_t err;
    printf("read nvs\n");
    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    size_t required_size = 0;  // value will default to 0, if not set yet in NVS
    // obtain required memory space to store blob being read from NVS
    err = nvs_get_blob(my_handle, "wifi_ssid", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    printf("wifi_ssid:");
    if (required_size == 0) {
        printf("Nothing saved yet!\n");
    } else {
        uint8_t* wifi_ssid = malloc(required_size);
        err = nvs_get_blob(my_handle, "wifi_ssid", wifi_ssid, &required_size);
        if (err != ESP_OK) {
            free(wifi_ssid);
            
        }
        for (size_t i = 0; i < required_size; i++) {
            printf("%c", wifi_ssid[i]);
        }
        printf("\n");
        memcpy(uwifi_ssid, wifi_ssid, sizeof(uwifi_ssid));
        free(wifi_ssid);
    }

    required_size = 0;  
    err = nvs_get_blob(my_handle, "wifi_password", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err;
    printf("wifi_password:");
    if (required_size == 0) {
        printf("Nothing saved yet!\n");
    } else {
        uint8_t* wifi_password = malloc(required_size);
        err = nvs_get_blob(my_handle, "wifi_password", wifi_password, &required_size);
        if (err != ESP_OK) {
            free(wifi_password);
            return err;
        }
        for (size_t i = 0; i < required_size; i++) {
            printf("%c", wifi_password[i]);
        }
        printf("\n");
        memcpy(uwifi_password, wifi_password, sizeof(uwifi_password));
        free(wifi_password);
    }

    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

esp_err_t save_wifi_inf(uint8_t* wifi_ssid, uint8_t* wifi_password)
{
    nvs_handle_t my_handle;
    esp_err_t err;

    // Open
    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, "wifi_ssid", wifi_ssid, 32);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(my_handle, "wifi_password", wifi_password, 64);
    if (err != ESP_OK) return err;

    // Commit
    err = nvs_commit(my_handle);
    if (err != ESP_OK) return err;

    // Close
    nvs_close(my_handle);
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) 
    {
        printf("WIFI CONNECTING....\n");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED)
    {
        printf("WiFi CONNECTED\n");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        printf("WiFi lost connection\n");
        if(retry_num<5)
        {
            esp_wifi_connect();
            retry_num++;
            xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
            printf("Retrying to Connect...\n");
        }
        else
        {
            xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) 
    {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        printf("Wifi got IP...\n\n");
    } 
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) 
    {
        ESP_LOGI(WIFI_TAG, "Scan done");
    } 
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) 
    {
        ESP_LOGI(WIFI_TAG, "Found channel");
    } 
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) 
    {
        ESP_LOGI(WIFI_TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

#ifdef CONFIG_SET_MAC_ADDRESS_OF_TARGET_AP
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            ESP_LOGI(WIFI_TAG, "Set MAC address of target AP: "MACSTR" ", MAC2STR(evt->bssid));
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }
#endif

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(WIFI_TAG, "SSID:%s", ssid);
        ESP_LOGI(WIFI_TAG, "PASSWORD:%s", password);
        save_wifi_inf(ssid, password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(WIFI_TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } 
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) 
    {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

void wifi_connection()
{
     //                          s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_netif_init();
    s_wifi_event_group = xEventGroupCreate();
    esp_event_loop_create_default();     // event loop                    s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station                      s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); //     
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration;
    //  = {
    //     .sta = {
    //         .ssid = USER_SSID,
    //         .password = USER_PASSWORD
    //         }
    //     };
    bzero(&wifi_configuration, sizeof(wifi_config_t));
    memcpy(wifi_configuration.sta.ssid, uwifi_ssid, sizeof(wifi_configuration.sta.ssid));
    memcpy(wifi_configuration.sta.password,  uwifi_password, sizeof(wifi_configuration.sta.password));
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    esp_wifi_start();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_connect();
    printf( "wifi_init_softap finished. SSID:%s  password:%s", uwifi_ssid, uwifi_password);
}

static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(WIFI_TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(WIFI_TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
esp_mqtt_event_handle_t event;
esp_mqtt_client_handle_t client;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    event = event_data;
    client = event->client;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    // esp_mqtt_client_config_t mqtt_cfg = {
    //     .broker.address.uri = "mqtt://mqtt.flespi.io:1883",
    //     .credentials.username = "o5DKonYxncEKrNRJ2FfUFBHH09LhkNMyJf6VsUgEUKe9oVJeqOYWdvt3v2aDGKZd",
    //     .credentials.authentication.password = "",
    //     .credentials.client_id = "esp3201",
    // };

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = "mqtt://broker.emqx.io",
        .username = "esp32",
        .password = "d123456",
        .client_id = "esp3201",

    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void sensor_app(void *pvParameter)
{
    while (1)
    {
        if (dht_read_float_data(DHT22_PIN, &humidity, &temperature) == ESP_OK)
        {
            printf("Humidity: %.1f%% Temp: %.1fC\n", humidity, temperature);
        }
        else
        {
            printf("Could not read data from sensor\n");
        }
            
        int brightness = adc1_get_raw(LDR_PIN);
        printf("LDR: %d\n", brightness);
        
        int humanPresent = gpio_get_level(LD2410_PIN);
        printf("Have human: %d\n", humanPresent);

        sprintf(data, "device_name: %s, human_presence: %d, brightness: %d, temperature:%.1f, humidity:%.1f", deviceName, 
        humanPresent, brightness, temperature, humidity);

        esp_mqtt_client_publish(client, sensorTopic, data, sizeof(data), qos, 0);

        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    err = get_wifi_user_config();
    if (err != ESP_OK) printf("Error (%s) reading data from NVS!\n", esp_err_to_name(err));
    
    wifi_connection();

    mqtt_app_start();
    gpio_set_direction(DHT22_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(LD2410_PIN, GPIO_MODE_INPUT);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(LDR_PIN, ADC_ATTEN_DB_11);
    xTaskCreate( &sensor_app, "sensor_task", 2048, NULL, 5, NULL );
}
