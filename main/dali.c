#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/ledc.h>
#include <led_strip.h>
#include <led_strip.h>
#include <driver/rmt_tx.h>

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
static const uint32_t g_total_filters = 6;
static const uint32_t g_sample_total_count = 600;

#if 0
#include "driver/rmt_tx.h"
#include "esp_log.h"

#define DALI_TX_PIN  17
#define DALI_BIT_US  833
#define DALI_HALF_US (DALI_BIT_US / 2)

static const char *TAG = "dali_rmt";

rmt_channel_handle_t dali_tx_channel;

void dali_init_tx(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = DALI_TX_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,   // 1 tick = 1 µs
        .mem_block_symbols = 128,
        .trans_queue_depth = 1,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_cfg, &dali_tx_channel));
    ESP_ERROR_CHECK(rmt_enable(dali_tx_channel));
}

static void append_dali_bit(rmt_symbol_word_t *buf, int *idx, uint8_t bit)
{
    if (bit)
    {
        // Logic 1: HIGH then LOW
        buf[*idx].level0 = 1;
        buf[*idx].duration0 = DALI_HALF_US;
        buf[*idx].level1 = 0;
        buf[*idx].duration1 = DALI_HALF_US;
    }
    else
    {
        // Logic 0: LOW then HIGH
        buf[*idx].level0 = 0;
        buf[*idx].duration0 = DALI_HALF_US;
        buf[*idx].level1 = 1;
        buf[*idx].duration1 = DALI_HALF_US;
    }
    (*idx)++;
}

void dali_send_frame(uint8_t address, uint8_t command)
{
    rmt_symbol_word_t frame[40];
    int idx = 0;

    // Start bit = 1
    append_dali_bit(frame, &idx, 1);

    // Address byte
    for (int i = 7; i >= 0; i--)
        append_dali_bit(frame, &idx, (address >> i) & 1);

    // Command byte
    for (int i = 7; i >= 0; i--)
        append_dali_bit(frame, &idx, (command >> i) & 1);

    // Two stop bits = HIGH all the time
    // In DALI this means last half-bit stays high, then two full high bits
    frame[idx].level0 = 1;
    frame[idx].duration0 = DALI_BIT_US * 2;  // 2 stop bits = 2 * 833 µs
    frame[idx].level1 = 1;
    frame[idx].duration1 = 0;
    idx++;

    // Transmit entire frame in one go
    rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
    ESP_ERROR_CHECK(rmt_transmit(dali_tx_channel, frame, idx * sizeof(rmt_symbol_word_t), &tx_cfg));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(dali_tx_channel, portMAX_DELAY));
}

#include "driver/rmt_rx.h"

#define DALI_RX_PIN    16
#define DALI_RX_CLK    1000000   // 1 MHz = 1 µs resolution

rmt_channel_handle_t dali_rx_channel;

void dali_init_rx(void)
{
    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = DALI_RX_PIN,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = DALI_RX_CLK,
        .mem_block_symbols = 64,
        .signal_range_min_ns = 200000,  // ignore glitches <200 µs
        .signal_range_max_ns = 2000000, // max pulse 2 ms
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_cfg, &dali_rx_channel));
    ESP_ERROR_CHECK(rmt_enable(dali_rx_channel));
}

void dali_read_response()
{
    rmt_receive_config_t rx_config = {
        .signal_range_min_ns = 200000,
        .signal_range_max_ns = 2000000,
    };
    rmt_symbol_word_t rx_buf[128];
    size_t n_symbols = 0;

    // Start hardware capture (non-blocking)
    ESP_ERROR_CHECK(rmt_receive(dali_rx_channel, rx_buf, sizeof(rx_buf), &rx_config));

    // Wait until idle or timeout
    ESP_ERROR_CHECK(rmt_rx_wait_all_done(dali_rx_channel, 10 / portTICK_PERIOD_MS));

    // Retrieve captured data
    ESP_ERROR_CHECK(rmt_receive_done(dali_rx_channel, &rx_buf, &n_symbols, 0));

    // Now decode Manchester from rx_buf[].duration0/duration1 and level0/level1
}

#include "driver/rmt_rx.h"
#include <stdio.h>
#include <stdint.h>

#define DALI_HALF_US 416   // expected half-bit
#define DALI_TOLERANCE 150 // µs tolerance

