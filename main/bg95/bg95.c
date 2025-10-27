#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "at.h"
#include "firmware_update.h"
#include "time_simple.h"
#include "led_control.h"
#include "relay_control.h"
#include "veml6030.h"
#include "version.h"
#include "module_specific.h"
#include "pin_define.h"
#include "json_util.h"
#include "light_control.h"
#include "time_simple.h"
#include "platform.h"
#include "base64.h"

#if defined(LSX_C3_MINI)
#include "option_board/han.h"
#include "option_board/mcp23008.h"
#endif

#define MAX_GNSS_RETRIES        120
#define MAX_GNSS_RETRY_DURATION 7200000 // 2 hours
#define GET_SERVICE_MAX_RETRIES 13

#define MAX_SEND_RETRIES      3
#define MAX_SEND_PING_RETRIES 5

#define BG95_MAX_RESTARTS_BEFORE_QUARANTINE 4

#define RECEIVE_TIMER_CAP           10000
#define NETWORK_TIME_OUT_MARK_START 20000
#define NETWORK_TIME_OUT_MARK_END   90000
#define POWER_DOWN_MARK_START       5000
#define POWER_DOWN_MARK_END         40000

#define CHECK_CONNECTION_TIME_MARK   600000    // 10 min
#define CHECK_SUBSCRIPTION_TIME_MARK 14400000U // 4 hours
#define SYNC_NTP_TIME_MARK           21600000U // 6 hours
// #define SYNC_NTP_TIME_MARK 20000U

#define LAST_CONNECTED_TIME_MARK 14400000U // 3 hours

#define BAND_CHANGE_TOTAL_TIME 3600000

#define LOST_CONNECTION_TIME_MARK   7200 // 2 hour
#define LOST_CONNECTION_TOTAL_COUNT 8

#define MESSAGES_OUT_TOTAL_SIZE 1024

#define TOTAL_OPERATOR_COUNT 30

#define BG95_STACK_SIZE (10 * 1024)

#define MESSAGE_LENGTH_OLD 13
#define COM_LUX            0x18
#define POS_T1             0x02
#define POS_T2             0x06

// #define LSX_STATIC_CJSON

#if defined(LSX_ZHAGA_DALI)
#define SET_MQTT_LISTEN_LTE_LED                                                        \
  do                                                                                   \
  {                                                                                    \
    if (g_bg95.led_is_on)                                                              \
    {                                                                                  \
      led_set(LED_LTE, 127 * (!g_bg95.gnss_found), 127, 0);                            \
    }                                                                                  \
  } while (0)
#else
#define SET_MQTT_LISTEN_LTE_LED                                                        \
  do                                                                                   \
  {                                                                                    \
    if (g_bg95.gnss_found)                                                             \
    {                                                                                  \
      led_set(LED_GPS, 0, 127, 0);                                                     \
      led_set(LED_LTE, 0, 127, 0);                                                     \
    }                                                                                  \
    else                                                                               \
    {                                                                                  \
      led_set(LED_LTE, 127, 68, 0);                                                    \
    }                                                                                  \
  } while (0)
#endif

#define FOR_FF                                                                         \
  do                                                                                   \
  {                                                                                    \
    uint8_t for_ff[] = { /**/ 0xED /**/, 0xA8,           0x3C,                         \
                         /**/ 0xD7 /**/, 0xB5,           /**/ 0xF2 /**/,               \
                         0x2A,           0x34,           0xB1,                         \
                         0x6E,           0xE3,           /**/ 0x11 /**/,               \
                         0xA8,           0x2E,           0x8F,                         \
                         /**/ 0xAD /**/, 0xCE,           0x1E,                         \
                         0xE1,           0xF5,           /**/ 0x39 /**/,               \
                         0xA5,           /**/ 0x71 /**/, 0xC1,                         \
                         /**/ 0xBF /**/, 0xE5,           0x02,                         \
                         0x05,           /**/ 0x44 };                                  \
    static_assert(sizeof(g_bg95.for_hange) > sizeof(for_ff));                          \
    memcpy(g_bg95.for_hange, for_ff, sizeof(for_ff));                                  \
  } while (0)

typedef enum network_state_t
{
  NETWORK_STATE_MODEM_RESTART,
  NETWORK_STATE_NETWORK_INITIALIZATION,
  NETWORK_STATE_GNSS,
  NETWORK_STATE_MQTT_CONNECT,
  NETWORK_STATE_MQTT_LISTEN,
  NETWORK_STATE_CHECK_SUBSCRIPTION,
  NETWORK_STATE_CHECK_CONNECTION,
  NETWORK_STATE_QUARANTINE,
} network_state_t;

typedef struct send_as_master_t
{
  bool should_send;
  float lux;
  float velocity;
  uint32_t time_stamp;
} send_as_master_t;

typedef struct operator_t
{
  string32_t name;
  string32_t short_name;
} operator_t;

typedef struct bg95_t
{
  QueueHandle_t incoming_queue;
  QueueHandle_t outgoing_queue;

  network_state_t state;
  uint32_t step1;
  uint32_t step2;

  uint32_t modem_restart_count;

  uint32_t extra;

  timer_ms_t gnss_retry_timer;
  timer_ms_t time_out_timer;
  timer_ms_t reconnect_gnss_timer;
  timer_ms_t check_subscription_timer;
  timer_ms_t check_connection_timer;
  timer_ms_t sync_time_timer;
  timer_ms_t check_band_change_timer;
  timer_ms_t no_connection_timer;
  timer_ms_t led_on_timer;

  timer_ms_t last_connected_timer;

  uint32_t gnss_retries;

  uint32_t time_out_mark;
  uint32_t bg95_restart_count;
  uint32_t powered_down_mark;

  uint32_t send_retries;

  uint32_t send_ping_retries;

  uint32_t no_service_retry_count;
  uint32_t operator_selected_index;

  uint32_t gnss_try_to_found_count;

  uint32_t recv_count;

  uint32_t search_for_operator_time_mark;

  time_buffer_t lost_connection_buffer;

  uint32_t light_status_sent;

  uint32_t no_response_count;

  uint8_t last_sent_light_status;
  uint8_t current_sent_light_status;

  bool mqtt_connected;
  bool network_initialized;
  bool gnss_found;
  bool gnss_skip;
  bool startup_ack;

  bool extra_file_enabled;
  bool use_extra_file_on_search;

  bool change_direction;

  bool is_telia;

  bool is_master;

  bool for_gone;

  bool activated_restart;

  bool auto_ntp_updates;

  bool led_is_on;

  bool ntp_used;

  bool retry_gnss;
  bool retry_gnss_zhaga;

  string32_t uid;

  uint32_t band_change_count;
  uint32_t band_change_time_mark;

  int32_t rssi;
  int32_t last_sent_rssi;

  string128_t ntp_server;

  string32_t start;

  string256_t response_to_send_back;

  string64_t ccid;
  string64_t tech;
  string64_t channel;
  string64_t oper;
  string64_t ip;

  string64_t last_sent_ccid;
  string64_t last_sent_tech;
  string64_t last_sent_channel;
  string64_t last_sent_oper;
  string64_t last_sent_ip;

  string64_t master_uid;

  string32_t it;

  nvs_t nvs;

  uint32_t operator_count;
  operator_t operators[TOTAL_OPERATOR_COUNT];

  uint8_t for_hange[32];

  string2048_t response;
  string2048_t message;

  uint32_t keepalive;
  uint32_t last_called_gpsloc;

  uint32_t search_for_operator_tries;
  uint32_t select_operator_tries;

} bg95_t;

static StackType_t g_b_s[BG95_STACK_SIZE] = {};
static StaticTask_t g_b_t = {};

static string64_t g_beginning = {};
static string64_t g_lux_beginning = {};
static string64_t g_deu = {};
static string32_t g_dp = {};
static string32_t g_dl = {};
static string32_t g_treu = {};

static bg95_t g_bg95 = {};

static string128_t g_subscribe_command = {};
static string128_t g_lux_subscribe_command = {};

static void set_beginning(void)
{
#ifdef LSX_ZHAGA_DALI
  setting_string2(&g_beginning, g_bg95.extra, 2, o_c('l', 0, g_bg95.extra),
                  o_c('s', 1, g_bg95.extra), o_c('x', 2, g_bg95.extra),
                  o_c('/', 4, g_bg95.extra), o_c('s', 5, g_bg95.extra),
                  o_c('t', 6, g_bg95.extra), o_c('a', 7, g_bg95.extra),
                  o_c('r', 8, g_bg95.extra), o_c('/', 9, g_bg95.extra));
#elif LSX_C3_MINI
  setting_string2(
    &g_beginning, g_bg95.extra, 2, o_c('l', 0, g_bg95.extra), o_c('s', 1, g_bg95.extra),
    o_c('x', 2, g_bg95.extra), o_c('/', 4, g_bg95.extra), o_c('h', 5, g_bg95.extra),
    o_c('a', 6, g_bg95.extra), o_c('n', 7, g_bg95.extra), o_c('/', 8, g_bg95.extra));
#endif

  setting_string2(
    &g_lux_beginning, g_bg95.extra, 2, o_c('l', 0, g_bg95.extra),
    o_c('s', 1, g_bg95.extra), o_c('x', 2, g_bg95.extra), o_c('/', 4, g_bg95.extra),
    o_c('l', 5, g_bg95.extra), o_c('t', 6, g_bg95.extra), o_c('e', 7, g_bg95.extra),
    o_c('s', 8, g_bg95.extra), o_c('w', 9, g_bg95.extra), o_c('/', 10, g_bg95.extra));

  setting_string2(
    &g_deu, g_bg95.extra, 6, o_c('d', 0, g_bg95.extra), o_c('e', 1, g_bg95.extra),
    o_c('v', 2, g_bg95.extra), o_c('i', 3, g_bg95.extra), o_c('c', 4, g_bg95.extra),
    o_c('e', 5, g_bg95.extra), o_c('s', 6, g_bg95.extra), o_c('/', 8, g_bg95.extra),
    o_c('e', 9, g_bg95.extra), o_c('u', 10, g_bg95.extra), o_c('i', 11, g_bg95.extra),
    o_c('-', 12, g_bg95.extra));

  setting_string2(
    &g_bg95.it, g_bg95.extra, 4, o_c('s', 0, g_bg95.extra), o_c('w', 1, g_bg95.extra),
    o_c('i', 2, g_bg95.extra), o_c('t', 3, g_bg95.extra), o_c('c', 4, g_bg95.extra),
    o_c('h', 6, g_bg95.extra), o_c('u', 7, g_bg95.extra), o_c('s', 8, g_bg95.extra),
    o_c('e', 9, g_bg95.extra), o_c('r', 10, g_bg95.extra));
}

static char* get_subscribe_topic(void)
{
  string128(&g_subscribe_command, "\"%s%s%s%s\"", g_beginning.data, g_deu.data,
            g_bg95.uid.data, g_dp.data);
  return g_subscribe_command.data;
}

static char* get_lux_subscribe_topic(void)
{
  string128(&g_lux_subscribe_command, "\"%s%s%s%s\"", g_lux_beginning.data, g_deu.data,
            g_bg95.master_uid.data, g_dl.data);
  return g_lux_subscribe_command.data;
}

static bool send_subscribe_command(void)
{
  string2048_reset(&g_bg95.response);

  bool all_good = false;

  const uint32_t timeout_ms = 80000;
  string128_t sub_command = {};
  string32_t s_internal = {};

  setting_string2(&s_internal, g_bg95.extra, 3, o_c('+', 0, g_bg95.extra),
                  o_c('Q', 1, g_bg95.extra), o_c('M', 2, g_bg95.extra),
                  o_c('T', 3, g_bg95.extra), o_c('S', 5, g_bg95.extra),
                  o_c('U', 6, g_bg95.extra), o_c('B', 7, g_bg95.extra));
  string128(&sub_command, "%s%s=0,1,%s,2", g_bg95.start.data, s_internal.data,
            get_subscribe_topic());

  string32_t expect = {};
  string32(&expect, "%s:", s_internal.data);
  if (at_send_and_wait_for_response(sub_command.data, expect.data, timeout_ms, true,
                                    &g_bg95.response, true, true))
  {
    string32_t find = {};
    string32(&find, "%s 0,1,0,2", expect.data);
    all_good = string2048_find(&g_bg95.response, find.data, 0, -1) != -1;

    if ((g_bg95.master_uid.length != 0) && all_good)
    {
      string128_reset(&sub_command);
      string128(&sub_command, "%s%s=0,1,%s,2", g_bg95.start.data, s_internal.data,
                get_lux_subscribe_topic());
      at_send_and_wait_for_response(sub_command.data, expect.data, timeout_ms, true,
                                    &g_bg95.response, true, true);

      all_good = string2048_find(&g_bg95.response, find.data, 0, -1) != -1;
    }
  }
  // lsx_log("AT+QMTSUB: %s\n", g_bg95.response.data);
  return all_good;
}

#define GNSS_RESET_RETRY_TIME 200000

static void reset_gnss(bg95_t* bg95)
{
  bg95->gnss_found = false;
  bg95->gnss_skip = false;
  bg95->gnss_retries = 70;
  bg95->gnss_retry_timer = timer_create_ms(GNSS_RESET_RETRY_TIME);
  bg95->gnss_try_to_found_count = 0;
}

