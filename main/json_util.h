#ifndef LSX_JSON_UTIL_H
#define LSX_JSON_UTIL_H
#include <stdbool.h>
#include <stdint.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C"
{
#endif

  int json_object_get_number(const cJSON* json, const char* key, int default_value);
  uint32_t json_object_get_uint(const cJSON* json, const char* key, uint32_t default_value);
  float json_object_get_float(const cJSON* json, const char* key, float default_value);
  bool json_string_is_ok(const cJSON* string_object);
  bool json_number_is_ok(const cJSON* number_object);
  bool json_key_number_is_ok(const cJSON* json, const char* key);

  cJSON* json_add_object(cJSON* object, const char* new_object_key);
  cJSON* json_add_object_message_type(cJSON* object, uint16_t message_type);
  cJSON* json_add_number(cJSON* object, const char* new_number_key, double number);
  cJSON* json_add_number_message_type(cJSON* object, uint16_t message_type, double number);
  cJSON* json_add_string(cJSON* object, const char* new_string_key, const char* string);
  cJSON* json_add_string_message_type(cJSON* object, uint16_t message_type, const char* string);


#ifdef __cplusplus
}
#endif

#endif
