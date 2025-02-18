#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_client.h"

// Definições para o KY-025
#define KY025_PIN GPIO_NUM_4 // Altere para GPIO_NUM_4

// Credenciais da rede Wi-Fi
#define WIFI_SSID "teste"      
#define WIFI_PASS "12345678"

// Credenciais ThingSpeak
#define THINGSPEAK_API_KEY "W2OPZCBARZ69YF99" 
#define THINGSPEAK_URL "http://api.thingspeak.com/update"

// Intervalo de envio em milissegundos (ex: 10 segundos = 10000 ms)
#define INTERVALO_ENVIO_THINGSPEAK 10000

volatile int count = 0; // Contador para o número de eventos do sensor
static const char *TAG = "wifi_station";

// Função de tratamento da interrupção do KY-025
void IRAM_ATTR ky025_isr(void) {
    count++; // Incrementa o contador a cada evento do sensor
}

// Função para configurar o GPIO e a interrupção do KY-025
void setup_ky025(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_POSEDGE, // Interrupção na borda de subida
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << KY025_PIN),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    gpio_config(&io_conf);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(KY025_PIN, ky025_isr, NULL);
}

// Função para enviar dados ao ThingSpeak
void envia_dados_thingspeak(int valor) {
    char query[128];
    snprintf(query, sizeof(query), "api_key=%s&field1=%d", THINGSPEAK_API_KEY, valor);

    esp_http_client_config_t config = {
        .url = THINGSPEAK_URL,
        .method = HTTP_METHOD_POST,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, query, strlen(query));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Dados enviados com sucesso ao ThingSpeak!");
    } else {
        ESP_LOGE(TAG, "Erro ao enviar dados ao ThingSpeak: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// Tarefa para monitorar e enviar a contagem acumulada a cada 10 segundos
void monitor_count(void *pvParameters) {
    while (1) {
        int contador_acumulado = count; // Captura o valor acumulado
        count = 0; // Reseta o contador para o próximo intervalo

        ESP_LOGI(TAG, "Contagem acumulada em 10 segundos: %d", contador_acumulado);

        // Envia os dados acumulados ao ThingSpeak
        envia_dados_thingspeak(contador_acumulado);

        // Aguarda o próximo intervalo de 10 segundos
        vTaskDelay(pdMS_TO_TICKS(INTERVALO_ENVIO_THINGSPEAK));
    }
}

void wifi_event_handler(void* arg, esp_event_base_t event_base,
                        int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Tentando reconectar ao Wi-Fi...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conectado! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Inicialização do Wi-Fi concluída.");
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    setup_ky025();
    wifi_init();

    xTaskCreate(monitor_count, "Monitor Count", 4096, NULL, 10, NULL);
}
xEventGroupSync