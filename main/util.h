#ifndef LSX_UTIL_H
#define LSX_UTIL_H
#include <stdint.h>
#ifndef LSX_RELEASE
#include <stdio.h>
#endif
#include <time.h>

#include "platform.h"
#include "json_util.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NO_LUX_VALUE (-1.0f)

#define ASTROUR_NOT_APPLIED 0
#define ASTROUR_NO_SHIFT    1
#define ASTROUR_HALF_SHIFT  2
#define ASTROUR_FULL_SHIFT  3

#define LED_ON_TIME 240000

#define IGNORE_VALUE 255

#ifndef LSX_RELEASE
#define lsx_log(...) printf(__VA_ARGS__)
#else
#define lsx_log(...)
#endif

#define QUEUE_SIZE2     10
#define MESSAGE_LENGTH2 256

#define MESSAGE_LENGTH 64
#define QUEUE_SIZE     20
#define NO_OF_RULES    64
#define MAX_REP_TIME   1800
// #define MAX_REP_TIME     60
#define TIME_IN_THE_PAST 1758781424

#define MESSAGE_TYPE_NULL                       0x0000
#define MESSAGE_TYPE_STATUS_REPORT              0x0001
#define MESSAGE_TYPE_RULE_REPORT                0x0002
#define MESSAGE_TYPE_CCID_REPORT                0x0003
#define MESSAGE_TYPE_IP_REPORT                  0x0004
#define MESSAGE_TYPE_OPERATOR_REPORT            0x0005
#define MESSAGE_TYPE_GPS_REPORT                 0x0006
#define MESSAGE_TYPE_SEARCH_GPS_AGAIN           0x0007
#define MESSAGE_TYPE_INSERT_POS                 0x0008
#define MESSAGE_TYPE_RSSI_REPORT                0x0009
#define MESSAGE_TYPE_INSERT_RULE                0x000a
#define MESSAGE_TYPE_REMOVE_RULE                0x000b
#define MESSAGE_TYPE_REMOVE_ALL_RULE            0x000c
#define MESSAGE_TYPE_SWITCH                     0x000d
#define MESSAGE_TYPE_BURNTIME_REPORT            0x000e
#define MESSAGE_TYPE_TIMEZONE_REPORT            0x000f
#define MESSAGE_TYPE_INSERT_TIMEZONE            0x0010
#define MESSAGE_TYPE_LUX_REPORT                 0x0011
#define MESSAGE_TYPE_INSERT_LUX                 0x0012
#define MESSAGE_TYPE_SET_MASTER_SLAVE           0x0013
#define MESSAGE_TYPE_REQUEST_MASTER_SLAVE       0x0015
#define MESSAGE_TYPE_RESTART_ESP                0x0016
#define MESSAGE_TYPE_CLEAR_AND_RESTART_ESP      0x0017
#define MESSAGE_TYPE_UPDATE_FIRMWARE            0x0018
#define MESSAGE_TYPE_SET_TIME                   0x0019
#define MESSAGE_TYPE_SYNC_WITH_NTP_SERVER       0x001a
#define MESSAGE_TYPE_NTP_SERVER_REPORT          0x001b
#define MESSAGE_TYPE_SET_NTP_SERVER             0x001c
#define MESSAGE_TYPE_RUN_RULES                  0x001d
#define MESSAGE_TYPE_OPERATORS_REPORT           0x001e
#define MESSAGE_TYPE_SEARCH_AND_SELECT_OPERATOR 0x001f
#define MESSAGE_TYPE_SELECT_NEW_OPERATOR        0x0020
#define MESSAGE_TYPE_SEND_AT_COMMAND            0x0021
#define MESSAGE_TYPE_RESTART_BG95               0x0022
#define MESSAGE_TYPE_VERSION_REPORT             0x0023
#define MESSAGE_TYPE_LIGHT_SET_CONFIG           0x0024
#define MESSAGE_TYPE_LIGHT_SEND_QUERY_COMMAND   0x0025
#define MESSAGE_TYPE_LIGHT_CONFIG_REPORT        0x0026
#define MESSAGE_TYPE_LIGHT_STATUS_REPORT        0x0027
#define MESSAGE_TYPE_LIGHT_FULL_DIAGNOSTIC      0x0028
#define MESSAGE_TYPE_DYNAMIC_LUX_ACTIVATION     0x0029
#define MESSAGE_TYPE_SET_AUTO_NTP_SERVER_UPDATE 0x002a
#define MESSAGE_TYPE_SET_KEEPALIVE              0x002b
#define MESSAGE_TYPE_SET_ASTROUR_SHIFT          0x002c
#define MESSAGE_TYPE_CONFIG_ALTITUDES           0x002d
#define MESSAGE_TYPE_LUX_CONFIG                 0x002e
#define MESSAGE_TYPE_MASTER_SEND_BOUNDS         0x002f
#define MESSAGE_TYPE_STORE_BURN_TIMES           0x0030
#define MESSAGE_TYPE_ADD_MULTI_GROUP            0x0031
#define MESSAGE_TYPE_REMOVE_MULTI_GROUP         0x0032
#define MESSAGE_TYPE_REPORT_MULTI_GROUP         0x0033
#define MESSAGE_TYPE_HAN_REPORT                 0x0034
#define MESSAGE_TYPE_SET_LIGHT_SENSOR           0x0035
#define MESSAGE_TYPE_SET_TIMEOUT_LUX_VALUE      0x0036
#define MESSAGE_TYPE_CLEAR_ESP                  0x0037
#define MESSAGE_TYPE_SET_LUX_STRAT              0x0038
#define MESSAGE_TYPE_UNKNOWN                    0x0039

