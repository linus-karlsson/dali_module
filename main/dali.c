#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/ledc.h>
#include <led_strip.h>
#include <led_strip.h>

#include <esp_task_wdt.h>
#include <string.h>

#include "dali.h"
#include "util.h"
#include "platform.h"
#include "pin_define.h"

#define DALI_MESSAGE_LENGTH 6

#define DALI_STACK_SIZE          4096
#define DALI_MESSAGE_TOTAL_COUNT 10

#define DALI_RECIEVE_TOTAL_COUNT 1024
/*
Bit  Description
 0   Fault of the DALI control gear
 1   Lamp failure
 2   Lamp power ON
 3   Limit value error
 4   Dimming is active
 5   Reset state
 6   Short address missing
 7   The power supply was enabled
*/

typedef struct led_rgb_t
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} led_rgb_t;

typedef struct dali_t
{
  uint32_t delay;
  uint8_t tx_pin;
  uint8_t rx_pin;
  uint32_t on_the_same_level_count;

  uint32_t on_0;

  uint8_t fade_rate;
  uint8_t fade_time;
  uint8_t dimming_curve;

  uint8_t short_address;

  uint8_t current_brightness;

  dali_config_t config;
  dali_config_t temp_config;

  bool set_config;

  led_strip_handle_t led_strip;
  led_rgb_t leds[NUM_LEDS];

  nvs_t nvs;
  nvs_t* scene_nvs;
} dali_t;

static uint8_t get_input_index(bool* filter_values)
{
  uint8_t result = (uint8_t)(filter_values[2]);
  result <<= 1;
  result |= (uint8_t)(filter_values[1]);
  result <<= 1;
  result |= (uint8_t)(filter_values[0]);
  return min(result, 7);
}

static StackType_t dali_stack[DALI_STACK_SIZE] = {};
static StaticTask_t dali_stack_type = {};

static dali_t dali = {};

static const int delay_time = 15;

static const uint8_t dali_input_pins[] = { DALI_PIN_0, DALI_PIN_1, DALI_PIN_2 };

void dali_initialize_(void);
void dali_turn_on_light_(void);
void dali_turn_off_light_(void);
void dali_set_brightness_(uint8_t brightness);
void dali_set_fade_time_(uint8_t fade_time);
void dali_set_fade_rate_(uint8_t fade_rate);
void dali_select_dimming_curve_(uint8_t curve);
void dali_set_DTR0_(uint8_t value);
void dali_set_min_brightness_(uint8_t min_brightness);
void dali_set_max_brightness_(uint8_t max_brightness);
void dali_transmit(uint8_t address, uint8_t command);
void dali_set_DTR0(uint8_t value);
uint8_t dali_receive(bool* error);
uint8_t dali_scale(uint8_t procent, uint8_t* min_brightness_out);

void led_set(uint8_t led_number, uint8_t r, uint8_t g, uint8_t b)
{
  led_rgb_t input = {};
  input.r = r;
  input.g = g;
  input.b = b;
  dali.leds[led_number] = input;
  led_strip_set_pixel(dali.led_strip, led_number, input.r, input.g, input.b);
  led_strip_refresh(dali.led_strip);
}

void led_set_no_refresh_internal(uint8_t led_number, uint8_t r, uint8_t g, uint8_t b)
{
  led_rgb_t input = {};
  input.r = r;
  input.g = g;
  input.b = b;
  dali.leds[led_number] = input;
  led_strip_set_pixel(dali.led_strip, led_number, r, g, b);
}

bool dali_set_config(dali_config_t config)
{
  if (!dali.set_config)
  {
    dali.temp_config = config;
    dali.set_config = true;
    return true;
  }
  return false;
}

static volatile uint32_t pin_change_count = 0;
static volatile uint8_t pin_change_buffer[128] = {};
static volatile uint32_t interupt_timemark = 0;

void pin_change(void* arguments)
{
  uint32_t now_time = lsx_get_micro();
  uint32_t difference = now_time - interupt_timemark;
  if (difference >= (dali.delay + 100))
  {
    interupt_timemark = now_time;
    if (difference < ((dali.delay + dali.delay) + 200))
    {
      pin_change_count *= (pin_change_count < sizeof(pin_change_buffer));
      uint32_t index = pin_change_count;
      pin_change_count = index + 1;
      pin_change_buffer[index] = lsx_gpio_read(dali.rx_pin);
    }
    else
    {
      pin_change_count = 0;
      interupt_timemark -= 200;
    }
  }
}

