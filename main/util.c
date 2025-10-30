#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include <esp_task_wdt.h>

#include "util.h"
#include "platform.h"

#define EARTH_RADIUS_KM 6371
#define PI              3.1415926535897932384626433832795

#define AES_KEY "1234567890123456"
#define AES_IV  "1234567890123456"

#define STRING_TYPE_IMPLEMENTATIONS(size)                                              \
  bool string##size(string##size##_t* result, const char* format, ...)                 \
  {                                                                                    \
    va_list args;                                                                      \
    va_start(args, format);                                                            \
    bool success = string_format_internal(result->data, &result->length,               \
                                          sizeof(result->data), format, args);         \
    va_end(args);                                                                      \
    return success;                                                                    \
  }                                                                                    \
                                                                                       \
  void string##size##_copy(string##size##_t* result, const string##size##_t* string)   \
  {                                                                                    \
    memcpy(result->data, string->data, string->length);                                \
    result->length = string->length;                                                   \
    result->data[result->length] = '\0';                                               \
  }                                                                                    \
                                                                                       \
  bool string##size##_can_fit(const string##size##_t* string, uint32_t length)         \
  {                                                                                    \
    return (string->length + length) < (sizeof(string->data) - 1);                     \
  }                                                                                    \
                                                                                       \
  void string##size##_reset(string##size##_t* result)                                  \
  {                                                                                    \
    result->length = 0;                                                                \
    result->data[0] = '\0';                                                            \
  }                                                                                    \
                                                                                       \
  void string##size##_set_length(string##size##_t* result)                             \
  {                                                                                    \
    result->length =                                                                   \
      string_get_length_internal(result->data, sizeof(result->data) - 1);              \
  }                                                                                    \
                                                                                       \
  int32_t string##size##_find(const string##size##_t* string_to_scan,                  \
                              const char* string_to_scan_for, int32_t start_index,     \
                              int32_t length)                                          \
  {                                                                                    \
    uint32_t constant_length = 0;                                                      \
    if (length < 0)                                                                    \
    {                                                                                  \
      constant_length =                                                                \
        length_of_constant_string(string_to_scan_for, string_to_scan->length + 1);     \
    }                                                                                  \
    else                                                                               \
    {                                                                                  \
      constant_length = (uint32_t)length;                                              \
    }                                                                                  \
    return string_in_string_internal(string_to_scan->data, string_to_scan->length,     \
                                     string_to_scan_for, constant_length,              \
                                     start_index);                                     \
  }                                                                                    \
                                                                                       \
  int32_t string##size##_find_char(const string##size##_t* string_to_scan,             \
                                   char char_to_scan_for, int32_t start_index)         \
  {                                                                                    \
    return char_in_string_internal(string_to_scan->data, string_to_scan->length,       \
                                   char_to_scan_for, start_index);                     \
  }                                                                                    \
                                                                                       \
  bool string##size##_equal(const string##size##_t* first,                             \
                            const string##size##_t* second)                            \
  {                                                                                    \
    return string_equal_internal(first->data, first->length, second->data,             \
                                 second->length);                                      \
  }                                                                                    \
                                                                                       \
  bool string##size##_equal_string(const string##size##_t* first, const char* second)  \
  {                                                                                    \
    uint32_t second_length = length_of_constant_string(second, first->length + 1);     \
    return string_equal_internal(first->data, first->length, second, second_length);   \
  }                                                                                    \
                                                                                       \
  bool string##size##_begins_with(const string##size##_t* first, const char* begin)    \
  {                                                                                    \
    uint32_t begin_length = length_of_constant_string(begin, first->length + 1);       \
    return string_begin_with_internal(first->data, first->length, begin,               \
                                      begin_length);                                   \
  }                                                                                    \
                                                                                       \
  uint32_t string##size##_as_uint32(const string##size##_t* string,                    \
                                    int32_t start_index, int32_t end_index)            \
  {                                                                                    \
    return string_as_uint32_internal(string->data, string->length, start_index,        \
                                     end_index);                                       \
  }                                                                                    \
                                                                                       \
  bool string##size##_is_empty(const string##size##_t* string)                         \
  {                                                                                    \
    return string->length == 0;                                                        \
  }                                                                                    \
                                                                                       \
  bool string##size##_is_null(const string##size##_t* value)                           \
  {                                                                                    \
    return (value->length == 0) || string##size##_equal_string(value, "null");         \
  }                                                                                    \
                                                                                       \
  uint32_t string##size##_substring_shared(string##size##_t* shared_string,            \
                                           int32_t start_index, int32_t end_index,     \
                                           char** result)                              \
  {                                                                                    \
    uint32_t result_size = 0;                                                          \
    if (end_index > start_index)                                                       \
    {                                                                                  \
      result_size = end_index - start_index;                                           \
      (*result) = shared_string->data + start_index;                                   \
    }                                                                                  \
    return result_size;                                                                \
  }

