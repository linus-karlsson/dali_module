#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>

#include "platform.h"
#include "pin_define.h"
#include "util.h"
#include "version.h"
#include "dali.h"

#include "web/web.h"

#define WATCH_DOG_TIMEOUT 120

static char my_uid[17] = {};

static nvs_t nvs = {};

void getUid64()
{
  uint8_t mac[8] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
  {
    mac[7] = mac[5];
    mac[6] = mac[4];
    mac[5] = mac[3];

    mac[3] = 0xFF;
    mac[4] = 0xFF;

    byte_to_hex(mac, 8, my_uid, false);
  }
}

void setup()
{
  getUid64();
  lsx_nvs_initialize();

  lsx_nvs_open(&nvs, "SCENES");
  uint8_t scenes[8] = {};
  uint32_t value_size = 0;
  lsx_nvs_get_bytes(&nvs, "Scenes", scenes, &value_size, sizeof(scenes));

  dali_initialize(scenes, &nvs);

  vTaskDelay(pdMS_TO_TICKS(6000));

  web_initialize(my_uid, scenes);
}

void app_main(void)
{
  setup();
}

void loop()
{
}