#define RULE_TYPE_NULL           0
#define RULE_TYPE_ALTITUDE_OVER  1
#define RULE_TYPE_ALTITUDE_UNDER 2
#define RULE_TYPE_AZIMUTH_OVER   3
#define RULE_TYPE_AZIMUTH_UNDER  4
#define RULE_TYPE_LUX_OVER       5
#define RULE_TYPE_LUX_UNDER      6
#define RULE_TYPE_TIME           7
#define RULE_TYPE_SCHEDULE       8
#define RULE_TYPE_UNKNOWN        9

#define RULE_IS_FLOAT_TYPE(value)                                                      \
  (((value) == RULE_TYPE_ALTITUDE_OVER) || ((value) == RULE_TYPE_ALTITUDE_UNDER) ||    \
   ((value) == RULE_TYPE_AZIMUTH_OVER) || ((value) == RULE_TYPE_AZIMUTH_UNDER) ||      \
   ((value) == RULE_TYPE_LUX_OVER) || ((value) == RULE_TYPE_LUX_UNDER))

#define in_between(start, value, end)      ((start) <= (value) && (value) <= (end))
#define in_between_open(start, value, end) ((start) <= (value) && (value) < (end))
#define array_size(array)                  (sizeof(array) / sizeof(array[0]))

#define BIT_64 0x8000000000000000
#define BIT_63 0x4000000000000000
#define BIT_62 0x2000000000000000
#define BIT_61 0x1000000000000000
#define BIT_60 0x800000000000000
#define BIT_59 0x400000000000000
#define BIT_58 0x200000000000000
#define BIT_57 0x100000000000000
#define BIT_56 0x80000000000000
#define BIT_55 0x40000000000000
#define BIT_54 0x20000000000000
#define BIT_53 0x10000000000000
#define BIT_52 0x8000000000000
#define BIT_51 0x4000000000000
#define BIT_50 0x2000000000000
#define BIT_49 0x1000000000000
#define BIT_48 0x800000000000
#define BIT_47 0x400000000000
#define BIT_46 0x200000000000
#define BIT_45 0x100000000000
#define BIT_44 0x80000000000
#define BIT_43 0x40000000000
#define BIT_42 0x20000000000
#define BIT_41 0x10000000000
#define BIT_40 0x8000000000
#define BIT_39 0x4000000000
#define BIT_38 0x2000000000
#define BIT_37 0x1000000000
#define BIT_36 0x800000000
#define BIT_35 0x400000000
#define BIT_34 0x200000000
#define BIT_33 0x100000000
#define BIT_32 0x80000000
#define BIT_31 0x40000000
#define BIT_30 0x20000000
#define BIT_29 0x10000000
#define BIT_28 0x8000000
#define BIT_27 0x4000000
#define BIT_26 0x2000000
#define BIT_25 0x1000000
#define BIT_24 0x800000
#define BIT_23 0x400000
#define BIT_22 0x200000
#define BIT_21 0x100000
#define BIT_20 0x80000
#define BIT_19 0x40000
#define BIT_18 0x20000
#define BIT_17 0x10000
#define BIT_16 0x8000
#define BIT_15 0x4000
#define BIT_14 0x2000
#define BIT_13 0x1000
#define BIT_12 0x800
#define BIT_11 0x400
#define BIT_10 0x200
#define BIT_9  0x100
#define BIT_8  0x80
#define BIT_7  0x40
#define BIT_6  0x20
#define BIT_5  0x10
#define BIT_4  0x8
#define BIT_3  0x4
#define BIT_2  0x2
#define BIT_1  0x1