int dali_decode_response(const rmt_symbol_word_t *symbols, size_t num_symbols, uint8_t *out_bytes)
{
    // 1. Flatten into (level, duration) stream
    int halfbits[128];
    int hb_count = 0;

    for (size_t i = 0; i < num_symbols; i++) {
        halfbits[hb_count++] = symbols[i].level0;
        halfbits[hb_count++] = symbols[i].level1;
    }

    // 2. Compress durations to uniform half-bits
    // (Skip if your signal is clean; durations near 416 µs)

    // 3. Manchester decode into bits
    int bit_count = 0;
    uint8_t bits[64];
    for (int i = 0; i + 1 < hb_count; i += 2) {
        int first = halfbits[i];
        int second = halfbits[i + 1];
        if (first == 0 && second == 1)
            bits[bit_count++] = 0;
        else if (first == 1 && second == 0)
            bits[bit_count++] = 1;
        else {
            // invalid transition (noise or framing)
            // optional: try to resync here
        }
    }

    // 4. Remove start bit (first bit)
    if (bit_count < 9)
        return 0; // too short
    int data_bits = bit_count - 1;

    // 5. Pack bits into bytes (MSB first)
    int byte_count = 0;
    for (int i = 1; i < bit_count; i += 8) {
        uint8_t b = 0;
        for (int j = 0; j < 8 && (i + j) < bit_count; j++) {
            b = (b << 1) | bits[i + j];
        }
        out_bytes[byte_count++] = b;
    }

    return byte_count; // number of valid bytes
}

#endif

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

void send_dali_command(uint8_t byte0, uint8_t byte1);

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

uint8_t dali_query_(uint8_t address, uint8_t command, bool* error)
{
  lsx_delay_millis(delay_time);
  pin_change_count = 0;
  dali_transmit(address, command);
  return dali_receive(error);
}

uint8_t dali_query1(uint8_t address, uint8_t command, bool* error)
{
  uint8_t response = dali_query_(address, command, error);
  if (error)
  {
    const uint32_t total_number_of_tries = 2;
    uint32_t tries = 0;
    while ((*error) && (tries++ < total_number_of_tries))
    {
      *error = false;
      lsx_delay_millis(delay_time * 2);
      response = dali_query_(address, command, error);
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
      printf("Response: %u\n", temp_response);
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
  if (dali.short_address == 0)
  {
    result = dali_query0(DALI_BROADCAST, command, error_out);
  }
  else
  {
    bool success = false;
    uint8_t max = 0;
    for (uint32_t i = 0; i < dali.short_address; ++i)
    {
      bool error = false;
      uint8_t current = dali_query0((i << 1) | 0x01, command, &error);
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

void dali_scan(void)
{
  int32_t low_address = 0;
  int32_t high_address = 0x00FFFFFF;
  int32_t current_address = (int32_t)(low_address + high_address) / 2;

  int32_t last_address = high_address;
  dali.short_address = 0;

  uint32_t count = 0;
  bool still_scanning = true;
  while (still_scanning && ((count++) < 128))
  {
    esp_task_wdt_reset();

    while ((high_address - low_address) > 0)
    {
      lsx_log("Low: %ld\n", low_address);
      lsx_log("High: %ld\n", high_address);
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
        dali_receive_(&error, 20000);
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
      if ((found_address == last_address) && (dali.short_address > 0))
      {
        dali.short_address--;
      }
      last_address = found_address;

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
  g_dali_send_timer = lsx_timer_create(timer_send_dali_command, NULL);

  lsx_log("Dali init\n");

  dali.tx_pin = DALI_TX;
  dali.rx_pin = DALI_RX;
  dali.delay = 833 / 2;
  dali.on_the_same_level_count = 0;
  dali.current_brightness = 0;

  lsx_gpio_install_interrupt_service();
  lsx_gpio_add_pin_interrput(dali.rx_pin, pin_change, NULL);

#if 1
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

void dali_task(void* pvParameters)
{
  esp_task_wdt_add(NULL);

  dali_initialize_();

  vTaskDelay(pdMS_TO_TICKS(100));

  bool turn_off_sequence = false;
  bool turn_off_blink = false;
  bool has_read_inputs = false;
  timer_ms_t turn_off_timer = timer_create_ms(dali.config.blink_duration * 1000);
  timer_ms_t send_brightness_timer = timer_create_ms(10000);

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
        timer_is_up_and_reset_ms(&send_brightness_timer, lsx_get_millis()))
    {
      dali_send_brightness(dali.current_brightness);
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
    vTaskDelay(pdMS_TO_TICKS(8));
  }
}

void dali_initialize(nvs_t* scenes_nvs, dali_config_t config)
{
  dali.config = config;
  dali.scene_nvs = scenes_nvs;

  xTaskCreateStatic(dali_task, "DALI Task", DALI_STACK_SIZE, NULL, 3, dali_stack,
                    &dali_stack_type);
}