static void bg95_send_set_master_message(bg95_t* bg95)
{
  uint8_t status = bg95->is_master ? 2 : (bg95->master_uid.length != 0) ? 1 : 0;
  cJSON* json = cJSON_CreateObject();
  cJSON* child = json_add_object_message_type(json, MESSAGE_TYPE_SET_MASTER_SLAVE);
  json_add_number(child, "S", status);
  json_send_to_queue_and_delete(json, bg95->incoming_queue, pdMS_TO_TICKS(100));
}

static void bg95_create(bg95_t* bg95)
{
  set_beginning();

  bg95->state = NETWORK_STATE_MODEM_RESTART;
  bg95->step1 = 0;
  bg95->step2 = 0;

  bg95->no_service_retry_count = 0;

  reset_gnss(bg95);
#ifdef LSX_RELEASE
  bg95->extra_file_enabled = false;
#else
  bg95->extra_file_enabled = false;
#endif
  bg95->use_extra_file_on_search = true;

  bg95->powered_down_mark = POWER_DOWN_MARK_START;

  bg95->bg95_restart_count = 0;

  bg95->modem_restart_count = 0;

  bg95->rssi = 99;
  string64(&bg95->tech, " - ");
  string64(&bg95->channel, " - ");
  string64(&bg95->oper, " - ");
  string64(&bg95->ip, "0.0.0.0");

  bg95->last_sent_rssi = 0;
  string64(&bg95->last_sent_tech, "");
  string64(&bg95->last_sent_channel, "");
  string64(&bg95->last_sent_oper, "");
  string64(&bg95->last_sent_ip, "");

  bg95->time_out_mark = NETWORK_TIME_OUT_MARK_START;
  bg95->time_out_timer = timer_create_ms(bg95->time_out_mark);

  bg95->check_band_change_timer = timer_create_ms(0);

  time_buffer_create(&bg95->lost_connection_buffer, LOST_CONNECTION_TOTAL_COUNT,
                     LOST_CONNECTION_TIME_MARK);

  bg95->search_for_operator_time_mark = 0;

  bg95->startup_ack = true;
  bg95->is_telia = false;

  bg95->retry_gnss = false;
  bg95->retry_gnss_zhaga = false;

  bg95->change_direction = false;

  bg95->auto_ntp_updates = true;
  lsx_nvs_get_uint8(&bg95->nvs, "NAU", (uint8_t*)&bg95->auto_ntp_updates, 1);

  bg95->keepalive = 360;
  lsx_nvs_get_uint32(&bg95->nvs, "KA", &bg95->keepalive, 360);

  bg95->recv_count = 0;

  bg95->operator_count = 0;
  memset(bg95->operators, 0, sizeof(bg95->operators));

  bg95->sync_time_timer = timer_create_ms(SYNC_NTP_TIME_MARK);

  bool ntp_success =
    lsx_nvs_get_string(&bg95->nvs, "ntp", bg95->ntp_server.data,
                       &bg95->ntp_server.length, sizeof(bg95->ntp_server.data) - 1);
  if (!ntp_success || (bg95->ntp_server.length == 0))
  {
    string128(&bg95->ntp_server, "ntp.se");
  }

  memset(&bg95->response, 0, sizeof(bg95->response));
  memset(&bg95->message, 0, sizeof(bg95->message));

  setting_string2(&bg95->start, bg95->extra, 3, o_c('A', 0, bg95->extra),
                  o_c('T', 1, bg95->extra));

  bg95->for_gone = false;

  bg95->light_status_sent = 0;
  bg95->last_sent_light_status = UINT8_MAX;
  bg95->current_sent_light_status = 0;

  bg95->no_response_count = 0;
  bg95->activated_restart = true;

  bg95->last_connected_timer = timer_create_ms(LAST_CONNECTED_TIME_MARK);

  memset(&bg95->master_uid, 0, sizeof(bg95->master_uid));
  lsx_nvs_get_string(&bg95->nvs, "Slave", bg95->master_uid.data,
                     &bg95->master_uid.length, sizeof(bg95->master_uid.data) - 1);

  lsx_nvs_get_uint8(&bg95->nvs, "Master", (uint8_t*)&bg95->is_master, 0);
  bg95_send_set_master_message(bg95);

  bg95->led_on_timer = timer_create_ms(LED_ON_TIME);
  bg95->led_is_on = true;

  bg95->ntp_used = false;
  bg95->last_called_gpsloc = lsx_get_millis();

  bg95->search_for_operator_tries = 0;
  bg95->select_operator_tries = 0;
}

static void bg95_reset(bg95_t* bg95, network_state_t new_state)
{
  bg95->step1 = 0;
  bg95->step2 = 0;
  bg95->state = new_state;
  bg95->time_out_timer = timer_create_ms(g_bg95.time_out_mark);
}

static void bg95_proceed(bg95_t* bg95)
{
  bg95->step1 += 1;
  bg95->step2 = 0;
  bg95->time_out_timer = timer_create_ms(g_bg95.time_out_mark);
}

static void bg95_reconnect_gnss(bg95_t* bg95)
{
  uint32_t extra = 0;
  if (bg95->gnss_try_to_found_count > 3)
  {
    extra = bg95->gnss_try_to_found_count;
  }
  bg95_reset(bg95, NETWORK_STATE_GNSS);
  bg95->gnss_retries = min(bg95->gnss_retries + 12, MAX_GNSS_RETRIES);
  bg95->gnss_retry_timer.duration =
    min(bg95->gnss_retry_timer.duration + 40000 + (120000 * extra),
        MAX_GNSS_RETRY_DURATION);
}

#define ASSERT_TOKEN(token)                                                            \
  do                                                                                   \
  {                                                                                    \
    if (!(token))                                                                      \
    {                                                                                  \
      lsx_log("Token Error");                                                          \
      return false;                                                                    \
    }                                                                                  \
  } while (0)

static int32_t parse_rssi_response(string2048_t* response)
{
  const char* delims = " ,";
  tokeniser_t tokeniser = {};
  tokeniser_create(&tokeniser, delims, strlen(delims), response->data, response->length,
                   TOKENISER_ACTION_NO_ORDER);

  int32_t rssi = 99;
  if (tokeniser.token_count >= 2)
  {
    rssi = atoi(tokeniser_get_token(&tokeniser, 1));
    if (rssi != 99)
    {
      rssi = -113 + (rssi * 2);
    }
  }
  tokeniser_reset(&tokeniser);
  return rssi;
}

static void date_as_string(time_info_t elements, string64_t* date)
{
  string64(date, "%04d/%02d/%02d,%02d:%02d:%02d", elements.year + 1900,
           elements.month + 1, elements.month_day, elements.hour, elements.minute,
           elements.second);
}

static bool parse_qnwinfo_response(string2048_t* response)
{
  const char* delims = "\",\r";

  tokeniser_t tokeniser = {};
  tokeniser_create(&tokeniser, delims, strlen(delims), response->data, response->length,
                   TOKENISER_ACTION_NO_ORDER);
  bool result = false;
  if (tokeniser.token_count >= 10)
  {
    string64(&g_bg95.tech, tokeniser_get_token(&tokeniser, 1));
    string64(&g_bg95.oper, tokeniser_get_token(&tokeniser, 4));
    string64(&g_bg95.channel, "%s %s", tokeniser_get_token(&tokeniser, 7),
             tokeniser_get_token(&tokeniser, 9));
    result = true;
  }
  tokeniser_reset(&tokeniser);
  return result;
}

static bool parse_qiact_response(string2048_t* response)
{
  const char* delims = " \",";
  tokeniser_t tokeniser = {};
  tokeniser_create(&tokeniser, delims, strlen(delims), response->data, response->length,
                   TOKENISER_ACTION_NO_ORDER);

  bool result = false;
  if (tokeniser.token_count >= 6)
  {
    string64(&g_bg95.ip, "%s", tokeniser_get_token(&tokeniser, 5));
    result = true;
  }
  tokeniser_reset(&tokeniser);

  return result;
}

static bool parse_qgpsloc_response(string2048_t* response, uint32_t offset)
{
  const char* delims = " ,";
  tokeniser_t tokeniser = {};
  tokeniser_create(&tokeniser, delims, strlen(delims), response->data, response->length,
                   TOKENISER_ACTION_NO_ORDER);

  bool result = false;
  if (tokeniser.token_count >= 11)
  {
    time_info_t ti = {};
    char* token = tokeniser_get_token(&tokeniser, 1);
    ti.second = atoi(token + 4);

    char temp = token[4];
    token[4] = '\0';
    ti.minute = atoi(token + 2);
    token[4] = temp;

    temp = token[2];
    token[2] = '\0';
    ti.hour = atoi(token);
    token[2] = temp;

    const float latitude = atof(tokeniser_get_token(&tokeniser, 2));
    const float longitude = atof(tokeniser_get_token(&tokeniser, 3));

    token = tokeniser_get_token(&tokeniser, 10);
    ti.year = atoi(token + 4) + 100;

    temp = token[4];
    token[4] = '\0';
    ti.month = atoi(token + 2) - 1;
    token[4] = temp;

    temp = token[2];
    token[2] = '\0';
    ti.month_day = atoi(token);
    token[2] = temp;

    const uint32_t t = (uint32_t)lsx_make_time(&ti) + offset;
    lsx_log("GPS Time: %lu\n", t);
    time_simple_set_time(t);

    cJSON* gps_json = cJSON_CreateObject();
    cJSON* child = json_add_object_message_type(gps_json, MESSAGE_TYPE_INSERT_POS);
    json_add_number(child, "Lat", latitude);
    json_add_number(child, "Lon", longitude);
    json_send_to_queue_and_delete(gps_json, g_bg95.incoming_queue, 1000);
    result = true;
  }
  tokeniser_reset(&tokeniser);

  return result;
}

static uint32_t string_remove_first_and_last(char* string, uint32_t length)
{
  if (length <= 2)
  {
    string[0] = '\0';
    return 0;
  }
  for (uint32_t i = 0; i < (length - 1); ++i)
  {
    string[i] = string[i + 1];
  }
  length -= 2;
  string[length] = '\0';
  return length;
}

static bool parse_cops_response(string2048_t* response)
{
  g_bg95.operator_count = 0;

  char* token = first_token(response->data, "(");
  token = next_token(")");
  while (token && (g_bg95.operator_count < TOTAL_OPERATOR_COUNT))
  {
    if (token[0] == '1' || token[0] == '2')
    {
      string128_t token_to_split = {};
      string128(&token_to_split, "%s", token);

      tokeniser_t tokeniser = {};
      tokeniser_create(&tokeniser, ",", 1, token_to_split.data, token_to_split.length,
                       TOKENISER_ACTION_NO_ORDER);
      if (tokeniser.token_count >= 4)
      {
        string32_t temp = {};
        if (string32(&temp, "%s", tokeniser_get_token(&tokeniser, 3)))
        {
          string_remove_first_and_last(temp.data, temp.length);

          bool found = false;
          for (uint32_t j = 0; (j < g_bg95.operator_count) && !found; ++j)
          {
            found = string32_equal(&g_bg95.operators[j].short_name, &temp);
          }
          if (!found)
          {
            string32_t* short_name =
              &g_bg95.operators[g_bg95.operator_count].short_name;
            string32_t* long_name = &g_bg95.operators[g_bg95.operator_count].name;
            string32_copy(short_name, &temp);
            string32(long_name, "%s", tokeniser_get_token(&tokeniser, 1));
            string_remove_first_and_last(long_name->data, long_name->length);

            g_bg95.operator_count++;
          }
        }
      }
    }
    token = next_token("(");
    token = next_token(")");
  }
  return false;
}

static bool send_ntp_command(const char* ntp_server, uint32_t timeout_ms)
{
  string128_t ntp_command = {};
  string128(&ntp_command, "AT+QNTP=1,\"%s\"", ntp_server);

  bool result = false;
  if (at_send_and_wait_for_response_d3(ntp_command.data, "+QNTP:", timeout_ms, true,
                                       &g_bg95.response))
  {
    if (string2048_find(&g_bg95.response, "+QNTP: 0,", 0, -1) != -1)
    {
      time_info_t ti = {};
      if (parse_ntp_response(&g_bg95.response, &ti))
      {
        const uint32_t ntp_time = (uint32_t)lsx_make_time(&ti);
        lsx_log("Updated time: %lu\n", ntp_time);
        time_simple_set_time(ntp_time);
        result = true;
        g_bg95.ntp_used = true;
      }
    }
  }
  printf("AT+QNTP: %s\n", g_bg95.response.data);
  return result;
}

void parse_and_set_gnss(string2048_t* response, int32_t offset)
{
  g_bg95.gnss_try_to_found_count = 0;
  parse_qgpsloc_response(response, offset);
#if defined(LSX_C3_MINI) || defined(LSX_S3)
  led_set(LED_GPS, 127, 68, 0);
#endif

#if 0
  if (g_bg95.extra_file_enabled)
  {
    g_bg95.extra_file_enabled = false;
    bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
    g_bg95.activated_restart = true;
  }
#endif

  g_bg95.gnss_found = true;
}

static bool g_try_new_operator = false;

