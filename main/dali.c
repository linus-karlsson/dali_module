#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/ledc.h>
#include <led_strip.h>
#include <led_strip.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>

#include <esp_task_wdt.h>
#include <string.h>

#include "dali.h"
#include "util.h"
#include "platform.h"
#include "pin_define.h"
#include "web/web.h"

#define DALI_MESSAGE_LENGTH 6
#define DALI_UART_NUMBER    1

#define DALI_STACK_SIZE          4096
#define DALI_MESSAGE_TOTAL_COUNT 10

#define DALI_RECIEVE_TOTAL_COUNT 1024

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

  uint32_t short_address_count;
  uint8_t short_address[64];

  uint8_t current_brightness;

  dali_config_t config;
  dali_config_t temp_config;

  bool set_config;

  led_strip_handle_t led_strip;
  led_rgb_t leds[NUM_LEDS];

  nvs_t nvs;
  nvs_t* scene_nvs;
} dali_t;

static StackType_t dali_stack[DALI_STACK_SIZE] = {};
static StaticTask_t dali_stack_type = {};

static dali_t dali = {};

static const int delay_time = 15;

static const uint8_t dali_input_pins[] = { DALI_PIN_0, DALI_PIN_1, DALI_PIN_2 };
static const uint32_t g_total_filters = 1;
static const uint32_t g_sample_total_count = 600;

static const char* g_rmt_tag = "dali_rmt";

static rmt_channel_handle_t g_rmt_tx_channel;
static rmt_channel_handle_t g_rmt_rx_channel;

static rmt_encoder_handle_t g_rmt_encoder;

bool rmt_rx_done_callback(rmt_channel_handle_t rx_chan,
                          const rmt_rx_done_event_data_t* edata, void* user_ctx)
{
  QueueHandle_t receive_queue = (QueueHandle_t)user_ctx;
  xQueueSendFromISR(receive_queue, edata, NULL);
  return false;
}

QueueHandle_t receive_queue = NULL;

rmt_rx_event_callbacks_t callbacks = {
  .on_recv_done = rmt_rx_done_callback,
};

static uint8_t get_input_index(bool* filter_values)
{
  uint8_t result = (uint8_t)(filter_values[2]);
  result <<= 1;
  result |= (uint8_t)(filter_values[1]);
  result <<= 1;
  result |= (uint8_t)(filter_values[0]);
  return min(result, 7);
}

static void dali_initialize_rmt(void)
{
  receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));

  rmt_tx_channel_config_t tx_cfg = {
    .gpio_num = DALI_TX,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = 64,
    .trans_queue_depth = 1,
  };
  rmt_new_tx_channel(&tx_cfg, &g_rmt_tx_channel);
  rmt_enable(g_rmt_tx_channel);

  rmt_rx_channel_config_t rx_cfg = {
    .gpio_num = DALI_RX,
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 1000000,
    .mem_block_symbols = 64,
  };
  rmt_new_rx_channel(&rx_cfg, &g_rmt_rx_channel);
  rmt_rx_register_event_callbacks(g_rmt_rx_channel, &callbacks, receive_queue);
  rmt_enable(g_rmt_rx_channel);

  rmt_copy_encoder_config_t encoder_config = {};
  rmt_new_copy_encoder(&encoder_config, &g_rmt_encoder);
}
static void dali_rmt_append_bit(rmt_symbol_word_t* rmt_buffer, uint32_t index,
                                uint8_t bit)
{
  rmt_symbol_word_t* current_symbol = rmt_buffer + index;
  current_symbol->duration0 = dali.delay;
  current_symbol->duration1 = dali.delay;
  current_symbol->level0 = bit;
  current_symbol->level1 = !bit;
}

void dali_transmit_(uint8_t address, uint8_t command)
{
  rmt_symbol_word_t frame[32] = {};
  uint32_t index = 0;

  dali_rmt_append_bit(frame, index++, 1);

  for (int32_t i = 7; i >= 0; i--)
    dali_rmt_append_bit(frame, index++, (address >> i) & 0x01);

  for (int32_t i = 7; i >= 0; i--)
    dali_rmt_append_bit(frame, index++, (command >> i) & 0x01);

  rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
  rmt_transmit(g_rmt_tx_channel, g_rmt_encoder, frame,
               index * sizeof(rmt_symbol_word_t), &tx_cfg);
  rmt_tx_wait_all_done(g_rmt_tx_channel, 100);
  lsx_gpio_write(dali.tx_pin, LSX_GPIO_LOW);
}