uint8_t light_control_get_fade_time(void)
{
  return dali.fade_time;
}

void light_control_add_interrupt(void)
{
  lsx_gpio_add_pin_interrput(dali.rx_pin, pin_change, NULL);
}

void light_control_remove_interrupt(void)
{
  lsx_gpio_remove_pin_interrput(dali.rx_pin);
}

uint8_t dali_is_extended_command(uint8_t command)
{
  return ((command == DALI_EX_REFERENCE_SYSTEM_POWER) ||
          (command == DALI_EX_ENABLE_CURRENT_PROTECTOR) ||
          (command == DALI_EX_DISABLE_CURRENT_PROTECTOR) ||
          (command == DALI_EX_SELECT_DIMMING_CURVE) ||
          (command == DALI_EX_STORE_DTR_AS_FAST_FADE_TIME) ||
          (command == DALI_EX_QUERY_GEAR_TYPE) ||
          (command == DALI_EX_QUERY_DIMMING_CURVE) ||
          (command == DALI_EX_QUERY_POSSIBLE_OPERATING_MODES) ||
          (command == DALI_EX_QUERY_FEATURES) ||
          (command == DALI_EX_QUERY_FAILURE_STATUS) ||
          (command == DALI_EX_QUERY_SHORT_CIRCUIT) ||
          (command == DALI_EX_QUERY_OPEN_CIRCUIT) ||
          (command == DALI_EX_QUERY_LOAD_DECREASE) ||
          (command == DALI_EX_QUERY_LOAD_INCREASE) ||
          (command == DALI_EX_QUERY_CURRENT_PROTECTOR_ACTIVE) ||
          (command == DALI_EX_QUERY_THERMAL_SHUT_DOWN) ||
          (command == DALI_EX_QUERY_THERMAL_OVERLOAD) ||
          (command == DALI_EX_QUERY_REFERENCE_RUNNING) ||
          (command == DALI_EX_QUERY_REFERENCE_MEASUREMENT_FAILED) ||
          (command == DALI_EX_QUERY_CURRENT_PROTECTOR_ENABLED) ||
          (command == DALI_EX_QUERY_OPERATING_MODE) ||
          (command == DALI_EX_QUERY_FAST_FADE_TIME) ||
          (command == DALI_EX_QUERY_MIN_FAST_FADE_TIME) ||
          (command == DALI_EX_QUERY_EXTENDED_VERSION_NUMBER));
}

static inline void dali_broadcast(uint8_t command)
{
  dali_transmit(DALI_BROADCAST, command);
}

static inline void dali_broadcast_twice(uint8_t command)
{
  lsx_delay_millis(delay_time);
  dali_broadcast(command);
  lsx_delay_millis(delay_time);
  dali_broadcast(command);
  lsx_delay_millis(delay_time);
}

uint8_t dali_query_(uint8_t address, uint8_t command, uint8_t is_extended,
                    bool* error)
{
  lsx_delay_millis(delay_time);

  if (is_extended)
  {
    dali_transmit(0xC1, 6);
    lsx_delay_millis(delay_time);
  }
  pin_change_count = 0;
  dali_transmit(address, command);
  return dali_receive(error);
}

uint8_t dali_query1(uint8_t address, uint8_t command, uint8_t is_extended,
                    bool* error)
{
  uint8_t response = dali_query_(address, command, is_extended, error);
  if (error)
  {
    uint32_t total_number_of_tries = 3;
    uint32_t tries = 0;
    while ((*error) && (tries++ < total_number_of_tries))
    {
      *error = false;
      lsx_delay_millis(delay_time * 2);
      response = dali_query_(address, command, is_extended, error);
    }
  }
  return response;
}