static bool send_qnwinfo_command(void)
{
  bool result = false;
  if (at_send_and_wait_for_response_d3("AT+QNWINFO", "+QNWINFO:", 40000, true,
                                       &g_bg95.response))
  {
    if (string2048_find(&g_bg95.response, "No Service", 0, -1) != -1)
    {
#ifdef LSX_ZHAGA_DALI
      if (g_bg95.led_is_on)
#endif
      {
        led_set(LED_LTE, 127, 0, 0);
      }
      const uint32_t timeout = 4000 + (4000 * g_bg95.step2);
      const uint32_t max_tries =
        GET_SERVICE_MAX_RETRIES + (14 * g_bg95.no_service_retry_count);
      if (g_bg95.step2++ >= max_tries)
      {
        bg95_reset(&g_bg95, NETWORK_STATE_QUARANTINE);
      }
      else
      {
        delay_task(timeout);
      }
      g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
    }
    else
    {
      parse_qnwinfo_response(&g_bg95.response);
      result = true;
    }
  }
  if (g_bg95.startup_ack)
  {
    printf("AT+QNWINFO: %s\n", g_bg95.response.data);
  }
  return result;
}

static bool send_qiact_info_command(void)
{
  bool result = false;
  if (at_send_and_wait_for_response_d3("AT+QIACT?", "+QIACT:", 40000, true,
                                       &g_bg95.response))
  {
    // TODO(Linus): only proceed if the parsing succeeded?
    if (parse_qiact_response(&g_bg95.response))
    {
    }
    result = true;
  }
  if (g_bg95.startup_ack)
  {
    // printf("AT+QIACT: %s\n", g_bg95.response.data);
  }
  return result;
}

static bool send_csq_command(void)
{
  bool result = false;
  if (at_send_and_wait_for_response_d3("AT+CSQ", "+CSQ:", 40000, true,
                                       &g_bg95.response))
  {
    g_bg95.rssi = parse_rssi_response(&g_bg95.response);
    if (g_bg95.rssi != 99)
    {
      result = true;
    }
    else
    {
      delay_task(1000);
    }
  }
  if (g_bg95.startup_ack)
  {
    printf("AT+CSQ: %s\n", g_bg95.response.data);
  }
  return result;
}

static void set_mqtt_listen(void)
{
  SET_MQTT_LISTEN_LTE_LED;

  bg95_reset(&g_bg95, NETWORK_STATE_MQTT_LISTEN);
  g_bg95.check_subscription_timer = timer_create_ms(20000);
  g_bg95.check_connection_timer = timer_create_ms(20000);
  g_bg95.gnss_retry_timer = timer_create_ms(g_bg95.gnss_retry_timer.duration);

  g_bg95.mqtt_connected = true;
  g_bg95.activated_restart = false;

  if (g_bg95.gnss_retry_timer.duration == GNSS_RESET_RETRY_TIME)
  {
    g_bg95.gnss_retry_timer.time = lsx_get_millis();
  }
}

static bool should_fall_through_at_command(void)
{
  delay_task(3000);
  if (g_bg95.bg95_restart_count >= 2)
  {
    g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
    return ++g_bg95.step2 >= 4;
  }
  return false;
}

static void populate_operator_array(const string2048_t* response)
{
  parse_cops_response(&g_bg95.response);
  g_bg95.search_for_operator_time_mark = (lsx_get_time() / 60);
}

static void send_search_for_operator_command(void)
{
  if (!g_bg95.is_telia)
  {
    const time_t time_now = lsx_get_time() / 60;
    bool half_day =
      ((int64_t)time_now - (int64_t)g_bg95.search_for_operator_time_mark) >= 720;

    if (g_bg95.operator_count == 0 || half_day)
    {
      const uint32_t timeout_ms = 1600000 + (300000 * g_bg95.search_for_operator_tries);
      g_bg95.operator_count = 0;
      if (at_send_and_wait_for_response_d3("AT+COPS=?", "+COPS:", timeout_ms, false,
                                           &g_bg95.response))
      {
        populate_operator_array(&g_bg95.response);
#ifndef LSX_RELEASE
        for (uint32_t i = 0; i < g_bg95.operator_count; ++i)
        {
          printf("%s\n", g_bg95.operators[i].short_name.data);
        }
#endif
        g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
      }
      else
      {
        g_bg95.search_for_operator_tries = min(g_bg95.search_for_operator_tries + 1, 3);
      }
    }
  }
}

static void restart_and_initialize_network(void)
{
}

static bool select_operator(const char* oper, bool bypass_qnwinfo)
{
  string128_t select_operator_command = {};
  if (string128(&select_operator_command, "AT+COPS=1,2,\"%s\"", oper))
  {
    const uint32_t timeout_ms = 210000 + (30000 * g_bg95.select_operator_tries);
    if (at_send_and_wait_for_response_d1(select_operator_command.data, "OK",
                                         timeout_ms))
    {
      delay_task(5000);
      at_send_and_wait_for_response_d1("AT+CFUN=1,1", "APP RDY", 60000);
      at_send_and_wait_for_response_d1(select_operator_command.data, "OK", 60000);
      at_reset_disconnected_flag();

      bg95_reset(&g_bg95, NETWORK_STATE_NETWORK_INITIALIZATION);
      g_bg95.activated_restart = true;
      return true;
    }
    else
    {
      g_bg95.select_operator_tries = min(g_bg95.select_operator_tries + 1, 4);
    }
  }
  return false;
}

static bool look_for_and_select_new_operator(void)
{
  bool success = true;
  if (!g_bg95.is_telia)
  {
    success = false;
    send_search_for_operator_command();

    if (g_bg95.operator_count != 0)
    {
      success = select_operator(
        g_bg95.operators[g_bg95.operator_selected_index].short_name.data, true);
      g_bg95.operator_selected_index =
        plus_one_wrap(g_bg95.operator_selected_index, g_bg95.operator_count);
    }
    delay_task(5000);
  }
  return success;
}

