#include "json_util.h"
#include "util.h"

int json_object_get_number(const cJSON* json, const char* key, int default_value)
{
  const cJSON* number = cJSON_GetObjectItemCaseSensitive(json, key);
  if (json_number_is_ok(number))
  {
    return number->valueint;
  }
  return default_value;
}

uint32_t json_object_get_uint(const cJSON* json, const char* key, uint32_t default_value)
{
  const cJSON* number = cJSON_GetObjectItemCaseSensitive(json, key);
  if (json_number_is_ok(number))
  {
    return (uint32_t)number->valuedouble;
  }
  return default_value;
}

float json_object_get_float(const cJSON* json, const char* key, float default_value)
{
  const cJSON* number = cJSON_GetObjectItemCaseSensitive(json, key);
  if (json_number_is_ok(number))
  {
    return (float)number->valuedouble;
  }
  return default_value;
}

bool json_string_is_ok(const cJSON* string_object)
{
  return (string_object != NULL) && cJSON_IsString(string_object) &&
         (string_object->valuestring != NULL);
}

bool json_number_is_ok(const cJSON* number_object)
{
  return (number_object != NULL) && cJSON_IsNumber(number_object);
}

bool json_key_number_is_ok(const cJSON* json, const char* key)
{
  const cJSON* number = cJSON_GetObjectItemCaseSensitive(json, key);
  return json_number_is_ok(number);
}

cJSON* json_add_object(cJSON* object, const char* new_object_key)
{
  cJSON* result = cJSON_GetObjectItemCaseSensitive(object, new_object_key);
  if (result == NULL)
  {
    result = cJSON_AddObjectToObject(object, new_object_key);
  }
  return result;
}

cJSON* json_add_number(cJSON* object, const char* new_number_key, double number)
{
  cJSON* number_object = cJSON_GetObjectItemCaseSensitive(object, new_number_key);
  if (number_object == NULL)
  {
    number_object = cJSON_AddNumberToObject(object, new_number_key, number);
  }
  else
  {
    cJSON_SetNumberValue(number_object, number);
  }
  return number_object;
}

cJSON* json_add_string(cJSON* object, const char* new_string_key, const char* string)
{
  cJSON* string_object = cJSON_GetObjectItemCaseSensitive(object, new_string_key);
  if (string_object == NULL)
  {
    string_object = cJSON_AddStringToObject(object, new_string_key, string);
  }
  else
  {
    cJSON_SetValuestring(string_object, string);
  }
  return string_object;
}

cJSON* json_add_number_message_type(cJSON* object, uint16_t message_type, double number)
{
  char message_type_string[(sizeof(message_type) * 2) + 1] = { 0 };
  get_message_type_as_hex(message_type_string, message_type);
  return json_add_number(object, message_type_string, number);
}

cJSON* json_add_string_message_type(cJSON* object, uint16_t message_type, const char* string)
{
  char message_type_string[(sizeof(message_type) * 2) + 1] = { 0 };
  get_message_type_as_hex(message_type_string, message_type);
  return json_add_string(object, message_type_string, string);
}

cJSON* json_add_object_message_type(cJSON* object, uint16_t message_type)
{
  char message_type_string[(sizeof(message_type) * 2) + 1] = { 0 };
  get_message_type_as_hex(message_type_string, message_type);
  return json_add_object(object, message_type_string);
}
