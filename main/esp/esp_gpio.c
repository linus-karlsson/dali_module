#include <driver/gpio.h>

#include "platform.h"

static gpio_mode_t lsx_to_esp_gpio_mode(lsx_gpio_mode_t mode)
{
  switch (mode)
  {
    case LSX_GPIO_MODE_INPUT:
    {
      return GPIO_MODE_INPUT;
    }
    case LSX_GPIO_MODE_OUTPUT:
    {
      return GPIO_MODE_OUTPUT;
    }
  }
  return GPIO_MODE_DISABLE;
}

static gpio_int_type_t lsx_to_esp_gpio_interrupt(lsx_gpio_interrupt_t interrupt_type)
{
  switch (interrupt_type)
  {
    case LSX_GPIO_INTR_DISABLE:
    {
      return GPIO_INTR_DISABLE;
    }
    case LSX_GPIO_INTR_POSEDGE:
    {
      return GPIO_INTR_POSEDGE;
    }
    case LSX_GPIO_INTR_NEGEDGE:
    {
      return GPIO_INTR_NEGEDGE;
    }
    case LSX_GPIO_INTR_ANYEDGE:
    {
      return GPIO_INTR_ANYEDGE;
    }
    case LSX_GPIO_INTR_LOW_LEVEL:
    {
      return GPIO_INTR_LOW_LEVEL;
    }
    case LSX_GPIO_INTR_HIGH_LEVEL:
    {
      return GPIO_INTR_HIGH_LEVEL;
    }
  }
  return GPIO_INTR_DISABLE;
}

void lsx_gpio_config(uint8_t gpio_number, lsx_gpio_mode_t mode,
                     lsx_gpio_interrupt_t interrupt_type, bool pullup, bool pulldown)
{
  gpio_config_t gpio_conf = {};
  gpio_conf.pin_bit_mask = (1ULL << gpio_number);
  gpio_conf.mode = lsx_to_esp_gpio_mode(mode);
  gpio_conf.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  gpio_conf.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  gpio_conf.intr_type = lsx_to_esp_gpio_interrupt(interrupt_type);
  gpio_config(&gpio_conf);
}

void lsx_gpio_write(uint8_t gpio_number, lsx_gpio_level_t level)
{
  gpio_set_level((gpio_num_t)gpio_number, level);
}

uint8_t lsx_gpio_read(uint8_t gpio_number)
{
  return (uint8_t)gpio_get_level((gpio_num_t)gpio_number);
}

static bool installed = false;
void lsx_gpio_install_interrupt_service(void)
{
  // TODO(Linus): mutex
  if (!installed)
  {
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    installed = true;
  }
}

void lsx_gpio_add_pin_interrput(uint8_t gpio_number, void (*interrupt_callback)(void*),
                                void* arguments)
{
  gpio_isr_handler_add((gpio_num_t)gpio_number, interrupt_callback, arguments);
}

void lsx_gpio_remove_pin_interrput(uint8_t gpio_number)
{
  gpio_isr_handler_remove((gpio_num_t)gpio_number);
}