uint8_t dali_query0(uint8_t address, uint8_t command, uint8_t is_extended,
                    bool* error)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(40));

  uint32_t total_number_queries = 6;
  uint32_t count = 0;
  uint8_t responses[6] = {};

  if (error) *error = true;

  for (uint32_t i = 0; i < total_number_queries; ++i)
  {
    bool error_temp = false;
    uint8_t temp_response = dali_query1(address, command, is_extended, &error_temp);
    if (!error_temp)
    {
      responses[count++] = temp_response;
    }
    vTaskDelay(pdMS_TO_TICKS(16));
  }
  if (count >= 4)
  {
    uint32_t correct_count = 0;
    uint8_t current_response = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
      correct_count = 0;
      current_response = responses[i];
      for (uint32_t j = 0; j < count; ++j)
      {
        if ((current_response == responses[j]) && ((++correct_count) >= 4))
        {
          if (error) *error = false;
          lsx_log("Query: %u\n", current_response);

          led_set(LED_DALI, 0, 127, 0);
          vTaskDelay(pdMS_TO_TICKS(40));

          return current_response;
        }
      }
    }
  }
  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(40));
  return 0;
}

uint8_t dali_query(uint8_t command, uint8_t is_extended, bool* error_out)
{
  uint8_t result = 0;
  if (dali.short_address == 0)
  {
    result = dali_query0(DALI_BROADCAST, command, is_extended, error_out);
  }
  else
  {
    bool success = false;
    uint8_t max = 0;
    for (uint32_t i = 0; i < dali.short_address; ++i)
    {
      bool error = false;
      uint8_t current = dali_query0((i << 1) | 0x01, command, is_extended, &error);
      if (!error && (current >= max))
      {
        max = current;
        success = true;
      }
    }
    if (!success)
    {
      lsx_log("BAD\n");
      result = dali_query0(DALI_BROADCAST, command, is_extended, error_out);
    }
    else
    {
      if (error_out) *error_out = false;
      result = max;
    }
  }
  return result;
}

uint32_t commands_count = 0;
uint8_t commands_sent[1024] = {};
uint8_t same_count = 0;

void check_if_stored_command(uint8_t command)
{
  switch (command)
  {
    case DALI_SET_FADE_TIME:
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      bool error = false;
      uint8_t fade_time_rate = dali_query(DALI_QUERY_FADE_TIME, 0, &error);
      if (!error)
      {
        uint8_t current_fade_time = (fade_time_rate >> 4) & 0x0F;
        if (dali.fade_time != current_fade_time)
        {
          dali.fade_time = current_fade_time;
          lsx_nvs_set_uint8(&dali.nvs, "DFT", dali.fade_time);
        }
      }

      break;
    }
    case DALI_SET_FADE_RATE:
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      bool error = false;
      uint8_t fade_rate = dali_query(DALI_QUERY_FADE_TIME, 0, &error) & 0x0F;
      if (!error && (dali.fade_rate != fade_rate))
      {
        dali.fade_rate = fade_rate;
        lsx_nvs_set_uint8(&dali.nvs, "DFR", dali.fade_rate);
      }
      break;
    }
    case DALI_EX_SELECT_DIMMING_CURVE:
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      bool error = false;
      uint8_t curve = dali_query(DALI_EX_QUERY_DIMMING_CURVE, 1, &error);
      if (!error && (dali.dimming_curve != curve))
      {
        dali.dimming_curve = curve;
        lsx_nvs_set_uint8(&dali.nvs, "DC", dali.dimming_curve);
      }
      break;
    }
  }
}

void dali_send_brightness(uint8_t brightness)
{
  uint8_t min_brightness = 170;

  brightness = dali_scale(brightness, &min_brightness) * (brightness != 0);

  lsx_delay_millis(delay_time);
  bool error = false;
  uint8_t level = dali_query(DALI_QUERY_ACTUAL_OUTPUT, 0, &error);

  lsx_log("Error: %u\n", error);
  lsx_delay_millis(delay_time);
  if (!error && (brightness != level))
  {
    dali.on_the_same_level_count = 0;
  }
  if (error || ((++dali.on_the_same_level_count) <= 8))
  {
    lsx_log("Sending brightness: %u\n", brightness);
    if ((level <= min_brightness) && (brightness == 0))
    {
      if (error)
      {
        if ((++dali.on_0) >= 6)
        {
          lsx_log("off\n");
          dali_turn_off_light_();
        }
        else
        {
          dali_set_brightness_(brightness);
        }
      }
      else
      {
        lsx_log("off\n");
        dali_turn_off_light_();
      }
    }
    else
    {
      dali.on_0 = 0;

      dali_set_brightness_(brightness);
    }
  }
}