#define STRING_FROM_STRING_IMPLEMENTATION(first_size, second_size)                     \
  bool string##first_size##_from_string##second_size(                                  \
    string##first_size##_t* destination, const string##second_size##_t* source,        \
    uint32_t length, uint32_t offset)                                                  \
  {                                                                                    \
    return string_from_string_internal(destination->data, &destination->length,        \
                                       sizeof(destination->data), source->data,        \
                                       source->length, length, offset);                \
  }

static bool string_from_string_internal(char* destination, uint32_t* destination_length,
                                        uint32_t destination_capacity,
                                        const char* source, uint32_t source_length,
                                        uint32_t length, uint32_t offset)
{
  if ((length < destination_capacity) && ((offset + length) <= source_length))
  {
    memcpy(destination, source + offset, length);
    *destination_length = length;
    destination[length] = '\0';
    return true;
  }
  destination[0] = '\0';
  *destination_length = 0;
  lsx_log("ERROR\n");
  return false;
}

static bool strings_are_valid(const char* first, uint32_t first_length,
                              const char* second, uint32_t second_length)
{
  return (first_length != 0) && (second_length != 0) && first && second;
}

static bool string_format_internal(char* result, uint32_t* result_length,
                                   uint32_t capacity, const char* format, va_list args)
{
  bool success = false;

  int32_t length = vsnprintf(result, capacity, format, args);

  uint32_t r_length = 0;
  if (length > capacity)
  {
    r_length = capacity - 1;
  }
  else if (length > 0)
  {
    r_length = length;
    success = true;
  }
  result[r_length] = '\0';
  *result_length = r_length;

  return success;
}

static uint32_t string_get_length_internal(char* string, uint32_t cap_length)
{
  for (uint32_t i = 0; i < cap_length; ++i)
  {
    if (string[i] == '\0')
    {
      return i;
    }
  }
  string[cap_length] = '\0';
  return cap_length;
}

int32_t string_in_string_internal(const char* string_to_scan, uint32_t length_of_scan,
                                  const char* string_to_scan_for,
                                  uint32_t length_of_search_string, int32_t start_index)
{
  if ((length_of_scan < length_of_search_string) || (start_index < 0) ||
      !strings_are_valid(string_to_scan, length_of_scan, string_to_scan_for,
                         length_of_search_string))
  {
    return -1;
  }

  int32_t diff = length_of_scan - length_of_search_string;
  for (int32_t i = start_index; i <= diff; ++i)
  {
    uint32_t j = 0;
    for (; (j < length_of_search_string) &&
           (string_to_scan[i + j] == string_to_scan_for[j]);
         ++j)
      ;

    if (j == length_of_search_string)
    {
      return i;
    }
  }
  return -1;
}

int32_t char_in_string_internal(const char* string, uint32_t string_length,
                                char char_to_scan_for, int32_t start_index)
{
  if (start_index != -1)
  {
    for (int32_t i = start_index; i < string_length; ++i)
    {
      if (string[i] == char_to_scan_for)
      {
        return i;
      }
    }
  }
  return -1;
}

static bool compare_string(const char* first, const char* second, uint32_t length)
{
  for (uint32_t i = 0; i < length; ++i, ++first, ++second)
  {
    if ((*first) != (*second))
    {
      return false;
    }
  }
  return true;
}

bool string_equal_internal(const char* first, uint32_t first_length, const char* second,
                           uint32_t second_length)
{
  if ((first_length != second_length) ||
      !strings_are_valid(first, first_length, second, second_length))
  {
    return false;
  }
  return compare_string(first, second, first_length);
}

static bool string_begin_with_internal(const char* first, uint32_t first_length,
                                       const char* second, uint32_t second_length)
{
  if ((first_length < second_length) ||
      !strings_are_valid(first, first_length, second, second_length))
  {
    return false;
  }
  return compare_string(first, second, second_length);
}

static uint32_t string_as_uint32_internal(const char* string, uint32_t length,
                                          int32_t start_index, int32_t end_index)
{
  start_index += 1 * (start_index == -1);
  const char* start = string + start_index;

  end_index = (end_index == -1) ? length : end_index;

  if ((end_index < length) && (start_index < end_index))
  {
    char* temp_ptr = (char*)string;
    char temp = temp_ptr[end_index];
    temp_ptr[end_index] = '\0';

    uint32_t result = (uint32_t)strtoul(start, NULL, 0);

    temp_ptr[end_index] = temp;

    return result;
  }
  return 0;
}

