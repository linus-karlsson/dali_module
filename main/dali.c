
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

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

typedef enum DaliCommand
{
  DALI_BRIGHTNESS_COMMAND,
  DALI_ON_COMMAND,
  DALI_OFF_COMMAND,
  DALI_QUERY_COMMAND,
  DALI_SET_COMMAND,
  DALI_ENABLE_CURRENT_PROTECTOR,
  DALI_DISABLE_CURRENT_PROTECTOR,
  DALI_RESET_COMMAND
} DaliCommand;

typedef struct dali_t
{
  QueueHandle_t queue;
  QueueHandle_t receive_queue;
  uint32_t delay;
  uint8_t tx_pin;
  uint8_t rx_pin;
  uint32_t on_the_same_level_count;

  uint8_t fade_rate;
  uint8_t fade_time;
  uint8_t dimming_curve;

  uint8_t short_address;

  uint8_t current_brightness;

  dali_config_t config;
  dali_config_t temp_config;

  bool set_config;

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
#if 1
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
#else
  // NOTE(Linus): Branchless version of the code above. In case of timing problems

  bool skip = (difference < (dali.delay + 100));
  bool ok = (difference < ((dali.delay + dali.delay) + 200));

  pin_change_count *= (ok && (pin_change_count < sizeof(pin_change_buffer)));

  pin_change_buffer[pin_change_count] = esp_gpio_read(dali.rx_pin);

  pin_change_count += (ok && (!skip));

  interupt_timemark += (difference * (!skip));
  interupt_timemark -= (200 * (!ok));
#endif
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
  uint32_t total_number_queries = 6;
  uint32_t count = 1;
  uint8_t responses[6] = {};

  if (error) *error = true;

  for (uint32_t i = 0; i < total_number_queries; ++i)
  {
    bool error_temp = false;
    responses[i] = dali_query1(address, command, is_extended, &error_temp);
    if (error_temp)
    {
      return 0;
    }
#if 1
    if ((i > 0) && (responses[i - 1] == responses[i]) && ((++count) >= 4))
    {
      if (error) *error = false;
      lsx_log("Query: %u\n", responses[i]);
      return responses[i];
    }
    else
    {
      count = 1;
    }
#endif
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  uint32_t correct_count = 0;
  uint8_t current_response = 0;
  for (uint32_t i = 0; i < total_number_queries; ++i)
  {
    correct_count = 0;
    current_response = responses[i];
    for (uint32_t j = 0; j < total_number_queries; ++j)
    {
      if ((current_response == responses[j]) && ((++correct_count) >= 4))
      {
        if (error) *error = false;
        lsx_log("Query: %u\n", current_response);
        return current_response;
      }
    }
  }
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
      lsx_log("off\n");
      dali_turn_off_light_();
    }
    else
    {
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

static uint8_t filter_total_count = 6;

static bool check_input(uint8_t pin, bool* filter_value_out,
                        uint8_t* filter_count_out)
{
  uint8_t filer_count = *filter_count_out;
  if (filer_count < filter_total_count)
  {
#if 0
    bool filter_value = lsx_gpio_read(pin) == LSX_GPIO_HIGH;
#else
    bool filter_value = (dali.config.scenes[7] >> pin) & 0x01;
#endif
    if (filer_count == 0)
    {
      (*filter_value_out) = filter_value;
    }
    else if (filter_value != (*filter_value_out))
    {
      return false;
    }
    (*filter_count_out) = filer_count + 1;
  }
  return true;
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

  dali.tx_pin = 6;
  dali.rx_pin = 5;
  dali.delay = 833 / 2;
  dali.on_the_same_level_count = 0;
  dali.current_brightness = 0;

  lsx_gpio_config(dali.tx_pin, LSX_GPIO_MODE_OUTPUT, LSX_GPIO_INTR_DISABLE, false,
                  false);
  lsx_gpio_write(dali.tx_pin, LSX_GPIO_LOW);

  lsx_gpio_config(dali.rx_pin, LSX_GPIO_MODE_INPUT, LSX_GPIO_INTR_ANYEDGE, false,
                  false);
  lsx_gpio_write(dali.rx_pin, LSX_GPIO_LOW);

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
  if(min_brightness_out) (*min_brightness_out) = min_brightness;

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
  dali_broadcast_twice(DALI_OFF);
}

void dali_set_brightness_(uint8_t brightness)
{
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
}

void dali_task(void* pvParameters)
{
  esp_task_wdt_add(NULL);

  bool turn_off_sequence = false;
  bool turn_off_blink = false;
  timer_ms_t turn_off_timer = timer_create_ms(dali.config.blink_duration * 1000);
  timer_ms_t send_brightness_timer = timer_create_ms(30000);
  timer_ms_t check_input_timer = timer_create_ms(500);

  const uint8_t dali_input_pins[] = { DALI_PIN_0, DALI_PIN_1, DALI_PIN_2 };
  bool filter_value[3] = {};
  uint8_t filter_count[3] = {};

  uint8_t last_sent_brightness = 0;

  while (true)
  {
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

    if (timer_is_up_and_reset_ms(&check_input_timer, lsx_get_millis()))
    {
      for (uint32_t i = 0; i < array_size(filter_count); ++i)
      {
#if 0
      if (!check_input(dali_input_pins[i], filter_value + i, filter_count + i))
      {
        memset(filter_count, 0, sizeof(filter_count));
        break;
      }
#else
        if (!check_input(i, filter_value + i, filter_count + i))
        {
          memset(filter_count, 0, sizeof(filter_count));
          break;
        }
#endif
      }
    }

    if ((filter_count[0] >= filter_total_count) &&
        (filter_count[1] >= filter_total_count) &&
        (filter_count[2] >= filter_total_count))
    {
      memset(filter_count, 0, sizeof(filter_count));

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
        float scale = (dali.fade_time <= 6) ? 0.32f : 0.54f;
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

    if (!turn_off_blink &&
        timer_is_up_ms(send_brightness_timer, lsx_get_millis()))
    {
      dali_send_brightness(dali.current_brightness);

      send_brightness_timer = timer_create_ms(10000);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void dali_initialize(nvs_t* scenes_nvs, dali_config_t config)
{
  dali.config = config;
  dali.scene_nvs = scenes_nvs;
  //dali.current_brightness = dali.config.scenes[get_input_index(filter_value)]

  dali_initialize_();

  xTaskCreateStatic(dali_task, "DALI Task", DALI_STACK_SIZE, NULL, 3, dali_stack,
                    &dali_stack_type);
}