void dali_set_command(uint8_t value, uint8_t command, bool extended)
{
  dali_set_DTR0(value);
  if (extended)
  {
    lsx_delay_millis(delay_time);
    dali_transmit(0xC1, 6);
  }
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(command);
  lsx_delay_millis(delay_time);
  check_if_stored_command(command);
}

static uint32_t filter_total_count = 100;

static void check_input(uint8_t pin, uint8_t* filter_values,
                        uint32_t* filter_indices_out, uint32_t* filter_count_out)
{
  uint32_t filter_indices = *filter_indices_out;
  uint32_t filter_count = *filter_count_out;

  filter_values[filter_indices] = lsx_gpio_read(pin) == LSX_GPIO_LOW;
  filter_count = min(filter_count + 1, filter_total_count);
  filter_indices = plus_one_wrap(filter_indices, filter_total_count);

  (*filter_indices_out) = filter_indices;
  (*filter_count_out) = filter_count;
}

void dali_send_bit(uint8_t bit)
{
  if (!bit)
  {
    lsx_gpio_write(dali.tx_pin, LSX_GPIO_LOW);
    lsx_delay_micro(dali.delay);
    lsx_gpio_write(dali.tx_pin, LSX_GPIO_HIGH);
    lsx_delay_micro(dali.delay);
  }
  else
  {
    lsx_gpio_write(dali.tx_pin, LSX_GPIO_HIGH);
    lsx_delay_micro(dali.delay);
    lsx_gpio_write(dali.tx_pin, LSX_GPIO_LOW);
    lsx_delay_micro(dali.delay);
  }
}

void dali_send_byte(uint8_t byte)
{
  dali_send_bit((byte >> 7) & 0x01);
  dali_send_bit((byte >> 6) & 0x01);
  dali_send_bit((byte >> 5) & 0x01);
  dali_send_bit((byte >> 4) & 0x01);
  dali_send_bit((byte >> 3) & 0x01);
  dali_send_bit((byte >> 2) & 0x01);
  dali_send_bit((byte >> 1) & 0x01);
  dali_send_bit(byte & 0x01);
}

void dali_transmit(uint8_t address, uint8_t command)
{
  dali_send_bit(1);
  dali_send_byte(address);
  dali_send_byte(command);
  lsx_gpio_write(dali.tx_pin, LSX_GPIO_LOW);
}

uint8_t dali_receive_(bool* error, uint32_t delay)
{
  interupt_timemark = lsx_get_micro();
  pin_change_count = 0;

  if (error) *error = true;

  uint32_t start = lsx_get_micro();
  while (pin_change_count < 9)
  {
    if ((lsx_get_micro() - start) >= delay)
    {
      return 0;
    }
  }
  if (error) *error = false;

  uint8_t response = 0;
  for (uint32_t i = 1; i < 9; ++i)
  {
    response |= (!(pin_change_buffer[i])) << (8 - i);
  }

  return response;
}

uint8_t dali_receive(bool* error)
{
  return dali_receive_(error, 100000);
}

void dali_set_DTR0(uint8_t value)
{
  dali_transmit(0xA3, value);
}

void dali_select_dimming_curve_(uint8_t curve)
{
  dali_set_DTR0(curve);
  lsx_delay_millis(delay_time);
  dali_transmit(0xC1, 6);
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(DALI_EX_SELECT_DIMMING_CURVE);
}

static void dali_set_saved_configuration(void)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(40));

  dali.fade_time = dali.config.fade_time;

  lsx_delay_millis(delay_time);
  dali_set_DTR0(dali.fade_time);
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(DALI_SET_FADE_TIME);
  lsx_delay_millis(delay_time);

  dali.dimming_curve = DALI_DIMMING_LOGARITHMIC;
  dali_set_DTR0(dali.dimming_curve);
  lsx_delay_millis(delay_time);
  dali_transmit(0xC1, 6);
  dali_broadcast_twice(DALI_EX_SELECT_DIMMING_CURVE);

  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(40));
}