STRING_TYPE_IMPLEMENTATIONS(32);
STRING_TYPE_IMPLEMENTATIONS(64);
STRING_TYPE_IMPLEMENTATIONS(128);
STRING_TYPE_IMPLEMENTATIONS(256);
STRING_TYPE_IMPLEMENTATIONS(512);
STRING_TYPE_IMPLEMENTATIONS(1024);
STRING_TYPE_IMPLEMENTATIONS(2048);

STRING_FROM_STRING_IMPLEMENTATION(64, 2048);

uint32_t length_of_constant_string(const char* constant_string, uint32_t cap_length)
{
  if (!constant_string)
  {
    return 0;
  }
  for (int32_t i = 0; i < cap_length; ++i, ++constant_string)
  {
    if ((*constant_string) == '\0')
    {
      return i;
    }
  }
  return 0;
}

timer_ms_t timer_create_ms(uint32_t duration)
{
  timer_ms_t timer = {};
  timer.time = lsx_get_millis();
  timer.duration = duration;
  return timer;
}

bool timer_is_up_ms(timer_ms_t timer, uint32_t ms)
{
  return (ms - timer.time) >= timer.duration;
}

bool timer_is_up_and_reset_ms(timer_ms_t* timer, uint32_t ms)
{
  bool result = timer_is_up_ms(*timer, ms);
  if (result)
  {
    timer->time = ms;
  }
  return result;
}

timer_us_t timer_create_us(uint32_t duration)
{
  timer_us_t timer = {};
  timer.time = lsx_get_micro();
  timer.duration = duration;
  return timer;
}

bool timer_is_up_us(timer_us_t timer, uint32_t us)
{
  return (us - timer.time) >= timer.duration;
}

bool timer_is_up_and_reset_us(timer_us_t* timer, uint32_t us)
{
  bool result = timer_is_up_us(*timer, us);
  if (result)
  {
    *timer = timer_create_us(timer->duration);
  }
  return result;
}

