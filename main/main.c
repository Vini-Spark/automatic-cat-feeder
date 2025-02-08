#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Wi-Fi configuration
#define WIFI_SSID "ESP32_Servo_AP"
#define WIFI_PASS "123456789"

// Servo configuration
#define SERVO_GPIO GPIO_NUM_18
#define LEDC_TIMER LEDC_TIMER_0
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL LEDC_CHANNEL_0
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY 50 // 50 Hz for SG90 servo

// HTML interface
static const char *HTML_FORM = "<!DOCTYPE html>\
<html>\
<head>\
    <title>Servo Control</title>\
</head>\
<body>\
    <h1>Servo Control</h1>\
    <form action=\"/control\" method=\"post\">\
        <label for=\"turns\">Number of Turns:</label>\
        <input type=\"number\" id=\"turns\" name=\"turns\" min=\"1\" max=\"10\" required><br><br>\
        <button type=\"submit\" name=\"direction\" value=\"cw\">Clockwise</button>\
        <button type=\"submit\" name=\"direction\" value=\"ccw\">Counterclockwise</button>\
    </form>\
    <form action=\"/control-duty\" method=\"post\">\
        <label for=\"Duty Cycle\">Set the Duty Cycle:</label>\
        <input type=\"number\" id=\"duty\" name=\"duty\" min=\"0\" max=\"8192\" required><br><br>\
    </form>\
</body>\
</html>";

// Function to set servo speed
void set_servo_speed(int speed) {
    // Neutral duty cycle for 50 Hz PWM: 7.5% (767 for 10-bit resolution)
    uint32_t neutral_duty = 614;

    // Map speed to duty cycle
    // Speed range: -100 (full clockwise) to 100 (full counterclockwise)
    uint32_t duty = neutral_duty + speed;

    // Clamp duty cycle to valid range
    if (duty < 368) duty = 368; // Minimum duty cycle (5%)
    if (duty > 860) duty = 860; // Maximum duty cycle (10%)

    ESP_LOGI("SERVO", "Setting speed: %d, Duty: %lu", speed, duty);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

// Function to set servo speed
void set_servo_duty(int duty) {
    ESP_LOGI("SERVO", "Setting Duty: %d", duty);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static esp_err_t control_handler(httpd_req_t *req) {
    char buf[512]; // Buffer to store the incoming data
    int ret, remaining = req->content_len;

    ESP_LOGI("CONTROL", "Received POST request with content length: %d", remaining);

    if (req->method == HTTP_POST) {
        // Receive the POST data
        ret = httpd_req_recv(req, buf, remaining);
        if (ret > 0) {
            buf[ret] = '\0'; // Null-terminate the buffer
            ESP_LOGI("CONTROL", "Received data: %s", buf);

            // Parse form data
            char *turns_str = strstr(buf, "turns=");
            char *direction_str = strstr(buf, "direction=");

            if (turns_str && direction_str) {
                // Extract the number of turns
                int turns = atoi(turns_str + 6);
                ESP_LOGI("CONTROL", "Number of turns: %d", turns);

                // Extract the direction
                char direction[10];
                sscanf(direction_str + 10, "%[^&]", direction);
                ESP_LOGI("CONTROL", "Direction: %s", direction);

                // Rotate servo
                for (int i = 0; i < turns; i++) {
                    if (strcmp(direction, "cw") == 0) {
                        ESP_LOGI("CONTROL", "Rotating Clockwise");
                        set_servo_speed(-100); // Rotate clockwise
                    } else if (strcmp(direction, "ccw") == 0) {
                        ESP_LOGI("CONTROL", "Rotating Counterclockwise");
                        set_servo_speed(100); // Rotate counterclockwise
                    }
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
                }

                // Stop the servo after rotation
                set_servo_speed(0);
            } else {
                ESP_LOGE("CONTROL", "Failed to parse form data");
            }
        } else {
            ESP_LOGE("CONTROL", "Failed to receive POST data");
        }
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}


static esp_err_t control_handler_duty(httpd_req_t *req) {
    char buf[512]; // Buffer to store the incoming data
    int ret, remaining = req->content_len;

    ESP_LOGI("CONTROL", "Received POST request with content length: %d", remaining);

    if (req->method == HTTP_POST) {
        // Receive the POST data
        ret = httpd_req_recv(req, buf, remaining);
        if (ret > 0) {
            buf[ret] = '\0'; // Null-terminate the buffer
            ESP_LOGI("CONTROL", "Received data: %s", buf);

            // Parse form data
            char *duty_str = strstr(buf, "duty=");

            if (duty_str) {
                // Extract the number of turns
                int duty = atoi(duty_str + 5);
                ESP_LOGI("CONTROL", "Duty: %d", duty);

                set_servo_duty(duty); // Rotate clockwise
                    
                vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1 second
                
            } else {
                ESP_LOGE("CONTROL", "Failed to parse form data");
            }
        } else {
            ESP_LOGE("CONTROL", "Failed to receive POST data");
        }
    }

    // Redirect back to the main page
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP request handler for root
static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, strlen(HTML_FORM));
    return ESP_OK;
}

// Start the web server
void start_webserver() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Increase buffer sizes
    config.max_uri_handlers = 16;       // Increase the number of URI handlers
    config.max_resp_headers = 16;       // Increase the number of response headers
    config.recv_wait_timeout = 10;      // Increase the receive timeout (in seconds)
    config.send_wait_timeout = 10;      // Increase the send timeout (in seconds)
    config.max_open_sockets = 7;        // Increase the number of open sockets
    config.stack_size = 10240;          // Increase the stack size for the HTTP server task
    config.lru_purge_enable = true;     // Enable LRU purging of old connections
    config.uri_match_fn = httpd_uri_match_wildcard; // Use wildcard URI matching

    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        });
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/control",
            .method = HTTP_POST,
            .handler = control_handler,
            .user_ctx = NULL
        });
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri = "/control-duty",
            .method = HTTP_POST,
            .handler = control_handler_duty,
            .user_ctx = NULL
        });
    }
}

// Initialize Wi-Fi as an access point
void wifi_init_softap() {
    ESP_LOGI("WIFI", "Initializing Wi-Fi Access Point...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize TCP/IP adapter
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default Wi-Fi AP
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (ap_netif == NULL) {
        ESP_LOGE("WIFI", "Failed to create default Wi-Fi AP");
        return;
    }

    // Initialize Wi-Fi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure Wi-Fi AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "Wi-Fi Access Point Initialized. SSID: %s", WIFI_SSID);
}

// Initialize servo
void servo_init() {
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ch_conf);
}

void app_main() {
    // Initialize Wi-Fi and web server
    wifi_init_softap();
    start_webserver();

    // Initialize servo
    servo_init();

    ESP_LOGI("MAIN", "Servo control system is ready!");
}