void dali_scan(void)
{
  int32_t low_address = 0;
  int32_t high_address = 0x00FFFFFF;
  int32_t current_address = (int32_t)(low_address + high_address) / 2;

  dali.short_address = 0;

  bool still_scanning = true;
  while (still_scanning)
  {
    while ((high_address - low_address) > 0)
    {
      lsx_log("High: %ld\n", high_address);
      lsx_log("Low:  %ld\n", low_address);
      bool error = false;
      for (uint32_t i = 0; i < 1; ++i)
      {
        lsx_delay_millis(delay_time);
        dali_transmit(0xB1, (current_address >> 16) & 0xFF);
        lsx_delay_millis(delay_time);
        dali_transmit(0xB3, (current_address >> 8) & 0xFF);
        lsx_delay_millis(delay_time);
        dali_transmit(0xB5, current_address & 0xFF);

        lsx_delay_millis(delay_time);
        dali_transmit(0xA9, 0);
        dali_receive_(&error, 50000);
        if (!error)
        {
          break;
        }
      }

      if (error)
      {
        low_address = current_address + 1;
      }
      else
      {
        high_address = current_address;
      }

      current_address = (int32_t)(low_address + high_address) / 2;
    }
    if (high_address != 0x00FFFFFF)
    {
      int32_t found_address = current_address;
      lsx_delay_millis(delay_time);
      dali_transmit(0xB1, (found_address >> 16) & 0xFF);
      lsx_delay_millis(delay_time);
      dali_transmit(0xB3, (found_address >> 8) & 0xFF);
      lsx_delay_millis(delay_time);
      dali_transmit(0xB5, found_address & 0xFF);

      lsx_delay_millis(delay_time);
      dali_transmit(0xB7, (dali.short_address << 1) | 0x01);
      lsx_delay_millis(delay_time);

      dali_transmit(0xAB, 0);
      lsx_delay_millis(delay_time);

      lsx_log("Found address: %ld\n", current_address);

      low_address = 0;
      high_address = 0x00FFFFFF;
      current_address = (low_address + high_address) / 2;

      dali.short_address++;
    }
    else
    {
      still_scanning = false;
    }
  }

  lsx_delay_millis(delay_time);
  dali_transmit(0xA1, 0);
  lsx_delay_millis(delay_time);

  lsx_log("Short address: %u\n", dali.short_address);
}

void dali_initialize_(void)
{
  lsx_log("Dali init\n");

  dali.tx_pin = 5;
  dali.rx_pin = 6;
  dali.delay = 833 / 2;
  dali.on_the_same_level_count = 0;
  dali.current_brightness = 0;

  lsx_gpio_config(dali.tx_pin, LSX_GPIO_MODE_OUTPUT, LSX_GPIO_INTR_DISABLE, false,
                  false);

  lsx_gpio_config(dali.rx_pin, LSX_GPIO_MODE_INPUT, LSX_GPIO_INTR_ANYEDGE, true,
                  false);

  for (uint32_t i = 0; i < array_size(dali_input_pins); ++i)
  {
    lsx_gpio_config(dali_input_pins[i], LSX_GPIO_MODE_INPUT, LSX_GPIO_INTR_DISABLE,
                    true, false);
  }

  lsx_gpio_install_interrupt_service();
  lsx_gpio_add_pin_interrput(dali.rx_pin, pin_change, NULL);

  lsx_delay_millis(delay_time);
  dali_transmit(0xA5, 0);
  lsx_delay_millis(delay_time);
  dali_transmit(0xA5, 0);
  lsx_delay_millis(delay_time);

  lsx_delay_millis(delay_time);
  dali_transmit(0xA7, 0);
  lsx_delay_millis(delay_time);
  dali_transmit(0xA7, 0);
  lsx_delay_millis(delay_time);

  dali_scan();

  lsx_delay_millis(delay_time);
  dali_set_DTR0(0);
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(DALI_SET_MIN_LEVEL);
  lsx_delay_millis(delay_time);

  lsx_delay_millis(delay_time);
  dali_set_DTR0(254);
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(DALI_SET_MAX_LEVEL);
  lsx_delay_millis(delay_time);

  dali.fade_rate = 1;
  dali_set_DTR0(dali.fade_rate);
  lsx_delay_millis(delay_time);
  dali_broadcast_twice(DALI_SET_FADE_RATE);

  dali_set_saved_configuration();
}