void delay_no_reset(uint32_t milliseconds)
{
  vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

void delay_task(uint32_t milliseconds)
{
  timer_ms_t noServiceTimer = timer_create_ms(milliseconds);
  while (!timer_is_up_ms(noServiceTimer, lsx_get_millis()))
  {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

uint32_t min(uint32_t first, uint32_t second)
{
  return first < second ? first : second;
}

uint32_t max(uint32_t first, uint32_t second)
{
  return first < second ? first : second;
}

uint32_t plus_one_wrap(uint32_t value, uint32_t length)
{
  return (value + 1) % length;
}

uint32_t minus_one_wrap(uint32_t value, uint32_t length)
{
  return (value + (length - 1)) % length;
}

uint16_t uint16_from_byte_buffer_big_endian(const uint8_t* message_buffer,
                                            uint32_t position)
{
  uint16_t result = 0;
  result = message_buffer[position];
  result = result << 8;
  result = result + message_buffer[position + 1];
  return result;
}

uint32_t uint32_from_byte_buffer_big_endian(const uint8_t* message_buffer,
                                            uint32_t position)
{
  uint32_t result = 0;
  result = message_buffer[position];
  result = result << 8;
  result = result + message_buffer[position + 1];
  result = result << 8;
  result = result + message_buffer[position + 2];
  result = result << 8;
  result = result + message_buffer[position + 3];
  return result;
}

void copy_uint16_to_byte_buffer_big_endian(uint16_t value, uint8_t* message_buffer,
                                           uint32_t position)
{
  message_buffer[position] = ((value >> 8) & 0xFF);
  message_buffer[position + 1] = (value & 0xFF);
}

void copy_uint32_to_byte_buffer_big_endian(uint32_t value, uint8_t* message_buffer,
                                           uint32_t position)
{
  message_buffer[position] = (value >> 24);
  message_buffer[position + 1] = ((value >> 16) & 0xFF);
  message_buffer[position + 2] = ((value >> 8) & 0xFF);
  message_buffer[position + 3] = (value & 0xFF);
}

uint32_t float_to_uint32_big_endian(float value)
{
  uint32_t result = 0;
  memcpy(&result, &value, sizeof(value));
  return result;
}

float float_from_uint32_big_endian(uint32_t value)
{
  float result = 0;
  memcpy(&result, &value, sizeof(float));
  return result;
}

char* first_token(char* string, const char* delims)
{
  return strtok(string, delims);
}

char* next_token(const char* delims)
{
  return strtok(NULL, delims);
}

uint16_t uint16_swap_bytes(uint16_t value)
{
  uint16_t result = value & 0xFF;
  result <<= 8;
  result |= (value >> 8) & 0xFF;
  return result;
}

uint32_t byte_to_hex(uint8_t* message, uint32_t length, char* result, bool capital)
{
  const char* hex_mapping = "0123456789ABCDEF";
  if (!capital)
  {
    hex_mapping = "0123456789abcdef";
  }
  uint32_t h = 0;
  for (uint32_t i = 0; i < length; ++i)
  {
    result[h++] = (hex_mapping[message[i] >> 4]);
    result[h++] = (hex_mapping[message[i] & 0x0F]);
  }
  result[h] = '\0';
  return h;
}

float degrees_to_radians(float degrees)
{
  return degrees * PI / 180.0f;
}

float haversine_distance_km(float lat_first, float lon_first, float lat_second,
                            float lon_second)
{
  float dLat = degrees_to_radians(lat_second - lat_first);
  float dLon = degrees_to_radians(lon_second - lon_first);

  float lat1 = degrees_to_radians(lat_first);
  float lat2 = degrees_to_radians(lat_second);

  float a = (sinf(dLat / 2) * sinf(dLat / 2)) +
            (sinf(dLon / 2) * sinf(dLon / 2) * cosf(lat1) * cosf(lat2));
  float c = 2 * atan2f(sqrtf(a), sqrtf(1 - a));
  return EARTH_RADIUS_KM * c;
}

void message_buffer_reset(message_buffer_t* buffer)
{
  if (buffer != NULL)
  {
    buffer->read_pointer = 0;
    buffer->write_pointer = 0;
    memset(buffer->data, 0, sizeof(buffer->data));
  }
}

result_code_t message_buffer_add_size(message_buffer_t* buffer)
{
  if (buffer == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  if (buffer->write_pointer <= (MESSAGE_LENGTH - 2))
  {
    for (int32_t i = buffer->write_pointer + 1; i >= 2; --i)
    {
      buffer->data[i] = buffer->data[i - 2];
    }
    copy_uint16_to_byte_buffer_big_endian(buffer->write_pointer, buffer->data, 0);
    buffer->write_pointer += 2;

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS;
}

result_code_t message_buffer_read_uint8(message_buffer_t* buffer, uint8_t* result)
{
  if (buffer == NULL || result == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  *result = 0;
  if (buffer->read_pointer < MESSAGE_LENGTH)
  {
    *result = buffer->data[buffer->read_pointer++];

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_READ_OUT_OF_BOUNDS;
}

result_code_t message_buffer_read_uint16(message_buffer_t* buffer, uint16_t* result)
{
  if (buffer == NULL || result == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  *result = 0;
  if ((buffer->read_pointer + sizeof(uint16_t)) <= MESSAGE_LENGTH)
  {
    *result = uint16_from_byte_buffer_big_endian(buffer->data, buffer->read_pointer);
    buffer->read_pointer += sizeof(uint16_t);

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_READ_OUT_OF_BOUNDS;
}

result_code_t message_buffer_read_uint32(message_buffer_t* buffer, uint32_t* result)
{
  if (buffer == NULL || result == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  *result = 0;
  if ((buffer->read_pointer + sizeof(uint32_t)) <= MESSAGE_LENGTH)
  {
    *result = uint32_from_byte_buffer_big_endian(buffer->data, buffer->read_pointer);
    buffer->read_pointer += sizeof(uint32_t);

    return RESULT_SUCCESS;
  }

  return RESULT_ERROR_BUFFER_READ_OUT_OF_BOUNDS;
}

result_code_t message_buffer_read_type(message_buffer_t* buffer, uint16_t* result)
{
  result_code_t error = message_buffer_read_uint16(buffer, result);
  if ((error == RESULT_SUCCESS) && (*result >= MESSAGE_TYPE_UNKNOWN))

  {
    error = RESULT_ERROR_NO_TYPE;
  }
  return error;
}

result_code_t message_buffer_read_float(message_buffer_t* buffer, float* result)
{
  if (buffer == NULL || result == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  *result = 0;
  if ((buffer->read_pointer + sizeof(float)) <= MESSAGE_LENGTH)
  {
    memcpy(result, buffer->data + buffer->read_pointer, sizeof(float));
    buffer->read_pointer += sizeof(float);
    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_READ_OUT_OF_BOUNDS;
}

result_code_t message_buffer_write_uint8(message_buffer_t* buffer, uint8_t value)
{
  if (buffer == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  if (buffer->write_pointer < MESSAGE_LENGTH)
  {
    buffer->data[buffer->write_pointer++] = value;

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS;
}

result_code_t message_buffer_write_uint16(message_buffer_t* buffer, uint16_t value)
{
  if (buffer == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  if ((buffer->write_pointer + sizeof(uint16_t)) <= MESSAGE_LENGTH)
  {
    copy_uint16_to_byte_buffer_big_endian(value, buffer->data, buffer->write_pointer);
    buffer->write_pointer += sizeof(uint16_t);

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS;
}

result_code_t message_buffer_write_uint32(message_buffer_t* buffer, uint32_t value)
{
  if (buffer == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  if ((buffer->write_pointer + sizeof(uint32_t)) <= MESSAGE_LENGTH)
  {
    copy_uint32_to_byte_buffer_big_endian(value, buffer->data, buffer->write_pointer);
    buffer->write_pointer += sizeof(uint32_t);

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS;
}

void swap_endianess_uint64(uint8_t* buffer, uint32_t pos)
{
  uint8_t temp = buffer[pos];
  buffer[pos] = buffer[pos + 7];
  buffer[pos + 7] = temp;

  temp = buffer[pos + 1];
  buffer[pos + 1] = buffer[pos + 6];
  buffer[pos + 6] = temp;

  temp = buffer[pos + 2];
  buffer[pos + 2] = buffer[pos + 5];
  buffer[pos + 5] = temp;

  temp = buffer[pos + 3];
  buffer[pos + 3] = buffer[pos + 4];
  buffer[pos + 4] = temp;
}

void swap_endianess_uint32(uint8_t* buffer, uint32_t pos)
{
  uint8_t temp = buffer[pos];
  buffer[pos] = buffer[pos + 3];
  buffer[pos + 3] = temp;

  temp = buffer[pos + 1];
  buffer[pos + 1] = buffer[pos + 2];
  buffer[pos + 2] = temp;
}

void swap_endianess_uint16(uint8_t* buffer, uint32_t pos)
{
  uint8_t temp = buffer[pos];
  buffer[pos] = buffer[pos + 1];
  buffer[pos + 1] = temp;
}

result_code_t message_buffer_write_float(message_buffer_t* buffer, float value)
{
  if (buffer == NULL)
  {
    return RESULT_ERROR_NULL_ARGUMENT;
  }
  if ((buffer->write_pointer + sizeof(float)) <= MESSAGE_LENGTH)
  {
    memcpy(buffer->data + buffer->write_pointer, &value, sizeof(float));
    buffer->write_pointer += sizeof(float);

    return RESULT_SUCCESS;
  }
  return RESULT_ERROR_BUFFER_WRITE_OUT_OF_BOUNDS;
}

uint8_t get_internal_util(uint8_t input, int index, uint8_t uint)
{
  uint8_t util = (uint * (index + 5) + 71) ^ (index * 83);
  return (input - (index % 97)) ^ util;
}

void uint8_from_util_buffer(uint8_t* util, uint32_t util_size, uint8_t* uint,
                            string64_t* util_out, const uint8_t* th)
{
  uint8_t th_in[32] = { 2, 1, 3, 4, 1 };
  for (int32_t i = 5; i >= 3; --i)
  {
    th_in[i] = th_in[i - 1];
  }
  th_in[2] = th[1];
  th_in[6] = th_in[5];
  th_in[5] = th[0];
  th_in[7] = th[2];
  th_in[3] = th_in[7];

  bool have_more_space = true;
  uint32_t index = 0;
  for (uint32_t i = 1, d = 0; (i < util_size) && have_more_space && (d < 8); ++i, ++d)
  {
    for (uint32_t j = 0; (j < th_in[d]) && have_more_space; ++j, ++i)
    {
      util_out->data[index] = (char)get_internal_util(util[i], index, *uint);
      have_more_space = ((++index) < (sizeof(util_out->data) - 1));
    }
  }
  util_out->data[index] = '\0';
  util_out->length = index;
}

uint32_t ch_int(uint32_t input, int index, uint32_t uint)
{
  uint32_t util = (uint * (index + 3) + 31) ^ (index * 5);
  return (input ^ util) + (index % 7);
}

uint32_t cc_int(uint32_t input, int index, uint32_t uint)
{
  uint32_t util = (uint * (index + 3) + 31) ^ (index * 5);
  return (input - (index % 7)) ^ util;
}

uint32_t transform_buffer_to_string(uint32_t* util, uint32_t util_size, uint32_t* uint,
                                    char* out, uint32_t out_size)
{
  uint32_t i = 0;
  for (; i < util_size; ++i)
  {
    if (i >= (out_size - 1))
    {
      break;
    }
    out[i] = (char)(cc_int(util[i], i, *uint) & 0xFF);
  }
  out[i] = '\0';
  return i;
}

time_buffer_t time_buffer_create_(uint32_t capacity, time_t mark)
{
  time_buffer_t result = {};
  result.count = 0;
  result.capacity = min(array_size(result.data), capacity);
  result.mark = mark;
  return result;
}

bool time_buffer_add_time(time_buffer_t* buffer, time_t time)
{
  buffer->data[buffer->count++] = time;
  if (buffer->count >= buffer->capacity)
  {
    time_t first_time = buffer->data[0];
    if ((time - first_time) <= buffer->mark)
    {
      buffer->count = 0;
      return true;
    }
    else
    {
      uint32_t expired_count = 1;
      for (; expired_count < buffer->count; ++expired_count)
      {
        time_t current_time = buffer->data[expired_count];
        if ((time - current_time) <= buffer->mark)
        {
          break;
        }
      }
      for (uint32_t i = expired_count, j = 0; i < buffer->count; ++i, ++j)
      {
        buffer->data[j] = buffer->data[i];
      }
      buffer->count -= expired_count;
    }
  }
  return false;
}

bool parse_ntp_response(string2048_t* response, time_info_t* time_info)
{

  const char* delims = "\"//,::+";

  tokeniser_t tokeniser = {};
  tokeniser_create(&tokeniser, delims, strlen(delims), response->data, response->length,
                   TOKENISER_ACTION_IN_ORDER);

  bool result = false;
  if (tokeniser.token_count >= 7)
  {
    time_info_t ti = {};
    ti.year = atoi(tokeniser_get_token(&tokeniser, 1)) - 1900;
    ti.month = atoi(tokeniser_get_token(&tokeniser, 2)) - 1;
    ti.month_day = atoi(tokeniser_get_token(&tokeniser, 3));
    ti.hour = atoi(tokeniser_get_token(&tokeniser, 4));
    ti.minute = atoi(tokeniser_get_token(&tokeniser, 5));
    ti.second = atoi(tokeniser_get_token(&tokeniser, 6));
    if (time_info)
    {
      *time_info = ti;
    }
    result = true;
  }
  tokeniser_reset(&tokeniser);
  return result;
}

void tokeniser_create(tokeniser_t* tokeniser, const char* delims, uint32_t delim_count,
                      char* buffer, uint32_t buffer_length, tokeniser_action_t action)
{
  memset(tokeniser, 0, sizeof(tokeniser_t));

  tokeniser->delim_count = delim_count;
  memcpy(tokeniser->delim_order, delims, delim_count);

  tokeniser->buffer = buffer;
  tokeniser->buffer_length = buffer_length;

  switch (action)
  {
    case TOKENISER_ACTION_NO_ORDER:
    {
      tokeniser_tokenise_no_order(tokeniser, NULL, 0);
      break;
    }
    case TOKENISER_ACTION_IN_ORDER:
    {
      tokeniser_tokenise(tokeniser, NULL, 0);
      break;
    }
    default: break;
  }
}

bool tokeniser_procceed_token(tokeniser_t* tokeniser, uint32_t i, char** tokens,
                              uint32_t token_capacity, uint32_t* token_count)
{
  if (tokeniser->positions_count < array_size(tokeniser->positions))
  {
    tokeniser->characters[tokeniser->positions_count] = tokeniser->buffer[i];
    tokeniser->positions[tokeniser->positions_count++] = i;
  }
  else
  {
    return true;
  }
  tokeniser->buffer[i] = '\0';

  if (tokens)
  {
    if ((*token_count) >= token_capacity)
    {
      return true;
    }
    tokens[*token_count] = tokeniser->buffer + i + 1;
    (*token_count)++;
  }
  if (i >= (tokeniser->buffer_length - 1))
  {
    return true;
  }
  return false;
}

static uint32_t tokeniser_finalize(tokeniser_t* tokeniser, char** tokens,
                                   uint32_t token_count)
{
  if ((tokeniser->buffer_length > 0) &&
      (tokeniser->positions_count < array_size(tokeniser->positions)))
  {
    int32_t i = tokeniser->buffer_length - 1;
    tokeniser->characters[tokeniser->positions_count] = tokeniser->buffer[i];
    tokeniser->positions[tokeniser->positions_count++] = i;
  }
  if (tokens)
  {
    return token_count;
  }
  return tokeniser->token_count;
}

uint32_t tokeniser_tokenise(tokeniser_t* tokeniser, char** tokens,
                            uint32_t token_capacity)
{
  uint32_t token_count = 0;
  if (tokens)
  {
    tokens[token_count++] = tokeniser->buffer;
  }
  for (uint32_t i = 0, delim_index = 0; i < tokeniser->buffer_length; ++i)
  {
    if (tokeniser->buffer[i] == tokeniser->delim_order[delim_index])
    {
      delim_index = plus_one_wrap(delim_index, tokeniser->delim_count);
      if (tokeniser_procceed_token(tokeniser, i, tokens, token_capacity, &token_count))
      {
        break;
      }
    }
  }
  return tokeniser_finalize(tokeniser, tokens, token_count);
}

uint32_t tokeniser_tokenise_no_order(tokeniser_t* tokeniser, char** tokens,
                                     uint32_t token_capacity)
{
  uint32_t token_count = 0;
  if (tokens)
  {
    tokens[token_count++] = tokeniser->buffer;
  }
  for (uint32_t i = 0; i < tokeniser->buffer_length; ++i)
  {
    for (uint32_t j = 0; j < tokeniser->delim_count; ++j)
    {
      if (tokeniser->buffer[i] == tokeniser->delim_order[j])
      {
        if (tokeniser_procceed_token(tokeniser, i, tokens, token_capacity,
                                     &token_count))
        {
          break;
        }
      }
    }
  }
  return tokeniser_finalize(tokeniser, tokens, token_count);
}

void tokeniser_reset(tokeniser_t* tokeniser)
{
  for (uint32_t i = 0; i < tokeniser->positions_count; ++i)
  {
    tokeniser->buffer[tokeniser->positions[i]] = tokeniser->characters[i];
  }
}

char* tokeniser_get_token(tokeniser_t* tokeniser, uint32_t index)
{
  if (index == 0)
  {
    return tokeniser->buffer;
  }
  return tokeniser->buffer + (tokeniser->positions[index - 1] + 1);
}

void uint16_to_hex(uint16_t value, char* hex)
{
  uint8_t temp_message_type[sizeof(value)] = { 0 };
  copy_uint16_to_byte_buffer_big_endian(value, temp_message_type, 0);
  byte_to_hex(temp_message_type, sizeof(temp_message_type), hex, false);
}

static uint16_t hex_char_to_val(char c)
{
  if (c >= '0' && c <= '9')
  {
    return (uint16_t)(c - '0');
  }
  else if (c >= 'a' && c <= 'f')
  {
    return (uint16_t)(10 + (c - 'a'));
  }
  else if (c >= 'A' && c <= 'F')
  {
    return (uint16_t)(10 + (c - 'A'));
  }
  else
  {
    return UINT16_MAX;
  }
}

uint16_t uint16_from_hex_string(const char* hex)
{
  if (strlen(hex) != 4)
  {
    return UINT16_MAX;
  }

  uint16_t result = 0;
  for (int i = 0; i < 4; ++i)
  {
    uint16_t val = hex_char_to_val(hex[i]);
    if (val == UINT16_MAX)
    {
      return UINT16_MAX;
    }
    result = (result << 4) | ((uint8_t)val);
  }

  return result;
}

bool hex_to_bytes(const char* hex, uint32_t hex_length, uint8_t* bytes,
                  uint32_t bytes_max_size)
{
  if ((hex_length % 2) != 0)
  {
    return false;
  }
  uint32_t byte_size = 0;
  for (uint32_t i = 0; i < hex_length;)
  {
    uint8_t result = 0;
    for (uint32_t j = 0; j < 2; ++j)
    {
      uint16_t val = hex_char_to_val(hex[i++]);
      if (val == UINT16_MAX)
      {
        return false;
      }
      result = (result << 4) | ((uint8_t)val);
    }
    bytes[byte_size++] = result;
    if (byte_size >= bytes_max_size)
    {
      break;
    }
  }
  return true;
}

float absolute_float(float value)
{
  return (value < 0.0f) ? (value * -1.0f) : value;
}

time_t absolute_time_t(time_t value)
{
  return (value < 0) ? (value * -1) : value;
}

double round_double_to_4_decimals(double value)
{
  return round(value * 10000.0) / 10000.0;
}

double round_double_to_6_decimals(double value)
{
  return round(value * 1000000.0) / 1000000.0;
}

bool astrour_apply_with_shift(bool overlapped_relay, uint32_t time,
                              float current_altitude, float angle,
                              float current_sun_altitude_width_shift,
                              uint8_t current_shift, uint8_t* astrour_applied_shift,
                              uint32_t* not_applied_shift_switch_timer,
                              uint32_t* off_on_full_shift_timer,
                              uint32_t* off_on_half_shift_timer,
                              uint32_t* on_on_half_shift_timer)
{
  bool applied = false;
  if (overlapped_relay)
  {
    lsx_log("Altitude: %.3f | Overlap: %u\n", current_sun_altitude_width_shift,
            overlapped_relay);
    if (current_sun_altitude_width_shift <= angle)
    {
      if ((*astrour_applied_shift) == ASTROUR_NOT_APPLIED &&
          (current_shift == ASTROUR_HALF_SHIFT))
      {
        (*on_on_half_shift_timer) = time;
      }
      (*astrour_applied_shift) = current_shift;
      applied = true;
    }
    else if ((*astrour_applied_shift) != ASTROUR_NOT_APPLIED)
    {
      if ((*astrour_applied_shift) == ASTROUR_FULL_SHIFT)
      {
        lsx_log("Full shift timer\n");
        (*off_on_full_shift_timer) = time;
      }
      else if ((*astrour_applied_shift) == ASTROUR_HALF_SHIFT)
      {
        lsx_log("Full shift timer\n");
        (*off_on_half_shift_timer) = time;
      }

      lsx_log("Shift switch timer\n");
      (*not_applied_shift_switch_timer) = time;

      (*astrour_applied_shift) = ASTROUR_NOT_APPLIED;
    }
  }
  else
  {
    lsx_log("Altitude: %.3f | Overlap: %u\n", current_altitude, overlapped_relay);
    if (current_altitude <= angle)
    {
      applied = true;
    }
  }
  return applied;
}

void get_message_type_as_hex(char* message_type_as_hex, uint16_t message_type)
{
  uint8_t buffer[2] = {};
  copy_uint16_to_byte_buffer_big_endian(message_type, buffer, 0);
  byte_to_hex(buffer, sizeof(buffer), message_type_as_hex, false);
}

void selection_sort_float(float* array, uint32_t count)
{
  if (count < 1)
  {
    return;
  }
  for (uint32_t i = 0; i < count - 1; i++)
  {
    uint32_t min_index = i;
    for (uint32_t j = i + 1; j < count; j++)
    {
      if (array[j] < array[min_index])
      {
        min_index = j;
      }
    }
    float temp = array[i];
    array[i] = array[min_index];
    array[min_index] = temp;
  }
}

float median_float_buffer(float* buffer, uint32_t count)
{
  selection_sort_float(buffer, count);
  lsx_log("%f", buffer[0]);
  for (uint32_t i = 1; i < count; ++i)
  {
    lsx_log(", %f", buffer[i]);
  }
  lsx_log("\n");

  uint32_t mid = count / 2;
  float result = buffer[mid];
  if (count >= 6)
  {
    float sum = 0;
    for (uint32_t i = mid - 2; i <= mid + 2; ++i)
    {
      sum += buffer[i];
    }
    result = sum / 5;
  }
  return result;
}

void selection_sort_double(double* array, uint32_t count)
{
  if (count < 1)
  {
    return;
  }
  for (uint32_t i = 0; i < count - 1; i++)
  {
    uint32_t min_index = i;
    for (uint32_t j = i + 1; j < count; j++)
    {
      if (array[j] < array[min_index])
      {
        min_index = j;
      }
    }
    double temp = array[i];
    array[i] = array[min_index];
    array[min_index] = temp;
  }
}

double median_double_buffer(double* buffer, uint32_t count, uint32_t avg_count)
{
  selection_sort_double(buffer, count);
  lsx_log("%f", buffer[0]);
  for (uint32_t i = 1; i < count; ++i)
  {
    lsx_log(", %f", buffer[i]);
  }
  lsx_log("\n");

  uint32_t mid = count / 2;
  double result = buffer[mid];
  uint32_t total_count_for_avg = ((avg_count * 2) + 1);
  if (count >= (total_count_for_avg + 1))
  {
    double sum = 0;
    for (uint32_t i = mid - avg_count; i <= mid + avg_count; ++i)
    {
      sum += buffer[i];
    }
    result = sum / total_count_for_avg;
  }
  return result;
}