#define XOR_KEY 0x25A34F00

#define FFINT_FOR 0x1037DF22

#define o_c(c, i, u) ((c) ^ (XOR_KEY + ((i + u) * 11)))

#define setting_string2(string, u, e, ...)                                             \
  do                                                                                   \
  {                                                                                    \
    uint32_t VVVDITHEHRHDJKL2447837[] = { __VA_ARGS__ };                               \
    for (uint32_t VHDKHFJKHKL211114 = 0;                                               \
         VHDKHFJKHKL211114 < array_size(VVVDITHEHRHDJKL2447837); ++VHDKHFJKHKL211114)  \
    {                                                                                  \
      uint32_t ERUTJD32887 = VHDKHFJKHKL211114 + (VHDKHFJKHKL211114 > (e));            \
      VVVDITHEHRHDJKL2447837[VHDKHFJKHKL211114] =                                      \
        ch_int(o_c(VVVDITHEHRHDJKL2447837[VHDKHFJKHKL211114], ERUTJD32887, u),         \
               VHDKHFJKHKL211114, u);                                                  \
    }                                                                                  \
    uint32_t UDJDJU37329JJFDHSU = u;                                                   \
    (string)->length = transform_buffer_to_string(                                     \
      VVVDITHEHRHDJKL2447837, array_size(VVVDITHEHRHDJKL2447837), &UDJDJU37329JJFDHSU, \
      (string)->data, sizeof((string)->data));                                         \
                                                                                       \
  } while (0)

  typedef struct timer_ms_t
  {
    uint32_t time;
    uint32_t duration;
  } timer_ms_t;

  typedef struct timer_us_t
  {
    uint32_t time;
    uint32_t duration;
  } timer_us_t;

  typedef struct message_buffer_t
  {
    uint32_t write_pointer;
    uint32_t read_pointer;
    uint8_t data[MESSAGE_LENGTH];
  } message_buffer_t;

  typedef struct time_buffer_t
  {
    uint32_t count;
    uint32_t capacity;
    time_t mark;
    time_t data[32];
  } time_buffer_t;

  typedef struct time_buffer_2_t
  {
    uint32_t head;
    uint32_t tail;

    uint32_t count;
    uint32_t capacity;

    time_t mark;
    time_t data[32];
  } time_buffer_2_t;

  typedef enum tokeniser_action_t
  {
    TOKENISER_ACTION_NO_ACTION,
    TOKENISER_ACTION_NO_ORDER,
    TOKENISER_ACTION_IN_ORDER
  } tokeniser_action_t;

  typedef struct tokeniser_t
  {
    char delim_order[32];
    uint32_t delim_count;

    union
    {
      uint32_t positions_count;
      uint32_t token_count;
    };
    uint32_t positions[64];
    char characters[64];

    uint32_t buffer_length;
    char* buffer;
  } tokeniser_t;

  typedef struct big_endian_t
  {
    union
    {
      uint32_t u;
      float f;
    };
  } big_endian_t;

  typedef enum result_code_t
  {
    RESULT_SUCCESS = 0,
    RESULT_ERROR_NO_TYPE,
    RESULT_ERROR_NULL_ARGUMENT,
    RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS,
    RESULT_ERROR_BUFFER_READ_OUT_OF_BOUNDS,
    RESULT_ERROR_RULE_PRIORITY_TOO_HIGH,
    RESULT_ERROR_RULE_SIZE_WRONG,
  } result_code_t;