uint8_t dali_scale(uint8_t procent, uint8_t* min_brightness_out)
{
  lsx_delay_millis(delay_time);

  bool error = false;
  uint8_t response = dali_query(DALI_QUERY_MIN_LEVEL, 0, &error);
  uint8_t min_brightness = error ? 170 : response;
  if (min_brightness_out) (*min_brightness_out) = min_brightness;

  lsx_log("Min: %u\n", min_brightness);

  lsx_delay_millis(delay_time);

  uint8_t max_brightness = 254;
  return min_brightness + (((max_brightness - min_brightness) * procent) / 100);
}

void dali_turn_on_light_(void)
{
  dali_broadcast_twice(DALI_ON);
}

void dali_turn_off_light_(void)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(40));

  dali_broadcast_twice(DALI_OFF);

  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(40));
}

void dali_set_brightness_(uint8_t brightness)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(40));

  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);

  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(40));
}

void dali_task(void* pvParameters)
{
  esp_task_wdt_add(NULL);

  bool turn_off_sequence = false;
  bool turn_off_blink = false;
  bool has_read_inputs = false;
  timer_ms_t turn_off_timer = timer_create_ms(dali.config.blink_duration * 1000);
  timer_ms_t send_brightness_timer = timer_create_ms(10000);
  timer_ms_t check_input_timer = timer_create_ms(40);
  const uint32_t random_delay_max = 10;
  uint32_t random_delay = random_delay_max;

  uint32_t filter_count[3] = {};
  uint32_t filter_indices[3] = {};
  uint8_t filter_values[3][filter_total_count] = {};

  bool filter_value[3] = {};

  uint8_t last_sent_brightness = 0;

  uint8_t main_light_tick = 24;
  uint32_t last_time = lsx_get_millis();
  float delta_time = 0.0f;

  while (true)
  {
    uint32_t start = lsx_get_millis();
    esp_task_wdt_reset();

    if (dali.set_config)
    {
      dali.config = dali.temp_config;
      memset(&dali.temp_config, 0, sizeof(dali.temp_config));
      lsx_nvs_set_uint8_ram(dali.scene_nvs, "BEnable", dali.config.blink_enabled);
      lsx_nvs_set_uint8_ram(dali.scene_nvs, "FTime", dali.config.fade_time);
      lsx_nvs_set_uint32_ram(dali.scene_nvs, "BDuration", dali.config.blink_duration);
      lsx_nvs_set_bytes_ram(dali.scene_nvs, "Scenes", dali.config.scenes,
                            sizeof(dali.config.scenes));
      lsx_nvs_commit(dali.scene_nvs);

      dali.set_config = false;

      dali_set_saved_configuration();
      vTaskDelay(pdMS_TO_TICKS(5000));

      lsx_log("Dali scenes: ");
      for (int i = 0; i < 8; i++)
      {
        lsx_log("%d ", dali.config.scenes[i]);
      }
      lsx_log("\n");
    }

    if (timer_is_up_ms(check_input_timer, lsx_get_millis()))
    {
      random_delay = rand() % random_delay_max;
      check_input_timer.duration =
        50 + ((random_delay) * (random_delay <= random_delay_max));
      check_input_timer.time = lsx_get_millis();

      for (uint32_t i = 0; i < array_size(filter_count); ++i)
      {
        check_input(dali_input_pins[i], filter_values[i], filter_indices + i,
                    filter_count + i);
      }
    }

    if ((filter_count[0] >= filter_total_count) &&
        (filter_count[1] >= filter_total_count) &&
        (filter_count[2] >= filter_total_count))
    {
      uint32_t on_count[3] = { 0, 0, 0 };
      for (uint32_t i = 0; i < array_size(filter_value); ++i)
      {
        lsx_log("ARRAY: ");
        for (uint32_t j = 0; j < filter_total_count; ++j)
        {
          on_count[i] += (filter_values[i][j] == 1);

          lsx_log("%u ", filter_values[i][j]);
        }
        lsx_log("\n");
      }
      for (uint32_t i = 0; i < array_size(filter_value); ++i)
      {
        uint32_t threshold =
          (uint32_t)((float)filter_total_count * (filter_value[i] ? 0.5f : 0.8f));
        filter_value[i] = 1;
        if (on_count[i] < threshold)
        {
          filter_value[i] = 0;
        }
        printf("COOUNT: %lu\n", on_count[i]);
        printf("threshold: %lu\n", threshold);
      }
      has_read_inputs = true;
      memset(filter_count, 0, sizeof(filter_count));
      memset(filter_indices, 0, sizeof(filter_indices));
      memset(filter_values, 0, sizeof(filter_values));

      uint8_t lamp_pins[] = { LED_I1, LED_I2, LED_I3 };
      for (uint32_t i = 0; i < array_size(lamp_pins); ++i)
      {
        uint8_t value = 127 * filter_value[i];
        led_set(lamp_pins[i], value, value, value);
      }

      uint8_t brightness = dali.config.scenes[get_input_index(filter_value)];
      if (brightness != last_sent_brightness)
      {
        last_sent_brightness = brightness;

        send_brightness_timer.time = lsx_get_millis();
        if (dali.config.blink_enabled && (brightness == 0))
        {
          if ((dali.current_brightness != 0) && !turn_off_sequence)
          {
            dali_send_brightness(1);
            turn_off_timer = timer_create_ms(dali.config.blink_duration * 1000);
            turn_off_sequence = true;
            turn_off_blink = true;
          }
        }
        else
        {
          turn_off_sequence = false;
          turn_off_blink = false;
        }
        if (!turn_off_sequence)
        {
          dali_send_brightness(dali.current_brightness = brightness);
        }
      }
    }

    if (dali.config.blink_enabled && turn_off_sequence)
    {
      uint32_t ms = lsx_get_millis();
      if (turn_off_blink)
      {
        float scale = (dali.fade_time <= 6) ? 0.36f : 0.56f;
        if ((ms - turn_off_timer.time) >=
            (uint32_t)((float)(dali.fade_time) * 1000.0f * scale))
        {
          dali_send_brightness(dali.current_brightness);
          turn_off_blink = false;
        }
      }
      turn_off_sequence = !timer_is_up_ms(turn_off_timer, ms);
      if (!turn_off_sequence)
      {
        dali_send_brightness(dali.current_brightness = 0);
      }
    }
    turn_off_blink = turn_off_blink && turn_off_sequence;

    if (!turn_off_blink && has_read_inputs &&
        timer_is_up_and_reset_ms(&send_brightness_timer, lsx_get_millis()))
    {
      dali_send_brightness(dali.current_brightness);
    }
    vTaskDelay(pdMS_TO_TICKS(32));

    delta_time = (float)(start - last_time) / 1000.0f;
    last_time = start;

    main_light_tick += (48 * delta_time);
    if (main_light_tick >= 127)
    {
      main_light_tick = 24;
    }
    led_set(LED_MAIN, 0, main_light_tick, 0);
  }
}

