#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "driver/uart.h"

#define WEB_SERVER "192.168.1.71"
#define WEB_PORT "1602"
#define WEB_PATH ""
#define UART_NUM UART_NUM_2
#define BUF_SIZE 1024
char request[200];

static const char *TAG = "UART_HTTP";
static char* createReq( char* mac, char*request)
{
    const char *REQUEST_FORMAT = "POST %s/%s HTTP/1.1\r\n" 
        "Host: %s:%s\r\n"
        "Content-Length:0\r\n"
        "content-type"
        "\r\n",
    sprintf(request, REQUEST_FORMAT, WEB_PATH, mac, WEB_SERVER, WEB_PORT);
    return request;
}

static void http_post_task(void *request)
{
      const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;
    char recv_buf[64];

    while (1)
    {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if (err != 0 || res == NULL)
        {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if (s < 0)
        {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if (connect(s, res->ai_addr, res->ai_addrlen) != 0)
        {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, request, strlen(request)) < 0)
        {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                       sizeof(receiving_timeout)) < 0)
        {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");
        ESP_LOGE(TAG,"--------------22222222222222----------------------------");

        /* Read HTTP response */
        do
        {
            bzero(recv_buf, sizeof(recv_buf));
            r = read(s, recv_buf, sizeof(recv_buf) - 1);
            for (int i = 0; i < r; i++)
            {
                putchar(recv_buf[i]);
            }
        } while (r > 0);

        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        break;
    }
}

static void uart_event_task(void *pvParameters)
{
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    if (data == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for UART data buffer");
        vTaskDelete(NULL);
        return;
    }

    while (1)
    {
        int length = 0; 
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM, (size_t *)&length));
        length = uart_read_bytes(UART_NUM, data, length, 100 / portTICK_PERIOD_MS);
        
        if (length == 0) 
        { 
            ESP_LOGE(TAG, "*************************");
        }
        else if (length > 0) 
        { 
            data[length] = '\r'; // Null-terminate the string 
            ESP_LOGI(TAG, "Received: %s", data); 

            char *mac = (char *)malloc(length + 1);
            if (mac == NULL) 
            { 
                ESP_LOGE(TAG, "Failed to allocate memory for mac data"); 
                
                continue; 
            } 
            
            strcpy(mac, (char *)data); 
            
            // Buffer lớn hơn để chứa request
            createReq(mac, request); // Không cần dùng *

            http_post_task(request); // Không cần dùng *

            free(mac); // Đừng quên giải phóng bộ nhớ
            ESP_LOGE(TAG,"--------------44444444444444444----------------------------");
          uart_write_bytes(UART_NUM,"OK\r", 2);
        };
         vTaskDelay(3000 / portTICK_PERIOD_MS);
    }

    free(data);
    vTaskDelete(NULL);
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());

    const uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
};;

    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));

    xTaskCreate(&uart_event_task,"uart_event_task", 4096,  NULL,  5,  NULL); 
    ESP_LOGE(TAG,"--------------6666666666666666666666----------------------------");

}