static void bg95_network_initialization(void)
{
  uint32_t i = 0;
  if (g_bg95.step1 == i++)
  {
    at_send_and_wait_for_response_d0("AT+QGPSCFG=\"priority\",1", "OK");
    delay_task(1000);
    at_send_and_wait_for_response_d0("AT+QGPSCFG=\"priority\"", "+QGPSCFG:");
    bg95_proceed(&g_bg95);

    if (!g_bg95.activated_restart)
    {
      if (time_buffer_add_time(&g_bg95.lost_connection_buffer, lsx_get_time()) ||
          timer_is_up_ms(g_bg95.last_connected_timer, lsx_get_millis()))
      {
        g_bg95.lost_connection_buffer.count = 0;
        g_bg95.last_connected_timer = timer_create_ms(LAST_CONNECTED_TIME_MARK);

        look_for_and_select_new_operator();
      }
    }
    else
    {
      g_bg95.activated_restart = false;
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d3("AT+QCCID", "+QCCID:", 40000, false,
                                         &g_bg95.response))
    {
      int32_t ccid_start_index = string2048_find_char(&g_bg95.response, ' ', 0);
      if (ccid_start_index != -1)
      {
        int32_t ccid_end_index =
          string2048_find_char(&g_bg95.response, '\r', ++ccid_start_index);
        if (ccid_end_index == -1)
        {
          ccid_end_index = g_bg95.response.length;
        }
        g_bg95.response.data[ccid_end_index] = '\0';
        string64(&g_bg95.ccid, "%s", g_bg95.response.data + ccid_start_index);
      }
      bg95_proceed(&g_bg95);

      g_bg95.is_telia = string64_begins_with(&g_bg95.ccid, "8945");
    }
    else if (should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
    if (g_bg95.startup_ack)
    {
      printf("AT+QCCID: %s\n", g_bg95.response.data);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d0("AT+CREG=2", "OK"))
    {
      bg95_proceed(&g_bg95);
    }
    else if (should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (g_bg95.is_telia || at_send_and_wait_for_response_d0("AT+CEREG=2", "OK"))
    {
      bg95_proceed(&g_bg95);
    }
    else if (should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d0("AT+CGREG=2", "OK"))
    {
      bg95_proceed(&g_bg95);
    }
    else if (should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);

    delay_task(2000);
    if (send_qnwinfo_command())
    {
      delay_task(2000);
      bg95_proceed(&g_bg95);
#ifdef LSX_ZHAGA_DALI
      if (g_bg95.led_is_on)
#endif
      {
        led_set(LED_LTE, 127, 68, 0);
      }
      g_bg95.no_service_retry_count = 0;
      g_bg95.search_for_operator_tries = 0;
      g_bg95.select_operator_tries = 0;
      g_try_new_operator = false;
    }

    if (g_bg95.no_service_retry_count >= 2 || g_bg95.state == NETWORK_STATE_QUARANTINE)
    {
      if (g_try_new_operator)
      {
        g_bg95.no_service_retry_count = 1;
      }
      if (++g_bg95.no_service_retry_count >= 2)
      {
        if (look_for_and_select_new_operator())
        {
          g_bg95.no_service_retry_count = 0;
          g_try_new_operator = !g_bg95.is_telia;
        }
      }
    }
  }
  else if (g_bg95.step1 == i++)
  {
    bool all_good = false;
    if (at_send_and_wait_for_response_d3("AT+CGREG?", "+CGREG:", 40000, false,
                                         &g_bg95.response))
    {
      lsx_log("AT+CGREG?: %s\n", g_bg95.response.data);
      if ((string2048_find(&g_bg95.response, "+CGREG: 2,5", 0, -1) != -1) ||
          (string2048_find(&g_bg95.response, "+CGREG: 2,1", 0, -1) != -1))
      {
        all_good = true;
      }
      else if (at_send_and_wait_for_response_d3("AT+CEREG?", "+CEREG:", 40000, false,
                                                &g_bg95.response))
      {
        lsx_log("AT+CEREG?: %s\n", g_bg95.response.data);
        if ((string2048_find(&g_bg95.response, "+CEREG: 2,5", 0, -1) != -1) ||
            (string2048_find(&g_bg95.response, "+CEREG: 2,1", 0, -1) != -1))
        {
          all_good = true;
        }
        else if (at_send_and_wait_for_response_d3("AT+CREG?", "+CREG:", 40000, false,
                                                  &g_bg95.response))
        {
          lsx_log("AT+CREG?: %s\n", g_bg95.response.data);
          if ((string2048_find(&g_bg95.response, "+CREG: 2,5", 0, -1) != -1) ||
              (string2048_find(&g_bg95.response, "+CREG: 2,1", 0, -1) != -1))
          {
            all_good = true;
          }
        }
      }
    }
    if (all_good || (++g_bg95.step2 >= (30 + (25 * g_bg95.bg95_restart_count))))
    {
      bg95_proceed(&g_bg95);
    }
    else
    {
      delay_task(8000);
      g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d0("AT+CIMI", "OK"))
    {
      bg95_proceed(&g_bg95);
    }
    else if (should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    const char* command = "AT+QICSGP=1,1,\"iot.1nce.net\",\"\",\"\",1";
    if (g_bg95.is_telia)
    {
      command = "AT+QICSGP=1,1,\"lpwa.telia.iot\",\"\",\"\",1";
    }
    if (at_send_and_wait_for_response_d0(command, "OK") ||
        should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
    lsx_log("%s\n", g_bg95.response.data);
  }
  else if (g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d0("AT+QIACT=1", "OK") ||
        should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else
  {
    if (send_qiact_info_command() || should_fall_through_at_command())
    {
      if (!g_bg95.ntp_used)
      {
        send_ntp_command(g_bg95.ntp_server.data, 60000);
      }
      g_bg95.network_initialized = true;
      g_bg95.mqtt_connected = false;
      if (!g_bg95.gnss_found && (g_bg95.gnss_try_to_found_count == 0))
      {
        bg95_reset(&g_bg95, NETWORK_STATE_GNSS);
      }
      else
      {
        FOR_FF;
        bg95_reset(&g_bg95, NETWORK_STATE_MQTT_CONNECT);
      }
    }
  }
}

static string64_t got = {};

static void drt(void)
{
  setting_string2(
    &got, g_bg95.extra, 7, o_c('A', 0, g_bg95.extra), o_c('T', 1, g_bg95.extra),
    o_c('+', 2, g_bg95.extra), o_c('Q', 3, g_bg95.extra), o_c('M', 4, g_bg95.extra),
    o_c('T', 5, g_bg95.extra), o_c('C', 6, g_bg95.extra), o_c('O', 7, g_bg95.extra),
    o_c('N', 9, g_bg95.extra), o_c('N', 10, g_bg95.extra), o_c('=', 11, g_bg95.extra),
    o_c('0', 12, g_bg95.extra));
}

static void bg95_mqtt_connect(uint8_t* th, uint8_t* deco)
{
  uint32_t i = 0;
  if (g_bg95.step1 == i++)
  {
    string64_t keepalive_command = {};
    string64(&keepalive_command, "AT+QMTCFG=\"keepalive\",0,%lu", g_bg95.keepalive);
    at_send_and_wait_for_response_d0(keepalive_command.data, "OK");
    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.step1 == i++)
  {

    string128_t pop_if_must = {};
    setting_string2(&pop_if_must, g_bg95.extra, 15, o_c('A', 0, g_bg95.extra),
                    o_c('T', 1, g_bg95.extra), o_c('+', 2, g_bg95.extra),
                    o_c('Q', 3, g_bg95.extra), o_c('M', 4, g_bg95.extra),
                    o_c('T', 5, g_bg95.extra), o_c('O', 6, g_bg95.extra),
                    o_c('P', 7, g_bg95.extra), o_c('E', 8, g_bg95.extra),
                    o_c('N', 9, g_bg95.extra), o_c('=', 10, g_bg95.extra),
                    o_c('0', 11, g_bg95.extra), o_c(',', 12, g_bg95.extra),
                    o_c('"', 13, g_bg95.extra), o_c('m', 14, g_bg95.extra),
                    o_c('q', 15, g_bg95.extra), o_c('t', 17, g_bg95.extra),
                    o_c('t', 18, g_bg95.extra), o_c('.', 19, g_bg95.extra),
                    o_c('g', 20, g_bg95.extra), o_c('a', 21, g_bg95.extra),
                    o_c('t', 22, g_bg95.extra), o_c('u', 23, g_bg95.extra),
                    o_c('b', 24, g_bg95.extra), o_c('e', 25, g_bg95.extra),
                    o_c('l', 26, g_bg95.extra), o_c('y', 27, g_bg95.extra),
                    o_c('s', 28, g_bg95.extra), o_c('n', 29, g_bg95.extra),
                    o_c('i', 30, g_bg95.extra), o_c('n', 31, g_bg95.extra),
                    o_c('g', 32, g_bg95.extra), o_c('.', 33, g_bg95.extra),
                    o_c('s', 34, g_bg95.extra), o_c('e', 35, g_bg95.extra),
                    o_c('"', 36, g_bg95.extra), o_c(',', 37, g_bg95.extra),
                    o_c('1', 38, g_bg95.extra), o_c('8', 39, g_bg95.extra),
                    o_c('8', 40, g_bg95.extra), o_c('4', 41, g_bg95.extra));

    string32_t expected = {};
    setting_string2(&expected, g_bg95.extra, 0, o_c('+', 0, g_bg95.extra),
                    o_c('Q', 2, g_bg95.extra), o_c('M', 3, g_bg95.extra),
                    o_c('T', 4, g_bg95.extra), o_c('O', 5, g_bg95.extra),
                    o_c('P', 6, g_bg95.extra), o_c('E', 7, g_bg95.extra),
                    o_c('N', 8, g_bg95.extra), o_c(':', 9, g_bg95.extra));
    if (at_send_and_wait_for_response_no_log(pop_if_must.data, expected.data, 160000,
                                             false, &g_bg95.response, true, true))
    {
      string32_t expected2 = {};
      string32(&expected2, "%s 0,0", expected.data);
      if (string2048_find(&g_bg95.response, expected2.data, 0, -1) != -1)
      {
        bg95_proceed(&g_bg95);
      }
      else
      {
        delay_task(2000);
      }
    }
    // lsx_log("AT%s: %s\n", expected.data, g_bg95.response.data);

    memset(&pop_if_must, 0, sizeof(pop_if_must));
    memset(&expected, 0, sizeof(expected));
  }
  else if (g_bg95.step1 == i++)
  {
    drt();
    string64_t util = {};

    uint8_from_util_buffer(g_bg95.for_hange, sizeof(g_bg95.for_hange), deco, &util, th);
    memset(g_bg95.for_hange, 0, sizeof(g_bg95.for_hange));

    string32_t ll = {};
    setting_string2(&ll, g_bg95.extra, 3, o_c('l', 0, g_bg95.extra),
                    o_c('s', 1, g_bg95.extra), o_c('x', 2, g_bg95.extra),
                    o_c('_', 3, g_bg95.extra), o_c('s', 5, g_bg95.extra),
                    o_c('w', 6, g_bg95.extra), o_c('_', 7, g_bg95.extra));
    string128_t connect_command = {};
    string128(&connect_command, "%s,\"%s%s\",\"%s\",\"%s\"", got.data, ll.data,
              g_bg95.uid.data, g_bg95.it.data, util.data);
    if (at_send_and_wait_for_response_no_log(connect_command.data, g_treu.data, 80000,
                                             false, &g_bg95.response, true, true))
    {
      string32_t temp = {};
      string32(&temp, "%s 0,0,0", g_treu.data);
      if (string2048_find(&g_bg95.response, temp.data, 0, -1) != -1)
      {
        bg95_proceed(&g_bg95);
      }
      else
      {
        delay_task(2000);
      }
    }
    // lsx_log("AT%s %s\n", g_treu.data, g_bg95.response.data);
    memset(&connect_command, 0, sizeof(connect_command));
    memset(&util, 0, sizeof(util));
  }
  else if (g_bg95.step1 == i++)
  {
    if (send_subscribe_command())
    {
      bg95_proceed(&g_bg95);
    }
    else
    {
      delay_task(2000);
    }
  }
  else if (g_bg95.step1 == i++)
  {
    if (send_csq_command() || should_fall_through_at_command())
    {
      bg95_proceed(&g_bg95);
    }
  }
  else
  {
    set_mqtt_listen();
    g_bg95.mqtt_connected = true;
  }
}

static bool extra_file_expired = true;
static void bg95_gnss_initialization(void)
{
  uint32_t i = 0;
  if (g_bg95.step1 == i++)
  {
    g_bg95.gnss_try_to_found_count++;
    if ((g_bg95.gnss_try_to_found_count == 1) && g_bg95.extra_file_enabled)
    {
      uint32_t time = lsx_get_time();
      if (time < TIME_IN_THE_PAST)
      {
        g_bg95.extra_file_enabled = false;
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        return;
      }
    }
    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.extra_file_enabled && g_bg95.step1 == i++)
  {
    extra_file_expired = true;

    if (at_send_and_wait_for_response_d3("AT+QGPSXTRATIME?", "+QGPSXTRATIME:", 40000,
                                         false, &g_bg95.response))
    {
      const int32_t time_index = string2048_find_char(&g_bg95.response, '"', 0);
      if (time_index != -1)
      {
        if (string2048_find(&g_bg95.response, "\"\"", time_index, -1) != -1)
        {
          time_info_t elements = {};
          lsx_get_time_info_default(&elements);
          string64_t date = {};
          date_as_string(elements, &date);
          string128_t insert_extra_time_command = {};
          string128(&insert_extra_time_command, "AT+QGPSXTRATIME=0,\"%s\"", date.data);
          at_send_and_wait_for_response_d0(insert_extra_time_command.data, "OK");
          at_send_and_wait_for_response_d0("AT+QGPSXTRATIME?", "+QGPSXTRATIME:");
        }
      }
    }
    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.extra_file_enabled && g_bg95.step1 == i++)
  {
    if (at_send_and_wait_for_response_d3("AT+QGPSCFG=\"xtra_info\"", "+QGPSCFG:", 40000,
                                         false, &g_bg95.response))
    {
      int32_t left_time_start_index = string2048_find_char(&g_bg95.response, ',', 0);
      if (left_time_start_index != -1)
      {
        const int32_t left_time_end_index =
          string2048_find_char(&g_bg95.response, ',', ++left_time_start_index);
        if (left_time_end_index != -1)
        {
          g_bg95.response.data[left_time_end_index] = '\0';
          const uint32_t left_time =
            (uint32_t)strtoul(g_bg95.response.data + left_time_start_index, NULL, 0);

          lsx_log("Extra file left time: %lu\n", left_time);
          extra_file_expired = left_time == 0;
        }
      }
    }
    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.extra_file_enabled && extra_file_expired && g_bg95.step1 == i++)
  {
    at_send_and_wait_for_response_d1("AT+QGPSCFG=\"xtra_download\",1",
                                     "+QGPSURC:", 40000);
    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.step1 == i++)
  {
    at_send_and_wait_for_response_d0("AT+QGPSCFG=\"priority\",0", "OK");
    if (at_send_and_wait_for_response_d0("AT+QGPS=1,3", "OK"))
    {
      delay_task(7200);
      at_send_and_wait_for_response_d0("AT+QGPSCFG=\"priority\"", "+QGPSCFG:");
    }
    else
    {
      delay_task(2000);
      at_send_and_wait_for_response_d0("AT+QGPSEND", "OK");
      delay_task(2000);
      at_send_and_wait_for_response_d0("AT+QGPS=1,3", "OK");
      delay_task(5000);
      at_send_and_wait_for_response_d0("AT+QGPSCFG=\"priority\"", "+QGPSCFG:");
    }

    bg95_proceed(&g_bg95);
  }
  else if (g_bg95.step1 == i++)
  {
    g_bg95.last_called_gpsloc = lsx_get_millis();
    if ((g_bg95.step2++ >= g_bg95.gnss_retries) ||
        at_send_and_wait_for_response_d3("AT+QGPSLOC=2", "+QGPSLOC:", 5000, false,
                                         &g_bg95.response))
    {
#ifdef LSX_RELEASE
      printf("AT+QGPSLOC=2: %s\n", g_bg95.response.data);
#endif
      at_send_and_wait_for_response_d0("AT+QGPSEND", "OK");

      g_bg95.gnss_found = g_bg95.step2 <= g_bg95.gnss_retries;
      if (g_bg95.gnss_found)
      {
        parse_and_set_gnss(&g_bg95.response, 0);
      }
      else
      {
        if (g_bg95.use_extra_file_on_search && g_bg95.gnss_try_to_found_count >= 3)
        {
          if (!g_bg95.extra_file_enabled)
          {
            g_bg95.extra_file_enabled = true;
            bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
            g_bg95.activated_restart = true;
          }
        }
#if defined(LSX_C3_MINI) || defined(LSX_S3)
        led_set(LED_GPS, 127, 0, 0);
#endif
      }

      if (g_bg95.state != NETWORK_STATE_MODEM_RESTART)
      {
        bg95_proceed(&g_bg95);
      }
    }
    else
    {
#ifdef LSX_RELEASE
      printf("AT+QGPSLOC=2: %s\n", g_bg95.response.data);
#endif
      delay_task(3000);
      g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
    }
  }
  else
  {
    if (!g_bg95.network_initialized)
    {
      bg95_reset(&g_bg95, NETWORK_STATE_NETWORK_INITIALIZATION);
    }
    else if (!g_bg95.mqtt_connected)
    {
      at_send_and_wait_for_response_d2("AT+QGPSCFG=\"priority\",1", "OK", 40000, true);
      delay_task(1000);
      at_send_and_wait_for_response_d2("AT+QGPSCFG=\"priority\"", "+QGPSCFG:", 40000,
                                       true);

      FOR_FF;
      bg95_reset(&g_bg95, NETWORK_STATE_MQTT_CONNECT);
    }
    else
    {
      set_mqtt_listen();
    }
  }
}

static void set_last_sent(void)
{
  if (!string64_equal(&g_bg95.channel, &g_bg95.last_sent_channel))
  {
    g_bg95.last_sent_channel = g_bg95.channel;
  }
  if (!string64_equal(&g_bg95.oper, &g_bg95.last_sent_oper))
  {
    g_bg95.last_sent_oper = g_bg95.oper;
  }
  if (!string64_equal(&g_bg95.tech, &g_bg95.last_sent_tech))
  {
    g_bg95.last_sent_tech = g_bg95.tech;
  }
  if (!string64_equal(&g_bg95.ip, &g_bg95.last_sent_ip))
  {
    g_bg95.last_sent_ip = g_bg95.ip;
  }
  if (!string64_equal(&g_bg95.ccid, &g_bg95.last_sent_ccid))
  {
    g_bg95.last_sent_ccid = g_bg95.ccid;
  }
  g_bg95.last_sent_rssi = g_bg95.rssi;
  g_bg95.startup_ack = false;
}

static void send_message(const char* topic, bool status_included,
                         bool dali_status_included)
{
#if defined(LSX_ZHAGA_DALI)
  if (g_bg95.led_is_on)
#endif
  {
    led_set(LED_LTE, 0, 0, 127);
  }

  uint32_t send_retries = 0;
  while (send_retries++ <= MAX_SEND_RETRIES && !at_disconnected_flag())
  {
    esp_task_wdt_reset();

    if (at_send_and_wait_for_response(topic, ">", 40000, true, NULL, false, true))
    {
      const char ctrl_z = '\x1A';
      g_bg95.message.data[g_bg95.message.length] = ctrl_z;
      g_bg95.message.data[g_bg95.message.length + 1] = '\0';
      if (at_send_and_wait_for_response(g_bg95.message.data, "+QMTPUB:", 60000, true,
                                        &g_bg95.response, true, true))
      {
        if (string2048_find(&g_bg95.response, "+QMTPUB: 0,0,0", 0, -1) != -1)
        {
          if (status_included)
          {
            set_last_sent();
          }
          if (dali_status_included)
          {
            g_bg95.last_sent_light_status = g_bg95.current_sent_light_status;
          }
          SET_MQTT_LISTEN_LTE_LED;
          return;
        }
      }
      lsx_log("AT+QMTPUB: %s\n", g_bg95.response.data);
    }
    delay_task(3000);
  }
  delay_task(2000);
  bg95_reset(&g_bg95, NETWORK_STATE_CHECK_SUBSCRIPTION);
  g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);

  SET_MQTT_LISTEN_LTE_LED;
}

static void send_message_as_master(send_as_master_t send_as_master)
{
  string256_t topic = {};
  string256(&topic, "AT+QMTPUB=0,0,0,0,\"%s%s%s%s\"", g_lux_beginning.data, g_deu.data,
            g_bg95.uid.data, g_dl.data);

  uint8_t buffer[2] = {};
  const uint16_t message_type = MESSAGE_TYPE_INSERT_LUX;
  copy_uint16_to_byte_buffer_big_endian(message_type, buffer, 0);

  char message_type_as_hex[(sizeof(message_type) * 2) + 1] = { 0 };
  byte_to_hex(buffer, sizeof(message_type), message_type_as_hex, false);

  cJSON* json = cJSON_CreateObject();

  cJSON* lux_json = json_add_object(json, message_type_as_hex);
  json_add_number(lux_json, "L", (double)send_as_master.lux);
  json_add_number(lux_json, "V", (double)send_as_master.velocity);
  json_add_number(lux_json, "T", (double)send_as_master.time_stamp);

  cJSON* old_lux_json = cJSON_AddArrayToObject(json, "downlinks");
  cJSON* item = cJSON_CreateObject();
  uint8_t lux_buffer[MESSAGE_LENGTH_OLD] = {};
  lux_buffer[0] = COM_LUX;
  memcpy(lux_buffer + POS_T1, &send_as_master.velocity, sizeof(float));
  memcpy(lux_buffer + POS_T2, &send_as_master.lux, sizeof(float));
  string64_t base64 = {};
  base64_encode(lux_buffer, MESSAGE_LENGTH_OLD, &base64);
  json_add_string(item, "frm_payload", base64.data);
  cJSON_AddItemToArray(old_lux_json, item);

  const uint32_t string_total_size = sizeof(g_bg95.message.data) - 3;
  string2048_reset(&g_bg95.message);

  cJSON_PrintPreallocated(json, g_bg95.message.data, string_total_size, false);
  cJSON_Delete(json);

  string2048_set_length(&g_bg95.message);

  send_message(topic.data, false, false);
}

static cJSON* add_operator_report(cJSON* current)
{
  json_add_string(current, "O", g_bg95.oper.data);
  json_add_string(current, "C", g_bg95.channel.data);
  json_add_string(current, "T", g_bg95.tech.data);
  return current;
}

const char* g_han_objects[] = { "AE",   "RE",   "L1AE", "L1RE", "L1PV", "L1PC",
                                "L2AE", "L2RE", "L2PV", "L2PC", "L3AE", "L3RE",
                                "L3PV", "L3PC", "AF",   "RF" };
uint32_t g_han_zero_counts[array_size(g_han_objects)] = {};

static void encode_json_message(cJSON* json, string256_t* string, bool* status_included,
                                bool* dali_status_included,
                                send_as_master_t* send_as_master)
{
  cJSON* parent = cJSON_ParseWithLength(string->data, string->length);
  if (parent == NULL)
  {
    return;
  }
  cJSON* current = parent->child;
  if ((current == NULL) || (current->string == NULL) || (strlen(current->string) != 4))
  {
    return;
  }

  string32_t current_string_copy = {};
  string32(&current_string_copy, "%s", current->string);

  uint16_t message_type = uint16_from_hex_string(current->string);
  switch (message_type)
  {
    case MESSAGE_TYPE_STATUS_REPORT:
    {
      if (g_bg95.startup_ack)
      {
        json_add_number(current, "U", 1);
        json_add_string_message_type(parent, MESSAGE_TYPE_VERSION_REPORT, VERSION);
      }
      json_add_number(current, "T", (double)lsx_get_time());

      if (!string64_equal(&g_bg95.ip, &g_bg95.last_sent_ip))
      {
        json_add_string_message_type(parent, MESSAGE_TYPE_IP_REPORT, g_bg95.ip.data);
      }

      const bool different_operator =
        !string64_equal(&g_bg95.oper, &g_bg95.last_sent_oper);
      const bool different_channel =
        !string64_equal(&g_bg95.channel, &g_bg95.last_sent_channel);
      const bool different_tech =
        !string64_equal(&g_bg95.channel, &g_bg95.last_sent_channel);

      if (different_operator || different_channel || different_tech)
      {
        add_operator_report(
          json_add_object_message_type(parent, MESSAGE_TYPE_OPERATOR_REPORT));
      }

      if (g_bg95.rssi != g_bg95.last_sent_rssi)
      {
        json_add_number_message_type(parent, MESSAGE_TYPE_RSSI_REPORT,
                                     (double)g_bg95.rssi);
      }
      if (!string64_equal(&g_bg95.ccid, &g_bg95.last_sent_ccid))
      {
        json_add_string_message_type(parent, MESSAGE_TYPE_CCID_REPORT,
                                     g_bg95.ccid.data);
      }
      (*status_included) = true;
      break;
    }
    case MESSAGE_TYPE_OPERATOR_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             add_operator_report(cJSON_CreateObject()));
      break;
    }
    case MESSAGE_TYPE_IP_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             cJSON_CreateString(g_bg95.ip.data));
      break;
    }
    case MESSAGE_TYPE_RSSI_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             cJSON_CreateNumber((double)g_bg95.rssi));
      break;
    }
    case MESSAGE_TYPE_CCID_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             cJSON_CreateString(g_bg95.ccid.data));
      break;
    }
    case MESSAGE_TYPE_LUX_REPORT:
    {
      cJSON* set_json = cJSON_GetObjectItemCaseSensitive(current, "S");
      if (g_bg95.is_master && json_number_is_ok(set_json) && (set_json->valueint != 0))
      {
        cJSON* lux_json = cJSON_GetObjectItemCaseSensitive(current, "L");
        cJSON* velocity_json = cJSON_GetObjectItemCaseSensitive(current, "V");
        cJSON* timestamp_json = cJSON_GetObjectItemCaseSensitive(current, "T");
        if (json_number_is_ok(lux_json) && json_number_is_ok(velocity_json) &&
            json_number_is_ok(timestamp_json))
        {
          send_as_master->should_send = true;
          send_as_master->lux = (float)lux_json->valuedouble;
          send_as_master->velocity = (float)velocity_json->valuedouble;
          send_as_master->time_stamp = (uint32_t)timestamp_json->valuedouble;
        }
      }
      break;
    }
    case MESSAGE_TYPE_REQUEST_MASTER_SLAVE:
    {
      uint8_t status = g_bg95.is_master ? 2 : (g_bg95.master_uid.length != 0) ? 1 : 0;
      cJSON* master_slave = cJSON_CreateObject();
      json_add_number(master_slave, "S", status);
      if (status == 1)
      {
        json_add_string(master_slave, "MU", g_bg95.master_uid.data);
      }
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             master_slave);
      break;
    }
    case MESSAGE_TYPE_NTP_SERVER_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(
        parent, current_string_copy.data, cJSON_CreateString(g_bg95.ntp_server.data));
      break;
    }
    case MESSAGE_TYPE_OPERATORS_REPORT:
    {
      cJSON* operator_array = cJSON_CreateArray();
      for (uint32_t i = 0; i < g_bg95.operator_count; ++i)
      {
        string128_t temp = {};
        string128(&temp, "%s, %s", g_bg95.operators[i].name.data,
                  g_bg95.operators[i].short_name.data);
        cJSON_AddItemToArray(operator_array, cJSON_CreateString(temp.data));
      }
      if (cJSON_GetObjectItemCaseSensitive(parent, current_string_copy.data) != NULL)
      {
        cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                               operator_array);
      }
      break;
    }
    case MESSAGE_TYPE_SEND_AT_COMMAND:
    {
      g_bg95.response_to_send_back.data[sizeof(g_bg95.response_to_send_back.data) - 1] =
        '\0';
      cJSON_ReplaceItemInObjectCaseSensitive(
        parent, current_string_copy.data,
        cJSON_CreateString(g_bg95.response_to_send_back.data));
      break;
    }
    case MESSAGE_TYPE_VERSION_REPORT:
    {
      cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                             cJSON_CreateString(VERSION));
      break;
    }
    case MESSAGE_TYPE_LIGHT_STATUS_REPORT:
    {
      *dali_status_included = module_set_light_status_json(parent);
      break;
    }
    case MESSAGE_TYPE_LIGHT_CONFIG_REPORT:
    {
      module_set_light_config_json(parent);
      break;
    }
    case MESSAGE_TYPE_LIGHT_SEND_QUERY_COMMAND:
    {
      // module_set_light_send_query_command(parent, buffer);
      break;
    }
    case MESSAGE_TYPE_LIGHT_FULL_DIAGNOSTIC:
    {
      module_set_light_diagnostic_json(parent);
      break;
    }
    case MESSAGE_TYPE_HAN_REPORT:
    {
#if defined(LSX_C3_MINI)
      han_snapshot_t han_snapshot = han_get_snapshot();
      bool any_values_sent = false;
      for (uint32_t i = 0; i < array_size(g_han_objects); ++i)
      {
        double value = han_snapshot.values[i];
        if (value != 0.0f)
        {
          json_add_number(current, g_han_objects[i], (double)value);
          any_values_sent = true;
        }
      }
      if (!any_values_sent)
      {
        cJSON_ReplaceItemInObjectCaseSensitive(parent, current_string_copy.data,
                                               cJSON_CreateObject());
        current = cJSON_GetObjectItemCaseSensitive(parent, current_string_copy.data);
      }
      if (current != NULL)
      {
        if (han_snapshot.corrupt_count > 0)
        {
          json_add_number(current, "CC", (double)han_snapshot.corrupt_count);
        }
        if (han_snapshot.corrupted_lines > 0)
        {
          json_add_number(current, "CL", (double)han_snapshot.corrupted_lines);
        }
      }
#endif
      break;
    }
    default:
    {
      break;
    }
  }
  if (message_type == MESSAGE_TYPE_RULE_REPORT)
  {
    cJSON* detached = cJSON_DetachItemViaPointer(parent, current);

    cJSON* rule_object =
      cJSON_GetObjectItemCaseSensitive(json, current_string_copy.data);
    if (rule_object != NULL)
    {
      if (cJSON_IsArray(rule_object))
      {
        cJSON* temp = NULL;
        cJSON_ArrayForEach(temp, rule_object)
        {
          uint8_t prio = (uint8_t)json_object_get_uint(temp, "P", NO_OF_RULES + 1);
          uint8_t current_prio =
            (uint8_t)json_object_get_uint(detached, "P", UINT8_MAX);
          if (prio == current_prio)
          {
            lsx_log("Relpaced rule\n");
            cJSON* replacee = cJSON_DetachItemViaPointer(rule_object, temp);
            cJSON_Delete(replacee);
            break;
          }
        }
        cJSON_AddItemToArray(rule_object, detached);
      }
      else
      {
        rule_object = cJSON_CreateArray();
        cJSON_AddItemToArray(rule_object, detached);
        cJSON_ReplaceItemInObjectCaseSensitive(json, current_string_copy.data,
                                               rule_object);
      }
    }
    else
    {
      rule_object = cJSON_CreateArray();
      if (cJSON_GetObjectItemCaseSensitive(detached, "P") != NULL)
      {
        cJSON_AddItemToArray(rule_object, detached);
      }
      cJSON_AddItemToObject(json, current_string_copy.data, rule_object);
    }
  }
  else if (message_type != MESSAGE_TYPE_NULL)
  {
    current = parent->child;
    while ((current != NULL) && (current->string != NULL))
    {
      cJSON* next = current->next;

      cJSON* detached = cJSON_DetachItemViaPointer(parent, current);
      string32_t string_copy = {};
      string32(&string_copy, "%s", detached->string);

      if (cJSON_GetObjectItemCaseSensitive(json, string_copy.data) != NULL)
      {
        cJSON_ReplaceItemInObjectCaseSensitive(json, string_copy.data, detached);
      }
      else
      {
        cJSON_AddItemToObject(json, string_copy.data, detached);
      }
      current = next;
    }
  }
  cJSON_Delete(parent);
}

