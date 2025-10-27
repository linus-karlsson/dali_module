#include <esp_timer.h>

#include "platform.h"

uint32_t lsx_get_millis(void)
{
  return (uint32_t)(esp_timer_get_time() / 1000);
}

uint32_t lsx_get_micro(void)
{
  return (uint32_t)esp_timer_get_time();
}

void lsx_delay_micro(int64_t us)
{
  int64_t start = (int64_t)esp_timer_get_time();
  while (((int64_t)esp_timer_get_time() - start) <= us)
    ;
}

void lsx_delay_millis(int64_t ms)
{
  lsx_delay_micro(ms * 1000);
}

lsx_timer_handle_t lsx_timer_create(void (*timer_callback)(void*), void* args)
{
  esp_timer_create_args_t timer_args = {};
  timer_args.callback = timer_callback;
  timer_args.arg = args;

  esp_timer_handle_t timer = NULL;
  esp_timer_create(&timer_args, &timer);

  return (lsx_timer_handle_t)timer;
}

void lsx_timer_destroy(lsx_timer_handle_t handle)
{
  esp_timer_stop((esp_timer_handle_t)handle);
  esp_timer_delete((esp_timer_handle_t)handle);
}

bool lsx_timer_start(lsx_timer_handle_t handle, uint64_t when, bool repeated)
{
  esp_timer_stop((esp_timer_handle_t)handle);
  if (repeated)
  {
    return esp_timer_start_periodic((esp_timer_handle_t)handle, when) == ESP_OK;
  }
  else
  {
    return esp_timer_start_once((esp_timer_handle_t)handle, when) == ESP_OK;
  }
}