void dali_transmit(uint8_t address, uint8_t command)
{
  rmt_enable(g_rmt_tx_channel);
  dali_transmit_(address, command);
  rmt_disable(g_rmt_tx_channel);
}

static rmt_symbol_word_t raw_symbols[64] = {};

bool dali_read_response(uint32_t timeout_ms, uint8_t* response_out, bool* any_symbols)
{
  lsx_delay_micro(2000);

  rmt_receive_config_t receive_config = {
    .signal_range_min_ns = 2000,
    .signal_range_max_ns = 4000000,
  };
  rmt_receive(g_rmt_rx_channel, raw_symbols, sizeof(raw_symbols), &receive_config);

  rmt_rx_done_event_data_t rx_data = {};
  BaseType_t queue_result =
    xQueueReceive(receive_queue, &rx_data, pdMS_TO_TICKS(timeout_ms));

  bool result = false;

  uint8_t response = 0;

  if (queue_result == pdPASS)
  {
    if (rx_data.num_symbols <= 16)
    {
      uint32_t count = 0;
      uint8_t values[32] = {};
      for (uint32_t i = 0; i < rx_data.num_symbols; ++i)
      {
        rmt_symbol_word_t symbol = rx_data.received_symbols[i];
        if (symbol.duration0 > 200)
        {
          values[count++] = symbol.level0;
          if (symbol.duration0 > 600)
          {
            values[count++] = symbol.level0;
          }
        }
        if (symbol.duration1 > 200)
        {
          values[count++] = symbol.level1;
          if (symbol.duration1 > 600)
          {
            values[count++] = symbol.level1;
          }
        }
      }
      if (count == 17)
      {
        values[count++] = 0;
      }
      if (count == 18)
      {
        for (uint32_t i = 2; i < 18; i += 2)
        {
          response <<= 1;
          if ((values[i] == 1) && (values[i + 1] == 0))
          {
            response |= 1;
          }
        }
        result = true;
      }
      else
      {
#if !defined(LSX_RELEASE)
        printf("SYMBOLS: %u\n", rx_data.num_symbols);
        for (uint32_t i = 0; i < rx_data.num_symbols; ++i)
        {
          printf("Level 0: %u\n", rx_data.received_symbols[i].level0);
          printf("Duration 0: %u\n", rx_data.received_symbols[i].duration0);
          printf("Level 1: %u\n", rx_data.received_symbols[i].level1);
          printf("Duration 1: %u\n", rx_data.received_symbols[i].duration1);
        }
#endif
      }
      if (any_symbols)
      {
        (*any_symbols) = (result || (rx_data.num_symbols >= 4));
      }
    }
  }

  if (response_out) (*response_out) = response;

  return result;
}

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

void light_control_add_interrupt(void)
{
  lsx_gpio_add_pin_interrput(dali.rx_pin, pin_change, NULL);
}

void light_control_remove_interrupt(void)
{
  lsx_gpio_remove_pin_interrput(dali.rx_pin);
}

static inline void dali_broadcast_twice(uint8_t command)
{
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST, command);
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST, command);
  lsx_delay_millis(delay_time);
}

uint8_t dali_query_(uint8_t address, uint8_t command, bool* error_out,
                    bool* any_response)
{
  lsx_delay_millis(delay_time);

  uint8_t result = 0;

  rmt_enable(g_rmt_tx_channel);
  rmt_enable(g_rmt_rx_channel);

  dali_transmit_(address, command);
  bool success = dali_read_response(50, &result, any_response);

  rmt_disable(g_rmt_rx_channel);
  rmt_disable(g_rmt_tx_channel);

  if (error_out) (*error_out) = !success;
  return result;
}

uint8_t dali_query1(uint8_t address, uint8_t command, bool* error)
{
  uint8_t response = dali_query_(address, command, error, NULL);
  if (error)
  {
    const uint32_t total_number_of_tries = 2;
    uint32_t tries = 0;
    while ((*error) && (tries++ < total_number_of_tries))
    {
      *error = false;
      lsx_delay_millis(delay_time * 2);
      response = dali_query_(address, command, error, NULL);
    }
  }
  return response;
}