static bool send_lux_unsubscribe_command(void)
{
  string32_t s_internal = {};

  setting_string2(&s_internal, g_bg95.extra, 3, o_c('+', 0, g_bg95.extra),
                  o_c('Q', 1, g_bg95.extra), o_c('M', 2, g_bg95.extra),
                  o_c('T', 3, g_bg95.extra), o_c('U', 5, g_bg95.extra),
                  o_c('N', 6, g_bg95.extra), o_c('S', 7, g_bg95.extra));

  string128_t unsub_command = {};
  string128(&unsub_command, "%s%s=0,1,%s", g_bg95.start.data, s_internal.data,
            get_lux_subscribe_topic());

  string32_t expect = {};
  string32(&expect, "%s:", s_internal.data);

  at_send_and_wait_for_response(unsub_command.data, expect.data, 60000, false,
                                &g_bg95.response, false, true);

  string32(&expect, "%s: 0,1,0", s_internal.data);
  return string2048_find(&g_bg95.response, expect.data, 0, -1) != -1;
}

static bool send_unsubscribe_command(void)
{
  string32_t s_internal = {};

  setting_string2(&s_internal, g_bg95.extra, 3, o_c('+', 0, g_bg95.extra),
                  o_c('Q', 1, g_bg95.extra), o_c('M', 2, g_bg95.extra),
                  o_c('T', 3, g_bg95.extra), o_c('U', 5, g_bg95.extra),
                  o_c('N', 6, g_bg95.extra), o_c('S', 7, g_bg95.extra));

  string128_t unsub_command = {};
  string128(&unsub_command, "%s%s=0,1,%s", g_bg95.start.data, s_internal.data,
            get_subscribe_topic());

  string32_t expect = {};
  string32(&expect, "%s:", s_internal.data);

  at_send_and_wait_for_response(unsub_command.data, expect.data, 60000, false,
                                &g_bg95.response, false, true);

  string32(&expect, "%s: 0,1,0", s_internal.data);
  bool success = string2048_find(&g_bg95.response, expect.data, 0, -1) != -1;
  if (g_bg95.master_uid.length != 0)
  {
    success = send_lux_unsubscribe_command() && success;
  }
  return success;
}

