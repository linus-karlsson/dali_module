#include <nvs.h>
#include <nvs_flash.h>

#include "platform.h"

void lsx_nvs_initialize(void)
{
  esp_err_t error = nvs_flash_init();
  if ((error == ESP_ERR_NVS_NO_FREE_PAGES) || (error == ESP_ERR_NVS_NEW_VERSION_FOUND))
  {
    nvs_flash_erase();
    nvs_flash_init();
  }
}

void lsx_nvs_open(nvs_t* nvs, const char* name)
{
  nvs_open(name, NVS_READWRITE, &nvs->handle);
}

void lsx_nvs_close(nvs_t* nvs)
{
  nvs_close(nvs->handle);
}

void lsx_nvs_clear(nvs_t* nvs)
{
  if (nvs_erase_all(nvs->handle) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_remove_key(nvs_t* nvs, const char* key)
{
  nvs_erase_key(nvs->handle, key);
}

void lsx_nvs_remove(nvs_t* nvs, const char* key)
{
  if (nvs_erase_key(nvs->handle, key) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_commit(nvs_t* nvs)
{
  nvs_commit(nvs->handle);
}

void lsx_nvs_set_int8(nvs_t* nvs, const char* key, int8_t value)
{
  if (nvs_set_i8(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_int16(nvs_t* nvs, const char* key, int16_t value)
{
  if (nvs_set_i16(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_int32(nvs_t* nvs, const char* key, int32_t value)
{
  if (nvs_set_i32(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_uint8(nvs_t* nvs, const char* key, uint8_t value)
{
  if (nvs_set_u8(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_uint16(nvs_t* nvs, const char* key, uint16_t value)
{
  if (nvs_set_u16(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_uint32(nvs_t* nvs, const char* key, uint32_t value)
{
  if (nvs_set_u32(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_float(nvs_t* nvs, const char* key, float value)
{
  lsx_nvs_set_bytes(nvs, key, (void*)&value, sizeof(float));
}

void lsx_nvs_set_string(nvs_t* nvs, const char* key, const char* value)
{
  if (nvs_set_str(nvs->handle, key, value) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

void lsx_nvs_set_bytes(nvs_t* nvs, const char* key, const void* value, uint32_t size)
{
  if (nvs_set_blob(nvs->handle, key, value, size) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}

bool lsx_nvs_set_int8_ram(nvs_t* nvs, const char* key, int8_t value)
{
  return nvs_set_i8(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_int16_ram(nvs_t* nvs, const char* key, int16_t value)
{
  return nvs_set_i16(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_int32_ram(nvs_t* nvs, const char* key, int32_t value)
{
  return nvs_set_i32(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_uint8_ram(nvs_t* nvs, const char* key, uint8_t value)
{
  return nvs_set_u8(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_uint16_ram(nvs_t* nvs, const char* key, uint16_t value)
{
  return nvs_set_u16(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_uint32_ram(nvs_t* nvs, const char* key, uint32_t value)
{
  return nvs_set_u32(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_float_ram(nvs_t* nvs, const char* key, float value)
{
  return lsx_nvs_set_bytes_ram(nvs, key, (const void*)&value, sizeof(float));
}

bool lsx_nvs_set_string_ram(nvs_t* nvs, const char* key, const char* value)
{
  return nvs_set_str(nvs->handle, key, value) == ESP_OK;
}

bool lsx_nvs_set_bytes_ram(nvs_t* nvs, const char* key, const void* value,
                           uint32_t size)
{
  return nvs_set_blob(nvs->handle, key, value, size) == ESP_OK;
}

bool lsx_nvs_get_int8(nvs_t* nvs, const char* key, int8_t* value, int8_t default_value)
{
  int8_t temp = default_value;
  bool success = nvs_get_i8(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_int16(nvs_t* nvs, const char* key, int16_t* value,
                       int16_t default_value)
{
  int16_t temp = default_value;
  bool success = nvs_get_i16(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_int32(nvs_t* nvs, const char* key, int32_t* value,
                       int32_t default_value)
{
  int32_t temp = default_value;
  bool success = nvs_get_i32(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_uint8(nvs_t* nvs, const char* key, uint8_t* value,
                       uint8_t default_value)
{
  uint8_t temp = default_value;
  bool success = nvs_get_u8(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_uint16(nvs_t* nvs, const char* key, uint16_t* value,
                        uint16_t default_value)
{
  uint16_t temp = default_value;
  bool success = nvs_get_u16(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_uint32(nvs_t* nvs, const char* key, uint32_t* value,
                        uint32_t default_value)
{
  uint32_t temp = default_value;
  bool success = nvs_get_u32(nvs->handle, key, &temp) == ESP_OK;
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_float(nvs_t* nvs, const char* key, float* value, float default_value)
{
  float temp = default_value;
  bool success = lsx_nvs_get_bytes(nvs, key, (void*)&temp, NULL, sizeof(float));
  if (value)
  {
    *value = success ? temp : default_value;
  }
  return success;
}

bool lsx_nvs_get_string(nvs_t* nvs, const char* key, char* value,
                        uint32_t* value_length, uint32_t max_length)
{
  bool success = false;
  size_t length = 0;
  if (nvs_get_str(nvs->handle, key, NULL, &length) == ESP_OK)
  {
    success = true;
    if (value != NULL)
    {
      success = false;
      if (length <= max_length)
      {
        success = nvs_get_str(nvs->handle, key, value, &length) == ESP_OK;
      }
    }
  }
  if (value_length)
  {
    *value_length = length;
  }
  return success;
}

bool lsx_nvs_get_bytes(nvs_t* nvs, const char* key, void* value, uint32_t* value_size,
                       uint32_t max_size)
{
  bool success = false;
  size_t size = 0;
  if (nvs_get_blob(nvs->handle, key, NULL, &size) == ESP_OK)
  {
    success = true;
    if (value != NULL)
    {
      success = false;
      if (size <= max_size)
      {
        success = nvs_get_blob(nvs->handle, key, value, &size) == ESP_OK;
      }
    }
  }
  if (value_size)
  {
    *value_size = size;
  }
  return success;
}

void lsx_nvs_erase_all(nvs_t* nvs)
{
  if (nvs_erase_all(nvs->handle) == ESP_OK)
  {
    nvs_commit(nvs->handle);
  }
}