#define STRING_TYPE_DEFINITIONS(size)                                                  \
  typedef struct string##size##_t                                                      \
  {                                                                                    \
    uint32_t length;                                                                   \
    char data[size];                                                                   \
  } string##size##_t;                                                                  \
  enum                                                                                 \
  {                                                                                    \
    STRING##size##_BUFFER_SIZE = size                                                  \
  };                                                                                   \
  bool string##size(string##size##_t* result, const char* format, ...);                \
  void string##size##_copy(string##size##_t* result, const string##size##_t* string);  \
  bool string##size##_can_fit(const string##size##_t* string, uint32_t length);        \
  void string##size##_reset(string##size##_t* result);                                 \
  void string##size##_set_length(string##size##_t* result);                            \
  int32_t string##size##_find(const string##size##_t* string_to_scan,                  \
                              const char* string_to_scan_for, int32_t start_index,     \
                              int32_t length);                                         \
  int32_t string##size##_find_char(const string##size##_t* string_to_scan,             \
                                   char char_to_scan_for, int32_t start_index);        \
  bool string##size##_equal(const string##size##_t* first,                             \
                            const string##size##_t* second);                           \
  bool string##size##_equal_string(const string##size##_t* first, const char* second); \
  bool string##size##_begins_with(const string##size##_t* first, const char* begin);   \
  uint32_t string##size##_as_uint32(const string##size##_t* string,                    \
                                    int32_t start_index, int32_t end_index);           \
  bool string##size##_is_empty(const string##size##_t* string);                        \
  bool string##size##_is_null(const string##size##_t* string);                         \
  uint32_t string##size##_substring_shared(string##size##_t* shared_string,            \
                                           int32_t start_index, int32_t end_index,     \
                                           char** result)

#define STRING_FROM_STRING_DEFINITION(first_size, second_size)                         \
  bool string##first_size##_from_string##second_size(                                  \
    string##first_size##_t* destination, const string##second_size##_t* source,        \
    uint32_t length, uint32_t offset)

  STRING_TYPE_DEFINITIONS(32);
  STRING_TYPE_DEFINITIONS(64);
  STRING_TYPE_DEFINITIONS(128);
  STRING_TYPE_DEFINITIONS(256);
  STRING_TYPE_DEFINITIONS(1024);
  STRING_TYPE_DEFINITIONS(2048);

  STRING_FROM_STRING_DEFINITION(64, 2048);

  uint32_t length_of_constant_string(const char* constant_string, uint32_t cap_length);

  timer_ms_t timer_create_ms(uint32_t duration);
  bool timer_is_up_ms(timer_ms_t timer, uint32_t ms);
  bool timer_is_up_and_reset_ms(timer_ms_t* timer, uint32_t ms);

  void delay_no_reset(uint32_t milliseconds);
  void delay_task(uint32_t milliseconds);

  uint32_t min(uint32_t first, uint32_t second);
  uint32_t max(uint32_t first, uint32_t second);
  uint32_t plus_one_wrap(uint32_t value, uint32_t length);
  uint32_t minus_one_wrap(uint32_t value, uint32_t length);

  uint16_t uint16_from_byte_buffer_big_endian(const uint8_t* message_buffer,
                                              uint32_t position);
  uint32_t uint32_from_byte_buffer_big_endian(const uint8_t* message_buffer,
                                              uint32_t position);
  uint32_t uint64_from_byte_buffer_big_endian(const uint8_t* message_buffer,
                                              uint32_t position);

  void copy_uint16_to_byte_buffer_big_endian(uint16_t value, uint8_t* message_buffer,
                                             uint32_t position);
  void copy_uint32_to_byte_buffer_big_endian(uint32_t value, uint8_t* message_buffer,
                                             uint32_t position);

  uint32_t float_to_uint32_big_endian(float value);
  float float_from_uint32_big_endian(uint32_t value);

  char* first_token(char* string, const char* delims);
  char* next_token(const char* delims);

  uint16_t uint16_swap_bytes(uint16_t value);

  uint32_t byte_to_hex(uint8_t* message, uint32_t length, char* result, bool capital);

  float degrees_to_radians(float degrees);
  float haversine_distance_km(float lat_first, float lon_first, float lat_second,
                              float lon_second);

  void message_buffer_reset(message_buffer_t* buffer);
  result_code_t message_buffer_add_size(message_buffer_t* buffer);

  result_code_t message_buffer_read_uint8(message_buffer_t* buffer, uint8_t* result);
  result_code_t message_buffer_read_uint16(message_buffer_t* buffer, uint16_t* result);
  result_code_t message_buffer_read_uint32(message_buffer_t* buffer, uint32_t* result);
  result_code_t message_buffer_read_type(message_buffer_t* buffer, uint16_t* result);

  result_code_t message_buffer_read_float(message_buffer_t* buffer, float* result);

  result_code_t message_buffer_write_uint8(message_buffer_t* buffer, uint8_t value);
  result_code_t message_buffer_write_uint16(message_buffer_t* buffer, uint16_t value);
  result_code_t message_buffer_write_uint32(message_buffer_t* buffer, uint32_t value);
  result_code_t message_buffer_write_float(message_buffer_t* buffer, float value);

  void uint8_from_util_buffer(uint8_t* util, uint32_t util_size, uint8_t* uint,
                              string64_t* util_out, const uint8_t* th);

  uint32_t ch_int(uint32_t input, int index, uint32_t uint);
  uint32_t transform_buffer_to_string(uint32_t* util, uint32_t util_size,
                                      uint32_t* uint, char* out, uint32_t out_size);