static void message_run_update(const cJSON* json)
{
  const cJSON* product_object = cJSON_GetObjectItemCaseSensitive(json, "P");
  const cJSON* veriant_object = cJSON_GetObjectItemCaseSensitive(json, "v");
  const cJSON* version_object = cJSON_GetObjectItemCaseSensitive(json, "V");
  const cJSON* time_object = cJSON_GetObjectItemCaseSensitive(json, "TOTI");

  if (json_string_is_ok(product_object) && json_string_is_ok(veriant_object) &&
      json_string_is_ok(version_object))
  {
    uint32_t timeout = 5400;
    if (json_number_is_ok(time_object))
    {
      uint32_t timeout_index = time_object->valueint;
      switch (timeout_index)
      {
        case 1:
        {
          // NOTE(Linus): 1h 0m
          timeout = 3600;
          break;
        }
        case 2:
        {
          // NOTE(Linus): 1h 45m
          timeout = 6300;
          break;
        }
        case 3:
        {
          // NOTE(Linus): 2h 00m
          timeout = 7200;
          break;
        }
        case 4:
        {
          // NOTE(Linus): 2h 30m
          timeout = 9000;
          break;
        }
        case 5:
        {
          // NOTE(Linus): 3h 00m
          timeout = 10800;
          break;
        }
        case 6:
        {
          // NOTE(Linus): 3h 30m
          timeout = 12600;
          break;
        }
        case 7:
        {
          // NOTE(Linus): 4h 00m
          timeout = 14400;
          break;
        }
        default:
        {
          // NOTE(Linus): 1h 30m
          timeout = 5400;
          break;
        }
      }
    }
    string64_t product = {};
    string64_t veriant = {};
    string64_t version = {};
    if (string64(&product, product_object->valuestring) &&
        string64(&veriant, veriant_object->valuestring) &&
        string64(&version, version_object->valuestring))
    {
      send_unsubscribe_command();

      json_send_empty_to_queue(g_bg95.incoming_queue, MESSAGE_TYPE_STORE_BURN_TIMES,
                               1000);

      delay_task(5000);

      at_clear();

      LedRGB lte_before = led_get(LED_LTE);
#if defined(LSX_C3_MINI) || defined(LSX_S3)
      LedRGB gps_before = led_get(LED_GPS);
#endif

      relay_control_set_pulse_led(false);

      update_firmware(product.data, veriant.data, version.data, timeout);

      relay_control_set_pulse_led(true);

      vTaskDelay(pdMS_TO_TICKS(6000));

#ifdef LSX_ZHAGA_DALI
      if (g_bg95.led_is_on)
#endif
      {
        led_set(LED_LTE, lte_before.r, lte_before.g, lte_before.b);
#if defined(LSX_C3_MINI) || defined(LSX_S3)
        led_set(LED_GPS, gps_before.r, gps_before.g, gps_before.b);
#endif
      }

      g_bg95.for_gone = true;
      bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
      g_bg95.activated_restart = true;
    }
  }
}

static void json_send_child_to_queue(const char* key, cJSON* json, QueueHandle_t queue)
{
  string32_t temp2 = {};
  string32(&temp2, "{\"%s\":", key);

  string256_t temp = {};
  cJSON_PrintPreallocated(json, temp.data + temp2.length,
                          sizeof(temp.data) - (temp2.length + 1), false);
  memcpy(temp.data, temp2.data, temp2.length);
  string256_set_length(&temp);
  temp.data[temp.length++] = '}';
  temp.data[temp.length] = '\0';

  xQueueSend(queue, temp.data, pdMS_TO_TICKS(600));
}

static void message_search_position_again(const cJSON* json)
{
  reset_gnss(&g_bg95);
  g_bg95.retry_gnss = true;
#if defined(LSX_C3_MINI) || defined(LSX_S3)
  led_set(LED_GPS, 127, 0, 0);
#endif
  g_bg95.use_extra_file_on_search = false;
  if (json_number_is_ok(json))
  {
    g_bg95.use_extra_file_on_search = (bool)json->valueint;
  }
  bg95_reset(&g_bg95, NETWORK_STATE_GNSS);
}

static void set_store_slave_topic(string64_t* old_value, const char* new_value,
                                  const char* nvs_key)
{
  string64_t new_string = {};
  string64(&new_string, new_value);
  if (new_string.length != 0)
  {
    if (!string64_equal(old_value, &new_string))
    {
      lsx_nvs_set_string(&g_bg95.nvs, nvs_key, new_string.data);
      if (!string64_is_empty(old_value) &&
          (g_bg95.state != NETWORK_STATE_MODEM_RESTART))
      {
        send_lux_unsubscribe_command();
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        g_bg95.activated_restart = true;
      }
      else if (g_bg95.state != NETWORK_STATE_MODEM_RESTART)
      {
        bg95_reset(&g_bg95, NETWORK_STATE_CHECK_SUBSCRIPTION);
      }
      string64_copy(old_value, &new_string);
    }
    else if (g_bg95.state != NETWORK_STATE_MODEM_RESTART)
    {
      bg95_reset(&g_bg95, NETWORK_STATE_CHECK_SUBSCRIPTION);
    }
  }
}

static void message_set_master_slave(const cJSON* json)
{
  const cJSON* status_object = cJSON_GetObjectItemCaseSensitive(json, "S");
  const cJSON* master_uid = cJSON_GetObjectItemCaseSensitive(json, "MU");

  uint8_t status = 0;
  if (json_number_is_ok(status_object))
  {
    status = (uint8_t)status_object->valueint;

    const bool new_master = (status == 2);
    if (new_master != g_bg95.is_master)
    {
      lsx_log("Store master: %u\n", new_master);
      lsx_nvs_set_uint8(&g_bg95.nvs, "Master", (uint8_t)new_master);
    }
    g_bg95.is_master = new_master;
  }
  if (status == 1 && json_string_is_ok(master_uid))
  {
    set_store_slave_topic(&g_bg95.master_uid, master_uid->valuestring, "Slave");
  }
  else if (g_bg95.master_uid.length != 0)
  {
    lsx_nvs_remove(&g_bg95.nvs, "Slave");
    send_lux_unsubscribe_command();
    string64_reset(&g_bg95.master_uid);
    bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
    g_bg95.activated_restart = true;
  }
  bg95_send_set_master_message(&g_bg95);
  json_send_empty_to_queue(g_bg95.outgoing_queue, MESSAGE_TYPE_REQUEST_MASTER_SLAVE,
                           1000);
}

static void message_send_at_command(const cJSON* json)
{
  const cJSON* command_object = cJSON_GetObjectItemCaseSensitive(json, "C");
  const cJSON* result_object = cJSON_GetObjectItemCaseSensitive(json, "R");
  const cJSON* timeout_object = cJSON_GetObjectItemCaseSensitive(json, "T");
  if (json_string_is_ok(command_object) && json_string_is_ok(result_object) &&
      json_number_is_ok(timeout_object))
  {
    string256_t at_command = {};
    string256_t expected_result = {};
    string256(&at_command, command_object->valuestring);
    string256(&expected_result, result_object->valuestring);
    const uint32_t timeout_s = (uint32_t)timeout_object->valueint;

    if (timeout_s <= 600)
    {
      at_send_and_wait_for_response_d6(at_command.data, expected_result.data,
                                       timeout_s * 1000, &g_bg95.response);
      if (g_bg95.response.length != 0)
      {
        string256_reset(&g_bg95.response_to_send_back);
        uint32_t to_send =
          min(g_bg95.response.length, sizeof(g_bg95.response_to_send_back.data) - 1);

        memcpy(g_bg95.response_to_send_back.data, g_bg95.response.data, to_send);

        g_bg95.response_to_send_back.length = to_send;
        g_bg95.response_to_send_back.data[to_send] = '\0';
        if (to_send != 0)
        {
          if (g_bg95.response_to_send_back.data[to_send - 1] == '\r')
          {
            g_bg95.response_to_send_back.data[--to_send] = '\0';
          }
        }
        json_send_empty_to_queue(g_bg95.outgoing_queue, MESSAGE_TYPE_SEND_AT_COMMAND,
                                 1000);
      }
    }
  }
}

static void message_set_ntp_server(const cJSON* json)
{
  if (json_string_is_ok(json))
  {
    string128_t ntp = {};
    string128(&ntp, json->valuestring);

    if (!string128_equal(&ntp, &g_bg95.ntp_server) &&
        send_ntp_command(ntp.data, 120000))
    {
      string128_copy(&g_bg95.ntp_server, &ntp);
      lsx_nvs_set_string(&g_bg95.nvs, "ntp", g_bg95.ntp_server.data);
    }
  }
  json_send_empty_to_queue(g_bg95.outgoing_queue, MESSAGE_TYPE_NTP_SERVER_REPORT, 1000);
}

static void json_send_single_value_to_outgoing(cJSON* current, double value)
{
  cJSON* json = cJSON_CreateObject();
  json_add_number(json, current->string, value);
  json_send_to_queue_and_delete(json, g_bg95.outgoing_queue, 1000);
}

