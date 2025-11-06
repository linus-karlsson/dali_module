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
  lsx_gpio_config(DALI_TX, LSX_GPIO_MODE_OUTPUT, LSX_GPIO_INTR_DISABLE, false, false);
  lsx_gpio_config(DALI_RX, LSX_GPIO_MODE_INPUT, LSX_GPIO_INTR_ANYEDGE, true, false);
  const uint8_t dali_input_pins[] = { DALI_PIN_0, DALI_PIN_1, DALI_PIN_2 };
  for (uint32_t i = 0; i < array_size(dali_input_pins); ++i)
  {
    lsx_gpio_config(dali_input_pins[i], LSX_GPIO_MODE_INPUT, LSX_GPIO_INTR_DISABLE,
                    true, false);
  }

  getUid64();
  lsx_nvs_initialize();

  lsx_nvs_open(&nvs, "DALI_CONFIG");
  dali_config_t config = {};
  uint32_t value_size = 0;
  lsx_nvs_get_uint8(&nvs, "BEnable", &config.blink_enabled, 0);
  lsx_nvs_get_uint8(&nvs, "FTime", &config.fade_time, 4);
  lsx_nvs_get_uint32(&nvs, "BDuration", &config.blink_duration, 0);
  lsx_nvs_get_bytes(&nvs, "Scenes", config.scenes, &value_size,
                    sizeof(config.scenes));

  dali_led_initialize();

  lsx_log("Dali scenes: ");
  for (int i = 0; i < 8; i++)
  {
    lsx_log("%d ", config.scenes[i]);
  }
  lsx_log("\n");

  web_initialize(my_uid, config);
  dali_initialize(&nvs, config);
}

void app_main(void)
{
  setup();
}

void loop()
{
}