#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

void dali_initialize(nvs_t* scenes_nvs, dali_config_t config)
{
  led_strip_config_t strip_config = {};
  strip_config.strip_gpio_num = LED_PIN;
  strip_config.max_leds = NUM_LEDS;
  strip_config.led_model = LED_MODEL_WS2812;
  strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
  strip_config.flags.invert_out = false;

  led_strip_rmt_config_t rmt_config = {};
  rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
  rmt_config.resolution_hz = LED_STRIP_RMT_RES_HZ;
  rmt_config.mem_block_symbols = 0;
  rmt_config.flags.with_dma = 0;

  led_strip_new_rmt_device(&strip_config, &rmt_config, &dali.led_strip);

  for (uint32_t i = 0; i < NUM_LEDS; i++)
  {
    led_set_no_refresh_internal(i, 0, 0, 0);
  }
  led_strip_refresh(dali.led_strip);

  for (uint32_t i = 0; i <= 127; ++i)
  {
    led_set_no_refresh_internal(LED_MAIN, i, 0, 0);
    led_set_no_refresh_internal(LED_DALI, i, 0, 0);
    led_set_no_refresh_internal(LED_TIMER, i, 0, 0);
    led_strip_refresh(dali.led_strip);
    vTaskDelay(pdMS_TO_TICKS(8));
  }

  dali.config = config;
  dali.scene_nvs = scenes_nvs;

  dali_initialize_();

  xTaskCreateStatic(dali_task, "DALI Task", DALI_STACK_SIZE, NULL, 3, dali_stack,
                    &dali_stack_type);
}