static void parse_incoming_message(const char* message, uint32_t message_length,
                                   bool lux_message)
{
  cJSON* json = cJSON_ParseWithLength(message, message_length);
  if (json == NULL)
  {
    return;
  }

  const uint32_t max_iterations = 64;
  uint32_t iteration = 0;

  cJSON* current = json->child;
  while ((current != NULL) && (iteration++ < max_iterations))
  {
    uint16_t key = MESSAGE_TYPE_NULL;
    if ((current->string != NULL) && (strlen(current->string) == 4))
    {
      lsx_log("%s\n", current->string);
      key = uint16_from_hex_string(current->string);
    }
    if (key == UINT16_MAX)
    {
      key = MESSAGE_TYPE_NULL;
    }
    switch (key)
    {
      case MESSAGE_TYPE_NULL:
      {
#if 0
        if (lux_message && (current->string != NULL) &&
            (strcmp(current->string, "downlinks") == 0))
        {
          cJSON* child = current->child;
          if (cJSON_IsArray(current) && child)
          {
            child = cJSON_GetObjectItemCaseSensitive(child, "frm_payload");
            if (json_string_is_ok(child))
            {
              uint8_t* byte_buffer[64] = {};
              base64_decode(child->valuestring, strlen(child->valuestring), byte_buffer,
                            sizeof(byte_buffer));

              if (byte_buffer[0] == COM_LUX)
              {
                float lux = 0.0f;
                memcpy(&lux, byte_buffer + POS_T2, sizeof(lux));
              }
            }
          }
        }
#endif
        break;
      }
      case MESSAGE_TYPE_UPDATE_FIRMWARE:
      {
        message_run_update(current);
        break;
      }
      case MESSAGE_TYPE_SEARCH_GPS_AGAIN:
      {
        message_search_position_again(current);
        break;
      }
      case MESSAGE_TYPE_SET_MASTER_SLAVE:
      {
        message_set_master_slave(current);
        break;
      }
      case MESSAGE_TYPE_LIGHT_SEND_QUERY_COMMAND:
      {
        json_send_child_to_queue(current->string, current, g_bg95.outgoing_queue);
        break;
      }
      case MESSAGE_TYPE_SYNC_WITH_NTP_SERVER:
      {
        send_ntp_command(g_bg95.ntp_server.data, 120000);
        json_send_empty_to_queue(g_bg95.incoming_queue, MESSAGE_TYPE_STATUS_REPORT,
                                 1000);
        break;
      }
      case MESSAGE_TYPE_SET_NTP_SERVER:
      {
        message_set_ntp_server(current);
        break;
      }
      case MESSAGE_TYPE_OPERATORS_REPORT:
      {
        send_search_for_operator_command();
        bg95_reset(&g_bg95, NETWORK_STATE_CHECK_SUBSCRIPTION);
        json_send_empty_to_queue(g_bg95.outgoing_queue, MESSAGE_TYPE_OPERATORS_REPORT,
                                 1000);
        break;
      }
      case MESSAGE_TYPE_SEARCH_AND_SELECT_OPERATOR:
      {
        look_for_and_select_new_operator();
        json_send_empty_to_queue(g_bg95.incoming_queue, MESSAGE_TYPE_STATUS_REPORT,
                                 1000);
        break;
      }
      case MESSAGE_TYPE_SELECT_NEW_OPERATOR:
      {
        const string64_t last_operator = g_bg95.oper;
        if (json_string_is_ok(current))
        {
          if (!select_operator(current->valuestring, false))
          {
            select_operator(last_operator.data, false);
          }
        }
        json_send_empty_to_queue(g_bg95.incoming_queue, MESSAGE_TYPE_STATUS_REPORT,
                                 1000);
        break;
      }
      case MESSAGE_TYPE_SEND_AT_COMMAND:
      {
        message_send_at_command(current);
        break;
      }
      case MESSAGE_TYPE_CLEAR_ESP:
      case MESSAGE_TYPE_CLEAR_AND_RESTART_ESP:
      {
        lsx_nvs_clear(&g_bg95.nvs);
        light_control_reset();
        vTaskDelay(pdMS_TO_TICKS(2500));
        json_send_child_to_queue(current->string, current, g_bg95.incoming_queue);
        break;
      }
      case MESSAGE_TYPE_RESTART_BG95:
      {
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        g_bg95.activated_restart = true;
        break;
      }
      case MESSAGE_TYPE_LIGHT_SET_CONFIG:
      {
        module_parse_config_message(current);
        json_send_empty_to_queue(g_bg95.outgoing_queue,
                                 MESSAGE_TYPE_LIGHT_CONFIG_REPORT, 1000);
        break;
      }
      case MESSAGE_TYPE_SET_AUTO_NTP_SERVER_UPDATE:
      {
        if (json_number_is_ok(current))
        {
          const uint8_t auto_ntp = (uint8_t)current->valueint;
          if (auto_ntp < 2)
          {
            if (((uint8_t)g_bg95.auto_ntp_updates) != auto_ntp)
            {
              lsx_nvs_set_uint8(&g_bg95.nvs, "NAU", auto_ntp);
            }
            g_bg95.auto_ntp_updates = (bool)auto_ntp;
          }
        }
        json_send_single_value_to_outgoing(current, (double)g_bg95.auto_ntp_updates);
        break;
      }
      case MESSAGE_TYPE_SET_KEEPALIVE:
      {
        if (json_number_is_ok(current))
        {
          const uint32_t keepalive = (uint32_t)current->valueint;
          if (keepalive >= 120)
          {
            if (g_bg95.keepalive != keepalive)
            {
              lsx_nvs_set_uint32(&g_bg95.nvs, "KA", keepalive);
              bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
            }
            g_bg95.keepalive = keepalive;
          }
        }
        json_send_single_value_to_outgoing(current, (double)g_bg95.keepalive);
        break;
      }
      case MESSAGE_TYPE_HAN_REPORT:
      {
#if defined(LSX_C3_MINI)
        han_send_status(false);
#endif
        break;
      }
      case MESSAGE_TYPE_INSERT_RULE:
      {
        uint32_t rule_count = 0;
        const uint32_t max_rules = NO_OF_RULES;
        cJSON* rule_object = NULL;
        cJSON_ArrayForEach(rule_object, current)
        {
          if (++rule_count >= max_rules)
          {
            break;
          }
          json_send_child_to_queue(current->string, rule_object, g_bg95.incoming_queue);
          vTaskDelay(pdMS_TO_TICKS(10));
        }
        break;
      }
      case MESSAGE_TYPE_INSERT_POS:
      {
        json_send_child_to_queue(current->string, current, g_bg95.incoming_queue);

        g_bg95.gnss_found = true;
        g_bg95.gnss_try_to_found_count = 0;
#if defined(LSX_C3_MINI) || defined(LSX_S3)
        led_set(LED_GPS, 127, 68, 0);
#endif
        if (g_bg95.extra_file_enabled)
        {
          g_bg95.extra_file_enabled = false;
          bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
          g_bg95.activated_restart = true;
        }
        break;
      }
      case MESSAGE_TYPE_CCID_REPORT:
      case MESSAGE_TYPE_IP_REPORT:
      case MESSAGE_TYPE_OPERATOR_REPORT:
      case MESSAGE_TYPE_RSSI_REPORT:
      case MESSAGE_TYPE_REQUEST_MASTER_SLAVE:
      case MESSAGE_TYPE_NTP_SERVER_REPORT:
      case MESSAGE_TYPE_LIGHT_CONFIG_REPORT:
      case MESSAGE_TYPE_LIGHT_STATUS_REPORT:
      case MESSAGE_TYPE_LIGHT_FULL_DIAGNOSTIC:
      case MESSAGE_TYPE_VERSION_REPORT:
      {
        json_send_empty_to_queue(g_bg95.outgoing_queue, key, 1000);
        break;
      }
      default:
      {
        json_send_child_to_queue(current->string, current, g_bg95.incoming_queue);
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    current = current->next;
  }
  cJSON_Delete(json);
}

static void bg95_mqtt_listen(void)
{
  QueueHandle_t recv_queue = (QueueHandle_t)at_recv_queue();

  const uint32_t ms = lsx_get_millis();

  g_bg95.last_connected_timer.time = ms;
  g_bg95.last_connected_timer.duration = LAST_CONNECTED_TIME_MARK;

#if defined(LSX_ZHAGA_DALI)
  if ((g_bg95.retry_gnss ||) && !g_bg95.gnss_skip && !g_bg95.gnss_found &&
      timer_is_up_ms(g_bg95.gnss_retry_timer, ms))
  {
#else
  if (!g_bg95.gnss_skip && !g_bg95.gnss_found &&
      timer_is_up_ms(g_bg95.gnss_retry_timer, ms))
  {
#endif
    if (g_bg95.gnss_try_to_found_count >= 8)
    {
      g_bg95.gnss_try_to_found_count = 0;
      g_bg95.gnss_skip = true; // NOTE(Linus): Give up the search.
      if (g_bg95.extra_file_enabled)
      {
        g_bg95.extra_file_enabled = false;
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        g_bg95.activated_restart = true;
      }
    }
    else
    {
      bg95_reconnect_gnss(&g_bg95);
    }
  }
  else if (xQueueReceive(recv_queue, g_bg95.response.data, pdMS_TO_TICKS(100)) ==
           pdPASS)
  {
    lsx_log("%s\n", g_bg95.response.data);

    if (!g_bg95.gnss_found)
    {
      if ((g_bg95.gnss_retry_timer.time + 5000) > ms)
      {
        g_bg95.gnss_retry_timer.time = ms;
      }
      else
      {
        g_bg95.gnss_retry_timer.time += 5000;
      }
    }

    g_bg95.response.length =
      length_of_constant_string(g_bg95.response.data, sizeof(g_bg95.response.data) - 1);

    bool lux_message = false;

    string32_t down_token = {};
    string32(&down_token, "%s\",\"", g_dp.data);
    int32_t message_index =
      string2048_find(&g_bg95.response, down_token.data, 0, down_token.length);
    if (message_index == -1)
    {
      if (g_bg95.master_uid.length != 0)
      {
        string32(&down_token, "%s\",\"", g_dl.data);
        message_index =
          string2048_find(&g_bg95.response, down_token.data, 0, down_token.length);
        if (message_index == -1)
        {
          g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
          return;
        }
        else
        {
          lux_message = true;
        }
      }
      else
      {
        g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
        return;
      }
    }
    const uint32_t message_offset = message_index + down_token.length;
    const char* message = g_bg95.response.data + message_offset;

    if (g_bg95.response.length < message_offset)
    {
      return;
    }
    const uint32_t message_length = g_bg95.response.length - message_offset;

    parse_incoming_message(message, message_length, lux_message);
  }
  else
  {
    string256_t message_buffer = {};
    if (xQueueReceive(g_bg95.outgoing_queue, message_buffer.data, pdMS_TO_TICKS(100)) ==
        pdPASS)
    {
      lsx_log("%s\n", message_buffer.data);
      bool status_included = false;
      bool dali_status_included = false;
      send_as_master_t send_as_master = {};

      cJSON* outgoing_json = cJSON_CreateObject();

      string256_set_length(&message_buffer);
      encode_json_message(outgoing_json, &message_buffer, &status_included,
                          &dali_status_included, &send_as_master);
      memset(&message_buffer, 0, sizeof(message_buffer));

      uint32_t count = 0;
      uint32_t max_messages = 64;
      while ((xQueueReceive(g_bg95.outgoing_queue, message_buffer.data,
                            pdMS_TO_TICKS(1000)) == pdPASS) &&
             (count++ < max_messages))
      {
        esp_task_wdt_reset();

        lsx_log("Extra: %s\n", message_buffer.data);
        string256_set_length(&message_buffer);
        encode_json_message(outgoing_json, &message_buffer, &status_included,
                            &dali_status_included, &send_as_master);
        memset(&message_buffer, 0, sizeof(message_buffer));
      }

      string2048_reset(&g_bg95.message);
      cJSON_PrintPreallocated(outgoing_json, g_bg95.message.data,
                              sizeof(g_bg95.message.data) - 3, false);
      string2048_set_length(&g_bg95.message);

      cJSON_Delete(outgoing_json);

      if (!string2048_is_null(&g_bg95.message))
      {
        string256_t topic = {};
        string256(&topic, "AT+QMTPUB=0,0,0,0,\"%s%s%s/up\"", g_beginning.data,
                  g_deu.data, g_bg95.uid.data);
        send_message(topic.data, status_included, dali_status_included);
      }
      if (send_as_master.should_send)
      {
        send_message_as_master(send_as_master);
      }
    }
    else
    {
      g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
    }
  }
}

#if 0
static void at_change_baud_rate(uint32_t rate)
{
    at_end();
    vTaskDelay(pdMS_TO_TICKS(300));
    at_initialize(rate);
    vTaskDelay(pdMS_TO_TICKS(300));
}
#endif

static void bg95_turn_on(void)
{
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);
  vTaskDelay(pdMS_TO_TICKS(1000));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);
}

static void bg95_turn_off(void)
{
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);
  vTaskDelay(pdMS_TO_TICKS(1000));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_HIGH);
  vTaskDelay(pdMS_TO_TICKS(1000));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);
}