uint8_t dali_query0(uint8_t address, uint8_t command, bool* error)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(32));

  uint32_t total_number_queries = 4;
  uint32_t count = 0;
  uint8_t responses[4] = {};

  if (error) *error = true;

  for (uint32_t i = 0; i < total_number_queries; ++i)
  {
    bool error_temp = false;
    uint8_t temp_response = dali_query1(address, command, &error_temp);
    if (!error_temp)
    {
      responses[count++] = temp_response;
    }
    vTaskDelay(pdMS_TO_TICKS(16));
  }
  if (count >= 3)
  {
    uint32_t correct_count = 0;
    uint8_t current_response = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
      correct_count = 0;
      current_response = responses[i];
      for (uint32_t j = 0; j < count; ++j)
      {
        if ((current_response == responses[j]) && ((++correct_count) >= 3))
        {
          if (error) *error = false;
          lsx_log("Query: %u\n", current_response);

          led_set(LED_DALI, 0, 127, 0);
          vTaskDelay(pdMS_TO_TICKS(32));

          return current_response;
        }
      }
    }
  }
  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(32));
  return 0;
}

uint8_t dali_query(uint8_t command, bool* error_out)
{
  uint8_t result = 0;
  if (dali.short_address_count == 0)
  {
    result = dali_query0(DALI_BROADCAST, command, error_out);
  }
  else
  {
    bool success = false;
    uint8_t max = 0;
    for (uint32_t i = 0; i < dali.short_address_count; ++i)
    {
      bool error = false;
      uint8_t current =
        dali_query0((dali.short_address[i] << 1) | 0x01, command, &error);
      if (!error && (current >= max))
      {
        max = current;
        success = true;
      }
    }
    if (!success)
    {
      lsx_log("BAD\n");
      result = dali_query0(DALI_BROADCAST, command, error_out);
    }
    else
    {
      if (error_out) *error_out = false;
      result = max;
    }
  }
  return result;
}

uint8_t dali_scale(uint8_t procent, uint8_t* min_brightness_out)
{
#if 1
  bool error = false;
  uint8_t response = dali_query(DALI_QUERY_MIN_LEVEL, &error);
  uint8_t min_brightness = error ? 1 : response;
  lsx_log("Min: %u\n", min_brightness);
#else
  uint8_t min_brightness = 1;
#endif
  if (min_brightness_out) (*min_brightness_out) = min_brightness;
  uint8_t max_brightness = 254;
  return min_brightness + (((max_brightness - min_brightness) * procent) / 100);
}

void dali_turn_off_light_(void)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(32));

  dali_broadcast_twice(DALI_OFF);

  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(32));
}

void dali_set_brightness_(uint8_t brightness)
{
  led_set(LED_DALI, 0, 0, 127);
  vTaskDelay(pdMS_TO_TICKS(32));

#if 1
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
#else
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
  dali_transmit(DALI_BROADCAST_DP, brightness);
  lsx_delay_millis(delay_time);
#endif

  led_set(LED_DALI, 0, 127, 0);
  vTaskDelay(pdMS_TO_TICKS(32));
}