#define time_buffer_create(buffer, capacity, mark)                                     \
  static_assert(array_size((buffer)->data) >= capacity);                               \
  *(buffer) = time_buffer_create_(capacity, mark)

  time_buffer_t time_buffer_create_(uint32_t capacity, time_t mark);
  bool time_buffer_add_time(time_buffer_t* buffer, time_t time);

  bool parse_ntp_response(string2048_t* response, time_info_t* time_info);

  void tokeniser_create(tokeniser_t* tokeniser, const char* delims,
                        uint32_t delim_count, char* buffer, uint32_t buffer_length,
                        tokeniser_action_t action);
  uint32_t tokeniser_tokenise(tokeniser_t* tokeniser, char** tokens,
                              uint32_t token_capacity);
  uint32_t tokeniser_tokenise_no_order(tokeniser_t* tokeniser, char** tokens,
                                       uint32_t token_capacity);
  void tokeniser_reset(tokeniser_t* tokeniser);
  char* tokeniser_get_token(tokeniser_t* tokeniser, uint32_t index);

  void uint16_to_hex(uint16_t value, char* hex);
  uint16_t uint16_from_hex_string(const char* hex);
  bool hex_to_bytes(const char* hex, uint32_t hex_length, uint8_t* bytes,
                    uint32_t bytes_max_size);

  float absolute_float(float value);
  time_t absolute_time_t(time_t value);

  double round_double_to_4_decimals(double value);
  double round_double_to_6_decimals(double value);

  void swap_endianess_uint64(uint8_t* buffer, uint32_t pos);
  void swap_endianess_uint32(uint8_t* buffer, uint32_t pos);
  void swap_endianess_uint16(uint8_t* buffer, uint32_t pos);

  int32_t char_in_string_internal(const char* string, uint32_t string_length,
                                  char char_to_scan_for, int32_t start_index);

  int32_t string_in_string_internal(const char* string_to_scan, uint32_t length_of_scan,
                                    const char* string_to_scan_for,
                                    uint32_t length_of_search_string,
                                    int32_t start_index);

  bool string_equal_internal(const char* first, uint32_t first_length,
                             const char* second, uint32_t second_length);

  bool astrour_apply_with_shift(bool overlapped_relay, uint32_t time,
                                float current_altitude, float angle,
                                float current_sun_altitude_width_shift,
                                uint8_t current_shift, uint8_t* astrour_applied_shift,
                                uint32_t* not_applied_shift_switch_timer,
                                uint32_t* off_on_full_shift_timer,
                                uint32_t* off_on_half_shift_timer,
                                uint32_t* on_on_half_shift_timer);

  void
  check_queue_for_dublicates(char temp_message_buffer[QUEUE_SIZE2][MESSAGE_LENGTH2],
                             uint16_t message_type, void* outgoing_queue_in,
                             void* json_object);

  void get_message_type_as_hex(char* message_type_as_hex, uint16_t message_type);

  void json_send_empty_to_queue(void* queue, uint16_t message_type,
                                uint32_t timeout_ms);
  void json_send_to_queue_and_delete(cJSON* json, void* queue, uint32_t timeout_ms);
  void json_send_to_queue(cJSON* json, void* queue, uint32_t timeout_ms);

  void selection_sort_float(float* array, uint32_t count);
  float median_float_buffer(float* buffer, uint32_t count);

  void selection_sort_double(double* array, uint32_t count);
  double median_double_buffer(double* buffer, uint32_t count, uint32_t avg_count);

#ifdef __cplusplus
}
#endif

#endif
