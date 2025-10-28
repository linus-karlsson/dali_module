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

  uint8_t current_brightness;

  uint8_t scenes[8];

  bool set_scenes;
  uint8_t temp_scenes[8];

  nvs_t nvs;
} dali_t;

static uint8_t get_input_index(bool* filter_values)
{
  uint8_t result = (uint8_t)(filter_values[0]);
  result <<= 1;
  result |= (uint8_t)(filter_values[1]);
  result <<= 1;
  result |= (uint8_t)(filter_values[2]);
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
uint8_t dali_scale(uint8_t procent);

bool dali_set_scenes(uint8_t* scenes)
{
  if (!dali.set_scenes)
  {
    memcpy(dali.temp_scenes, scenes, sizeof(dali.temp_scenes));
    dali.set_scenes = true;
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

uint8_t dali_query_(uint8_t command, uint8_t is_extended, bool* error)
{
  if (is_extended)
  {
    dali_transmit(0xC1, 6);
    lsx_delay_millis(delay_time);
  }
  dali_broadcast(command);
  lsx_delay_micro(10);
  return dali_receive(error);
}

uint8_t dali_query(uint8_t command, uint8_t is_extended, bool* error)
{
  uint8_t response = dali_query_(command, is_extended, error);
  if (error)
  {
    uint32_t total_number_of_tries = 3;
    uint32_t tries = 0;
    while ((*error) && (tries++ < total_number_of_tries))
    {
      *error = false;
      lsx_delay_millis(delay_time * 2);
      response = dali_query_(command, is_extended, error);
    }
  }
  return response;
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
  brightness = dali_scale(brightness) * (brightness != 0);

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
    lsx_log("Sending brightness\n");
    if ((brightness == level) && (brightness == 0))
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
    bool filter_value = lsx_gpio_read(pin) == LSX_GPIO_HIGH;
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

void dali_task(void* pvParameters)
{
  esp_task_wdt_add(NULL);

  dali_initialize_();

  bool turn_off_sequence = false;
  bool turn_off_blink = false;
  timer_ms_t turn_off_timer = timer_create_ms(10 * 1000);

  timer_ms_t send_brightness_timer = timer_create_ms(10000);

  const uint8_t dali_input_pins[] = { DALI_PIN_0, DALI_PIN_1, DALI_PIN_2 };
  uint8_t filter_count[3] = {};

  uint32_t tick = 1;

  while (true)
  {
    esp_task_wdt_reset();

    if (dali.set_scenes)
    {
      memcpy(dali.scenes, dali.temp_scenes, sizeof(dali.scenes));
      dali.set_scenes = false;
    }

    for (uint32_t i = 0; i < array_size(filter_count); ++i)
    {
#if 1
      if (!check_input(dali_input_pins[i], filter_value + i, filter_count + i))
      {
        memset(filter_count, 0, sizeof(filter_count));
        break;
      }
#else
      filter_count[i] += 1;
#endif
    }

    if ((filter_count[0] >= filter_total_count) &&
        (filter_count[1] >= filter_total_count) &&
        (filter_count[2] >= filter_total_count))
    {
      memset(filter_count, 0, sizeof(filter_count));

      uint8_t brightness = dali.scenes[get_input_index(filter_value)];
      if (brightness != dali.current_brightness)
      {
        send_brightness_timer.time = lsx_get_millis();
        if (brightness == 0)
        {
          if (dali.current_brightness != 0)
          {
            if(!turn_off_sequence)
            {
              dali_send_brightness(10);
              turn_off_timer.time = lsx_get_millis();
              turn_off_sequence = true;
              turn_off_blink = true;
            }
          }
        }
        else
        {
          turn_off_sequence = false;
          turn_off_blink = false;
        }
        if (!turn_off_sequence)
        {
          dali_send_brightness(brightness);
          dali.current_brightness = brightness;
        }
      }
    }

    if (turn_off_sequence)
    {
      printf("%lu turn off sequence\n", tick);
      uint32_t ms = lsx_get_millis();
      if (turn_off_blink)
      {
        if ((ms - turn_off_timer.time) >= (uint32_t)((float)(dali.fade_time) * 1000.0f * 0.52f))
        {
          printf("%lu blank\n", tick);
          dali_send_brightness(dali.current_brightness);
          turn_off_blink = false;
        }
      }
      turn_off_sequence = !timer_is_up_ms(turn_off_timer, ms);
      if (!turn_off_sequence)
      {
        dali.current_brightness = 0;
      }
    }
    turn_off_blink = turn_off_blink && turn_off_sequence;

    if (!turn_off_blink &&
        timer_is_up_and_reset_ms(&send_brightness_timer, lsx_get_millis()))
    {
      dali_send_brightness(dali.current_brightness);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    tick++;
  }
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

uint8_t dali_receive(bool* error)
{
  interupt_timemark = lsx_get_micro();
  pin_change_count = 0;

  if (error) *error = true;

  uint32_t start = lsx_get_micro();
  while (pin_change_count < 9)
  {
    if ((lsx_get_micro() - start) >= 100000)
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
  lsx_nvs_open(&dali.nvs, "DALI");

  lsx_nvs_get_uint8(&dali.nvs, "DFT", &dali.fade_time, 7);
  lsx_nvs_get_uint8(&dali.nvs, "DFR", &dali.fade_rate, 1);

  dali.fade_time = 4;
  dali.fade_rate = 1;

  bool error = false;
  uint8_t fade_time_rate = dali_query(DALI_QUERY_FADE_TIME, 0, &error);
  if (!error)
  {
    uint8_t current_fade_time = (fade_time_rate >> 4) & 0x0F;
    uint8_t current_fade_rate = fade_time_rate & 0x0F;
    if (dali.fade_time != current_fade_time)
    {
      printf("Fade time: %u\n", dali.fade_time);
      dali_set_DTR0(dali.fade_time);
      lsx_delay_millis(delay_time);
      dali_broadcast_twice(DALI_SET_FADE_TIME);
    }
    if (dali.fade_rate != current_fade_rate)
    {
      dali_set_DTR0(dali.fade_rate);
      lsx_delay_millis(delay_time);
      dali_broadcast_twice(DALI_SET_FADE_RATE);
    }
  }

  error = false;
  lsx_nvs_get_uint8(&dali.nvs, "DC", &dali.dimming_curve, DALI_DIMMING_LOGARITHMIC);
  uint8_t curve = dali_query(DALI_EX_QUERY_DIMMING_CURVE, 1, &error);
  if (!error && (dali.dimming_curve != curve))
  {
    dali_set_DTR0(dali.dimming_curve);
    lsx_delay_millis(delay_time);
    dali_transmit(0xC1, 6);
    dali_broadcast_twice(DALI_EX_SELECT_DIMMING_CURVE);
  }
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

  dali_set_saved_configuration();
}

uint8_t dali_scale(uint8_t procent)
{
  lsx_delay_millis(delay_time);

  bool error = false;
  uint8_t response = dali_query(DALI_QUERY_MIN_LEVEL, 0, &error);
  uint8_t min_brightness = error ? 170 : response;

  lsx_log("Error: %u\n", error);

  lsx_delay_millis(delay_time);

  error = false;
  response = dali_query(DALI_QUERY_MAX_LEVEL, 0, &error);
  uint8_t max_brightness = error ? 254 : response;

  lsx_log("Error: %u\n", error);

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

uint32_t light_control_light_source_to_tring(uint8_t light_source, char* result)
{
  const char* value = "Error";
  switch (light_source)
  {
    case 0:
    {
      value = "Low Pressure Fluorescent";
      break;
    }
    case 2:
    {
      value = "HID";
      break;
    }
    case 3:
    {
      value = "Low Voltage Halogen";
      break;
    }
    case 4:
    {
      value = "Incandescent";
      break;
    }
    case 6:
    {
      value = "LED";
      break;
    }
    case 7:
    {
      value = "OLED";
      break;
    }
    case 252:
    {
      value = "Other";
      break;
    }
    case 253:
    {
      value = "Unknown";
      break;
    }
    case 254:
    {
      value = "No Light Source";
      break;
    }
    case 255:
    {
      value = "Multiple";
      break;
    }
  }
  uint32_t len = strlen(value);
  memcpy(result, value, len);
  return len;
}

void dali_initialize(uint8_t* scenes)
{
  memcpy(dali.scenes, scenes, sizeof(dali.scenes));

  xTaskCreateStatic(dali_task, "DALI Task", DALI_STACK_SIZE, NULL, 3, dali_stack,
                    &dali_stack_type);
}