static void bg95_turn_off_and_on(uint32_t power_down_milli, bool read_response,
                                 bool look_for_powered_down)
{
  lsx_log("Turning bg95 off and on\n");

  bool found_app_ready = false;
  bool found_normal_power_down = false;

  bg95_turn_off();
  timer_ms_t timer = timer_create_ms(power_down_milli);
  while (!timer_is_up_and_reset_ms(&timer, lsx_get_millis()))
  {
    esp_task_wdt_reset();
    if (lsx_uart_available(1))
    {
      string2048_reset(&g_bg95.response);
      g_bg95.response.length = lsx_uart_read_until_string(
        1, g_bg95.response.data, sizeof(g_bg95.response.data), '\n', 1000, NULL);

      if (string2048_find(&g_bg95.response, "NORMAL POWER DOWN", 0, -1) != -1)
      {
        found_normal_power_down = true;
      }
      else if (string2048_find(&g_bg95.response, "APP RDY", 0, -1) != -1)
      {
        found_app_ready = true;
      }

      if (look_for_powered_down && found_normal_power_down)
      {
        break;
      }

      if (read_response)
      {
        if (string2048_find(&g_bg95.response, "+QGPSLOC:", 0, -1) != -1)
        {
          lsx_log("AT+QGPSLOC=2: %s", g_bg95.response.data);
          uint32_t offset =
            ((uint32_t)(lsx_get_millis() - g_bg95.last_called_gpsloc)) / 1000U;
          if (offset < 600)
          {
            offset += 6;
            lsx_log("Offset: %lu\n", offset);
            parse_and_set_gnss(&g_bg95.response, offset);
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (look_for_powered_down && found_app_ready && !found_normal_power_down)
  {
    return;
  }

  bg95_turn_on();
  timer = timer_create_ms(40000);
  while (!timer_is_up_ms(timer, lsx_get_millis()))
  {
    esp_task_wdt_reset();
    if (lsx_uart_available(1))
    {
      string2048_reset(&g_bg95.response);
      g_bg95.response.length = lsx_uart_read_until_string(
        1, g_bg95.response.data, sizeof(g_bg95.response.data), '\n', 1000, NULL);
      if (string2048_find(&g_bg95.response, "APP RDY", 0, -1) != -1)
      {
        return;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

static void bg95_backup_startup(void)
{
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);
  vTaskDelay(pdMS_TO_TICKS(1000));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_HIGH);
  vTaskDelay(pdMS_TO_TICKS(2000));
  lsx_gpio_write(BG95_PIN, LSX_GPIO_LOW);

  timer_ms_t timer = timer_create_ms(30000);
  while (!timer_is_up_ms(timer, lsx_get_millis()))
  {
    esp_task_wdt_reset();
    if (lsx_uart_available(1))
    {
      string2048_reset(&g_bg95.response);
      g_bg95.response.length = lsx_uart_read_until_string(
        1, g_bg95.response.data, sizeof(g_bg95.response.data), '\n', 1000, NULL);
      if (string2048_find(&g_bg95.response, "APP RDY", 0, -1) != -1)
      {
        return;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

#if 0
static void at_set_or_search_for_correct_baud_rate(void)
{
    if (!at_send_and_wait_for_response_d6("AT+IPR=115200;&W", "OK", 8000,
                                          &g_bg95.response))
    {
        if (g_bg95.response.length == 0)
        {
            bg95_turn_off_and_on(5000);
            if (!at_send_and_wait_for_response_d6("AT+IPR=115200;&W", "OK", 8000,
                                                  &g_bg95.response))
            {
                if (g_bg95.response.length == 0)
                {
                    const uint32_t baud_rates_to_test[] = {
                        230400,
                        460800,
                        921600,
                        2900000,
                    };
                    for (uint32_t i = 0; i < array_size(baud_rates_to_test); ++i)
                    {
                        at_change_baud_rate(baud_rates_to_test[i]);
                        if (!at_send_and_wait_for_response_d6("AT+IPR=115200;&W", "OK",
                                                              8000, &g_bg95.response))
                        {
                            if (g_bg95.response.length != 0)
                            {
                                break;
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    at_change_baud_rate(115200);
                }
            }
        }
    }
}
#endif

static void bg95_task(void* parameters)
{
  lsx_nvs_open(&g_bg95.nvs, "master_slave");

  vTaskDelay(pdMS_TO_TICKS(700));

  at_initialize(115200);

  bg95_create(&g_bg95);

  esp_task_wdt_add(NULL);

  // g_bg95.gnss_found = true;

#ifdef LSX_STATIC_CJSON
  g_hooks.malloc_fn = json_malloc;
  g_hooks.free_fn = json_free;
  cJSON_InitHooks(&g_hooks);
#endif

  uint8_t th[] = { 1, 5, 3 };

  lsx_gpio_config(BG95_PIN, LSX_GPIO_MODE_OUTPUT, LSX_GPIO_INTR_DISABLE, false, false);
  bg95_turn_off_and_on(5000, false, true);

  // at_set_or_search_for_correct_baud_rate();

  del_BG_firmware();

  g_bg95.band_change_time_mark = lsx_get_millis();

  g_bg95.extra_file_enabled = true;

  setting_string2(
    &g_dp, g_bg95.extra, 5, o_c('/', 0, g_bg95.extra), o_c('d', 1, g_bg95.extra),
    o_c('o', 2, g_bg95.extra), o_c('w', 3, g_bg95.extra), o_c('n', 4, g_bg95.extra),
    o_c('/', 5, g_bg95.extra), o_c('p', 7, g_bg95.extra), o_c('u', 8, g_bg95.extra),
    o_c('s', 9, g_bg95.extra), o_c('h', 10, g_bg95.extra));

  while (true)
  {
    esp_task_wdt_reset();

    string2048_reset(&g_bg95.response);

    if (at_available() > 0)
    {
      g_bg95.response.length =
        at_serial_read_line(g_bg95.response.data, sizeof(g_bg95.response.data));

      if (string2048_find(&g_bg95.response, "+QMTRECV:", 0, -1) != -1)
      {
        QueueHandle_t recv_queue = (QueueHandle_t)at_recv_queue();
        xQueueSend(recv_queue, g_bg95.response.data, pdMS_TO_TICKS(100));

        string2048_reset(&g_bg95.response);
      }
      else if ((string2048_find(&g_bg95.response, "+QMTSTAT:", 0, -1) != -1) ||
               (string2048_find(&g_bg95.response, "+QIURC: \"pdpdeact\"", 0, -1) != -1))
      {
        lsx_log("QMTSTAT!\n");

        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        g_bg95.activated_restart = false;

        string2048_reset(&g_bg95.response);
      }
      else if (string2048_find(&g_bg95.response, "+QNTP: 0,", 0, -1) != -1)
      {
        time_info_t te = {};
        if (parse_ntp_response(&g_bg95.response, &te))
        {
          const uint32_t ntp_time = (uint32_t)lsx_make_time(&te);
          lsx_log("Updated time: %lu\n", ntp_time);
          time_simple_set_time(ntp_time);
        }
      }
    }

    if (at_disconnected_flag())
    {
      lsx_log("DISCONNECTED FLAG!\n");
      bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
      g_bg95.activated_restart = false;

      string2048_reset(&g_bg95.response);
    }

    // NOTE(Linus): If the modem becomes unresponsive
    if ((at_last_sent_time() > at_last_receive_time()) &&
        ((at_last_sent_time() - at_last_receive_time()) >= 5000))
    {
      if (++g_bg95.no_response_count >= 4)
      {
        bg95_backup_startup();
        g_bg95.no_response_count = 0;
      }
      else
      {
        bg95_turn_off_and_on(5000, true, false);
      }
      g_bg95.activated_restart = false;
    }

    uint32_t ms = lsx_get_millis();

    if (timer_is_up_ms(g_bg95.time_out_timer, ms))
    {
      if (((++g_bg95.bg95_restart_count) % BG95_MAX_RESTARTS_BEFORE_QUARANTINE) == 0)
      {
        bg95_reset(&g_bg95, NETWORK_STATE_QUARANTINE);
      }
      else
      {
        g_bg95.time_out_mark =
          min(g_bg95.time_out_mark + 5000, NETWORK_TIME_OUT_MARK_END);
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
      }
      if (g_bg95.bg95_restart_count > 10)
      {
        g_bg95.bg95_restart_count = 0;
      }
      g_bg95.activated_restart = false;
    }

    uint8_t cont = 0x4F;

    if (g_bg95.auto_ntp_updates && g_bg95.mqtt_connected &&
        timer_is_up_and_reset_ms(&g_bg95.sync_time_timer, ms) &&
        (g_bg95.state != NETWORK_STATE_GNSS))
    {
      send_ntp_command(g_bg95.ntp_server.data, 10000);
    }

#if defined(LSX_ZHAGA_DALI)
    if (g_bg95.led_is_on && timer_is_up_ms(g_bg95.led_on_timer, ms))
    {
      led_set(LED_LTE, 0, 0, 0);
      g_bg95.led_is_on = false;
    }
#endif

    at_reset_disconnected_flag();

    switch (g_bg95.state)
    {
      case NETWORK_STATE_MODEM_RESTART:
      {
#if defined(LSX_ZHAGA_DALI)
        if (g_bg95.led_is_on)
#endif
        {
          // led_set(LED_LTE, 127, 0, 0);
        }

        g_bg95.mqtt_connected = false;
        g_bg95.network_initialized = false;

        uint32_t time_now = lsx_get_time();
        if ((time_now < TIME_IN_THE_PAST) && (g_bg95.modem_restart_count++ >= 4) &&
            (!g_bg95.gnss_found) && (g_bg95.gnss_try_to_found_count < 4))
        {
          g_bg95.extra_file_enabled = false;
          g_bg95.gnss_retries = 126;
          g_bg95.change_direction = !g_bg95.change_direction;
        }
        else
        {
          g_bg95.change_direction = false;
        }

        uint32_t i = 0;
        if (g_bg95.step1 == i++)
        {
          at_send_and_wait_for_response_d0("AT+QGPSCFG=\"gnssconfig\",3", "OK");
          if (g_bg95.extra_file_enabled)
          {
            at_send_and_wait_for_response_d0("AT+QGPSXTRA=1", "OK");
            at_send_and_wait_for_response_d0("AT+QGPSCFG=\"xtrafilesize\",1", "OK");
          }
          else
          {
            at_send_and_wait_for_response_d0("AT+QGPSXTRA=0", "OK");
          }
          bg95_proceed(&g_bg95);
        }
        else if (at_send_and_wait_for_response_d1("AT+CFUN=1,1", "APP RDY", 80000))
        {
          g_bg95.no_response_count = 0;
          if (g_bg95.change_direction)
          {
            bg95_reset(&g_bg95, NETWORK_STATE_GNSS);
          }
          else
          {
            bg95_reset(&g_bg95, NETWORK_STATE_NETWORK_INITIALIZATION);
          }

          if (g_bg95.for_gone)
          {
            vTaskDelay(pdMS_TO_TICKS(300));
            g_bg95.for_gone = !del_BG_firmware();
          }
        }
        at_reset_disconnected_flag();
        break;
      }
      case NETWORK_STATE_NETWORK_INITIALIZATION:
      {
        bg95_network_initialization();
        break;
      }
      case NETWORK_STATE_MQTT_CONNECT:
      {
        bg95_mqtt_connect(th, &cont);
        break;
      }
      case NETWORK_STATE_GNSS:
      {
        bg95_gnss_initialization();
        at_reset_disconnected_flag();
        break;
      }
      case NETWORK_STATE_MQTT_LISTEN:
      {
        g_bg95.modem_restart_count = 0;
        g_bg95.time_out_mark = NETWORK_TIME_OUT_MARK_START;
        g_bg95.powered_down_mark = POWER_DOWN_MARK_START;
        g_bg95.bg95_restart_count = 0;

        if (timer_is_up_ms(g_bg95.check_connection_timer, ms))
        {
          bg95_reset(&g_bg95, NETWORK_STATE_CHECK_CONNECTION);
          break;
        }
        else if (timer_is_up_ms(g_bg95.check_subscription_timer, ms))
        {
          bg95_reset(&g_bg95, NETWORK_STATE_CHECK_SUBSCRIPTION);
          break;
        }
        bg95_mqtt_listen();
        break;
      }
      case NETWORK_STATE_CHECK_SUBSCRIPTION:
      {
        uint32_t i = 0;

        if (g_bg95.step1 == i++)
        {
          if (send_subscribe_command())
          {
            bg95_proceed(&g_bg95);
          }
          else
          {
            if (++g_bg95.step2 >= 3)
            {
              g_bg95.mqtt_connected = false;
              g_bg95.network_initialized = false;
              bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
              g_bg95.activated_restart = false;
            }
            g_bg95.time_out_timer = timer_create_ms(g_bg95.time_out_mark);
            delay_task(5000);
          }
        }
        else
        {
          g_bg95.check_subscription_timer =
            timer_create_ms(CHECK_SUBSCRIPTION_TIME_MARK);
          SET_MQTT_LISTEN_LTE_LED;
          bg95_reset(&g_bg95, NETWORK_STATE_MQTT_LISTEN);
        }
        break;
      }
      case NETWORK_STATE_CHECK_CONNECTION:
      {
        uint32_t i = 0;
        if (g_bg95.step1 == i++)
        {
          bg95_proceed(&g_bg95);
          string64_t command = {};
          char temp = g_treu.data[8];
          g_treu.data[8] = '\0';
          string64(&command, "%s%s?", g_bg95.start.data, g_treu.data);
          g_treu.data[8] = temp;
          if (at_send_and_wait_for_response(command.data, g_treu.data, 60000, true,
                                            &g_bg95.response, true, true))
          {
            string32_t temp = {};
            string32(&temp, "%s 0,3", g_treu.data);
            if (string2048_find(&g_bg95.response, temp.data, 0, -1) == -1)
            {
              bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
              g_bg95.activated_restart = false;
            }
          }
        }
        else if (g_bg95.step1 == i++)
        {
          send_qnwinfo_command();
          bg95_proceed(&g_bg95);
        }
        else if (g_bg95.step1 == i++)
        {
          send_qiact_info_command();
          bg95_proceed(&g_bg95);
        }
        else if (g_bg95.step1 == i++)
        {
          send_csq_command();
          g_bg95.check_connection_timer = timer_create_ms(CHECK_CONNECTION_TIME_MARK);
          SET_MQTT_LISTEN_LTE_LED;
          bg95_reset(&g_bg95, NETWORK_STATE_MQTT_LISTEN);
        }
        break;
      }
      case NETWORK_STATE_QUARANTINE:
      {
        bg95_turn_off_and_on(g_bg95.powered_down_mark, false, false);
        g_bg95.powered_down_mark =
          min(g_bg95.powered_down_mark + 5000, POWER_DOWN_MARK_END);
        bg95_reset(&g_bg95, NETWORK_STATE_MODEM_RESTART);
        g_bg95.activated_restart = false;
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void bg95_initialize(void* incoming_queue, void* outgoing_queue, char* uid)
{
  uint32_t uid_len = strlen(uid);
  uint32_t iterations = min(uid_len, 16);
  for (uint32_t i = 0; i < iterations; ++i)
  {
    g_bg95.extra += ch_int(uid[i], i, 0xFD274901);
  }

  setting_string2(&g_dl, g_bg95.extra, 6, o_c('/', 0, g_bg95.extra),
                  o_c('d', 1, g_bg95.extra), o_c('o', 2, g_bg95.extra),
                  o_c('w', 3, g_bg95.extra), o_c('n', 4, g_bg95.extra),
                  o_c('/', 5, g_bg95.extra), o_c('l', 6, g_bg95.extra),
                  o_c('u', 8, g_bg95.extra), o_c('x', 9, g_bg95.extra));

  g_bg95.incoming_queue = (QueueHandle_t)incoming_queue;
  g_bg95.outgoing_queue = (QueueHandle_t)outgoing_queue;
  string32(&g_bg95.uid, uid);

  setting_string2(&g_treu, g_bg95.extra, 2, o_c('+', 0, g_bg95.extra),
                  o_c('Q', 1, g_bg95.extra), o_c('M', 2, g_bg95.extra),
                  o_c('T', 4, g_bg95.extra), o_c('C', 5, g_bg95.extra),
                  o_c('O', 6, g_bg95.extra), o_c('N', 7, g_bg95.extra),
                  o_c('N', 8, g_bg95.extra), o_c(':', 9, g_bg95.extra));

  xTaskCreateStatic(bg95_task, "BG95 Task", BG95_STACK_SIZE, NULL, 1, g_b_s, &g_b_t);
}

bool bg95_mqtt_is_connected(void)
{
  return g_bg95.mqtt_connected;
}

bool bg95_gnss_found(void)
{
  return g_bg95.gnss_found;
}