void dali_send_brightness(uint8_t brightness)
{
  uint8_t min_brightness = 1;

  brightness = dali_scale(brightness, &min_brightness) * (brightness != 0);

#if 1
  lsx_delay_millis(delay_time);
  bool error = false;
  uint8_t level = dali_query(DALI_QUERY_ACTUAL_OUTPUT, &error);
  lsx_log("Error: %u\n", error);
#else
  bool error = true;
  uint8_t level = 0;
#endif

  if (!error && (brightness != level))
  {
    dali.on_the_same_level_count = 0;
  }
  if (error || ((++dali.on_the_same_level_count) <= 8))
  {
    lsx_log("Sending brightness: %u\n", brightness);
    if ((level <= min_brightness) && (brightness == 0))
    {
      if (error && ((++dali.on_0) < 6))
      {
        dali_set_brightness_(brightness);
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

#if 0
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
#endif

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
  return dali_receive_(error, 40000);
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

void dali_short_scan(void)
{
  for (uint8_t i = 0; i < array_size(dali.short_address); ++i)
  {
    bool any_response = false;
    dali_query_((i << 1) | 0x01, 0x90, NULL, &any_response);
    if (any_response)
    {
      lsx_log("Found Short address: %u\n", i);
      dali.short_address[dali.short_address_count++] = i;
      break;
    }
  }
}

void dali_scan(void)
{
  int32_t low_address = 0;
  int32_t high_address = 0x00FFFFFF;
  int32_t current_address = (int32_t)(low_address + high_address) / 2;

  int32_t last_address = high_address;
  uint8_t short_address = 0;

  uint32_t count = 0;
  bool still_scanning = true;
  while (still_scanning &&
         (dali.short_address_count < array_size(dali.short_address)))
  {
    esp_task_wdt_reset();

    while ((high_address - low_address) > 0)
    {
      // lsx_log("Low: %ld\n", low_address);
      // lsx_log("High: %ld\n", high_address);
      bool any_response = false;
      for (uint32_t i = 0; i < 1; ++i)
      {
        lsx_delay_millis(delay_time);
        dali_transmit(0xB1, (current_address >> 16) & 0xFF);
        lsx_delay_millis(delay_time);
        dali_transmit(0xB3, (current_address >> 8) & 0xFF);
        lsx_delay_millis(delay_time);
        dali_transmit(0xB5, current_address & 0xFF);

        lsx_delay_millis(delay_time);

        dali_query_(0xA9, 0, NULL, &any_response);
        if (any_response)
        {
          break;
        }
      }

      if (any_response)
      {
        high_address = current_address;
      }
      else
      {
        low_address = current_address + 1;
      }

      current_address = (int32_t)(low_address + high_address) / 2;
    }

    if (high_address != 0x00FFFFFF)
    {
      lsx_delay_millis(delay_time);
      dali_transmit(0xB1, (current_address >> 16) & 0xFF);
      lsx_delay_millis(delay_time);
      dali_transmit(0xB3, (current_address >> 8) & 0xFF);
      lsx_delay_millis(delay_time);
      dali_transmit(0xB5, current_address & 0xFF);

      bool error = true;
      uint8_t temp_short_address = dali_query0(0xBB, 0, &error) >> 1;

      printf("Found short address: %u\n", temp_short_address);

      bool found = true;
      if (temp_short_address < array_size(dali.short_address))
      {
        found = false;
        for (uint32_t i = 0; i < dali.short_address_count; ++i)
        {
          if (temp_short_address == dali.short_address[i])
          {
            printf("Dubplicate found\n");
            found = true;
            break;
          }
        }
        if (!found)
        {
          short_address = temp_short_address;
        }
      }
      while (found)
      {
        found = false;
        for (uint32_t i = 0; i < dali.short_address_count; ++i)
        {
          if (short_address == dali.short_address[i])
          {
            found = true;
            short_address++;
          }
        }
      }

      if (short_address >= array_size(dali.short_address))
      {
        still_scanning = false;
        break;
      }

      lsx_delay_millis(delay_time);
      dali_transmit(0xB7, (short_address << 1) | 0x01);
      lsx_delay_millis(delay_time);

      temp_short_address = dali_query0(0xBB, 0, &error) >> 1;
      printf("Short address after set: %u\n", temp_short_address);

      dali_transmit(0xAB, 0);
      lsx_delay_millis(delay_time);

      dali.short_address[dali.short_address_count++] = short_address;

      lsx_log("Found address: %ld\n", current_address);

      low_address = 0;
      high_address = 0x00FFFFFF;
      current_address = (low_address + high_address) / 2;

      short_address++;
    }
    else
    {
      still_scanning = false;
    }
  }

  lsx_delay_millis(delay_time);
  dali_transmit(0xA1, 0);
  lsx_delay_millis(delay_time);

  vTaskDelay(pdMS_TO_TICKS(600));

  lsx_log("Short address count: %lu\n", dali.short_address_count);
}

typedef struct dali_send_t
{
  uint8_t done;
  uint8_t bytes[2];
} dali_send_t;

static lsx_timer_handle_t g_dali_send_timer;
static volatile dali_send_t g_dali_send = {};

void timer_send_dali_command(void* p_arguments)
{
  dali_transmit(g_dali_send.bytes[0], g_dali_send.bytes[1]);
  g_dali_send.done = true;
}

void dali_initialize_(void)
{
  lsx_log("Dali init\n");

  dali.tx_pin = DALI_TX;
  dali.rx_pin = DALI_RX;
  dali.delay = 833 / 2;
  dali.on_the_same_level_count = 0;
  dali.current_brightness = 0;

  dali_initialize_rmt();

  vTaskDelay(pdMS_TO_TICKS(600));

#if 0
  lsx_gpio_install_interrupt_service();
  lsx_gpio_add_pin_interrput(dali.rx_pin, pin_change, NULL);
#endif

#if 1

  lsx_delay_millis(delay_time);
  dali_transmit(0xA1, 0);
  lsx_delay_millis(delay_time);

  vTaskDelay(pdMS_TO_TICKS(600));

  // dali_short_scan();

#if 1
  lsx_delay_millis(delay_time);
  dali_transmit(0xA5, 0);
  lsx_delay_millis(delay_time);
  dali_transmit(0xA5, 0);
  lsx_delay_millis(delay_time);

#if 1
  lsx_delay_millis(delay_time);
  dali_transmit(0xA7, 0);
  lsx_delay_millis(delay_time);
  dali_transmit(0xA7, 0);
  lsx_delay_millis(delay_time);
#endif

  dali_scan();

#endif

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

#if 0
  srand(lsx_get_micro());
  lsx_delay_millis(delay_time);
  dali_send_brightness(0);
  vTaskDelay(pdMS_TO_TICKS(5000));
  dali_send_brightness(rand() % 100);
  vTaskDelay(pdMS_TO_TICKS(5000));
  //esp_restart();
#endif
#endif
}

#if 0
void dali_transmit(uint8_t address, uint8_t command)
{
  g_dali_send.done = 0;
  g_dali_send.bytes[0] = address;
  g_dali_send.bytes[1] = command;
  lsx_timer_start(g_dali_send_timer, 0, false);
  while (!g_dali_send.done)
  {
  }
}
#endif

void dali_task(void* pvParameters)
{
  esp_task_wdt_add(NULL);

  dali_initialize_();

  vTaskDelay(pdMS_TO_TICKS(100));

  bool turn_off_sequence = false;
  bool turn_off_blink = false;
  bool has_read_inputs = false;
  timer_ms_t turn_off_timer = timer_create_ms(dali.config.blink_duration * 1000);
  timer_ms_t send_brightness_timer = timer_create_ms(4000);

  const uint32_t random_delay_max = 4;
  uint32_t random_delay = random_delay_max;

  uint8_t filter_samples[3][g_sample_total_count] = {};

  uint32_t filter_count = 0;
  uint32_t filter_index = 0;
  bool filters[3][g_total_filters] = {};

  bool filter_value[3] = {};

  timer_ms_t log_values_timer = timer_create_ms(4000);

  uint8_t last_sent_brightness = 255;

  uint8_t main_light_tick = 24;
  uint32_t last_time = lsx_get_millis();
  float delta_time = 0.0f;

  uint32_t index_dali = 0;

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

    for (uint32_t i = 0; i < g_sample_total_count; ++i)
    {
      for (uint32_t j = 0; j < array_size(dali_input_pins); ++j)
      {
        filter_samples[j][i] = lsx_gpio_read(dali_input_pins[j]) == LSX_GPIO_LOW;
      }
      lsx_delay_millis(1);
    }

    uint32_t on_count[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < array_size(filter_value); ++i)
    {
      for (uint32_t j = 0; j < g_sample_total_count; ++j)
      {
        on_count[i] += (filter_samples[i][j] == 1);
      }
    }

    for (uint32_t i = 0; i < array_size(filter_value); ++i)
    {
      uint32_t threshold =
        (uint32_t)((float)g_sample_total_count * (filter_value[i] ? 0.5f : 0.75f));
      filters[i][filter_index] = 1;
      if (on_count[i] < threshold)
      {
        filters[i][filter_index] = 0;
      }
    }
    filter_index = plus_one_wrap(filter_index, g_total_filters);

    memset(filter_samples, 0, sizeof(filter_samples));

    if (++filter_count >= g_total_filters)
    {
      --filter_count;

      has_read_inputs = true;

      uint32_t max_different = 0;
      for (uint32_t i = 0; i < array_size(filter_value); ++i)
      {
        uint32_t true_count = 0;
        for (uint32_t j = 0; j < g_total_filters; ++j)
        {
          true_count += (uint8_t)(filters[i][j]);
        }

        if (true_count == 0)
        {
          filter_value[i] = 0;
        }
        else if (true_count == g_total_filters)
        {
          filter_value[i] = 1;
        }
        else
        {
          uint32_t diff = true_count;
          if (filter_value[i])
          {
            diff = g_total_filters - true_count;
          }
          if (diff > max_different)
          {
            max_different = diff;
          }
        }
      }

      float diff_percent = ((float)max_different) / ((float)g_total_filters);
      uint8_t value = 127 * diff_percent;
      led_set(LED_TIMER, value, 127 - value, 0);

      uint8_t lamp_pins[] = { LED_I1, LED_I2, LED_I3 };
      for (uint32_t i = 0; i < array_size(lamp_pins); ++i)
      {
        uint8_t value = 127 * filter_value[i];
        led_set(lamp_pins[i], value, value, value);
      }

      uint8_t index = get_input_index(filter_value);
      uint8_t brightness = 254; // dali.config.scenes[get_input_index(filter_value)];
      for (uint32_t i = 0; i < dali.short_address_count; ++i)
      {
        if (index == dali.short_address[i])
        {
          lsx_delay_millis(delay_time);
          dali_transmit(dali.short_address[i] << 1, 254);
          lsx_delay_millis(delay_time);
          dali_transmit(dali.short_address[i] << 1, 254);
          lsx_delay_millis(delay_time);
        }
        else
        {
          lsx_delay_millis(delay_time);
          dali_transmit(dali.short_address[i] << 1, 0);
          lsx_delay_millis(delay_time);
          dali_transmit(dali.short_address[i] << 1, 0);
          lsx_delay_millis(delay_time);
        }
        vTaskDelay(pdMS_TO_TICKS(300));
      }
#if 0
      if (brightness != last_sent_brightness)
      {
        last_sent_brightness = brightness;

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

        send_brightness_timer.time = lsx_get_millis();
      }
#endif
    }

    if (timer_is_up_and_reset_ms(&log_values_timer, lsx_get_millis()))
    {
      for (uint32_t i = 0; i < array_size(filter_value); ++i)
      {
        lsx_log("Filters %lu: { ", i);
        for (uint32_t j = 0; j < (g_total_filters - 1); ++j)
        {
          lsx_log("%u, ", filters[i][j]);
        }
        lsx_log("%u }\n", filters[i][g_total_filters - 1]);
      }
      lsx_log("\n");

      lsx_log("Filter values: ");
      for (uint32_t i = 0; i < array_size(filter_value); ++i)
      {
        lsx_log("%u ", filter_value[i]);
      }
      lsx_log("\n\n");
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
        timer_is_up_ms(send_brightness_timer, lsx_get_millis()))
    {
#if 0
      lsx_delay_millis(delay_time);
      dali_transmit(dali.short_address[index_dali] << 1, 0);
      lsx_delay_millis(delay_time);
      dali_transmit(dali.short_address[index_dali] << 1, 0);
      lsx_delay_millis(delay_time);
      index_dali = plus_one_wrap(index_dali, dali.short_address_count);

      vTaskDelay(pdMS_TO_TICKS(4000));
      lsx_delay_millis(delay_time);
      dali_transmit(dali.short_address[index_dali] << 1, 254);
      lsx_delay_millis(delay_time);
      dali_transmit(dali.short_address[index_dali] << 1, 254);
      lsx_delay_millis(delay_time);

#endif
      // dali_send_brightness(dali.current_brightness);
      send_brightness_timer.time = lsx_get_millis();
    }

    vTaskDelay(
      pdMS_TO_TICKS(32 + (random_delay * (random_delay <= random_delay_max))));
    random_delay = rand() % random_delay_max;

    delta_time = (float)(start - last_time) / 1000.0f;
    last_time = start;

    main_light_tick += (32 * delta_time);
    if (main_light_tick >= 127)
    {
      main_light_tick = min(main_light_tick - 127, 24);
    }
    led_set(LED_MAIN, 0, main_light_tick, 0);
  }
}

#define LED_STRIP_RMT_RES_HZ (10 * 1000 * 1000)

void dali_led_initialize(void)
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
    lsx_delay_millis(8);
  }
}

void dali_initialize(nvs_t* scenes_nvs, dali_config_t config)
{
  dali.config = config;
  dali.scene_nvs = scenes_nvs;

  xTaskCreateStatic(dali_task, "DALI Task", DALI_STACK_SIZE, NULL, 3, dali_stack,
                    &dali_stack_type);
}
