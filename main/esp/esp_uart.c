#include <driver/uart.h>

#include "platform.h"
#include "util.h"

void lsx_uart_initialize(int32_t uart_number, int32_t baud_rate, int32_t tx_pin,
                         int32_t rx_pin, int32_t tx_buffer_size, int32_t rx_buffer_size)
{
  uart_config_t uart_config = {};
  uart_config.baud_rate = baud_rate;
  uart_config.data_bits = UART_DATA_8_BITS;
  uart_config.parity = UART_PARITY_DISABLE;
  uart_config.stop_bits = UART_STOP_BITS_1;
  uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;

  uart_param_config((uart_port_t)uart_number, &uart_config);
  uart_set_pin((uart_port_t)uart_number, tx_pin, rx_pin, UART_PIN_NO_CHANGE,
               UART_PIN_NO_CHANGE);
  uart_driver_install((uart_port_t)uart_number, rx_buffer_size, tx_buffer_size, 0, NULL,
                      0);
}

void lsx_uart_set_baudrate(int32_t uart_number, uint32_t new_baudrate)
{
  uart_set_baudrate((uart_port_t)uart_number, new_baudrate);
}

uint32_t lsx_uart_get_baudrate(int32_t uart_number)
{
  uint32_t baud_rate = 0;
  uart_get_baudrate((uart_port_t)uart_number, &baud_rate);
  return baud_rate;
}

void lsx_uart_write(int32_t uart_number, const void* bytes, uint32_t size)
{
  uart_write_bytes((uart_port_t)uart_number, bytes, size);
}

int32_t lsx_uart_available(int32_t uart_number)
{
  size_t result = 0;
  return (uart_get_buffered_data_len((uart_port_t)uart_number, &result) == ESP_OK)
           ? result
           : 0;
}

static bool read_byte(int32_t uart_number, uint32_t timeout_ms, uint8_t* data)
{
  return (uart_read_bytes((uart_port_t)uart_number, data, sizeof(uint8_t),
                          pdMS_TO_TICKS(timeout_ms)) > 0);
}

uint32_t lsx_uart_read_until(int32_t uart_number, uint8_t* buffer,
                             uint32_t buffer_max_size, char character,
                             uint32_t timeout_ms, bool* timedout)
{
  uint32_t index = 0;
  while (index < buffer_max_size)
  {
    uint8_t data = 0;
    if (read_byte(uart_number, timeout_ms, &data))
    {
      if (((char)data) == character)
      {
        break;
      }
      buffer[index++] = data;
    }
    else
    {
      if (timedout)
      {
        *timedout = true;
      }
      break;
    }
  }
  return index;
}

uint32_t lsx_uart_read_until_string(int32_t uart_number, char* buffer,
                                    uint32_t buffer_max_size, char character,
                                    uint32_t timeout_ms, bool* timedout)
{
  uint32_t result =
    lsx_uart_read_until(uart_number, (uint8_t*)buffer, buffer_max_size - 1, character,
                        timeout_ms, timedout);
  buffer[result] = '\0';

  return result;
}

int32_t lsx_uart_read(int32_t uart_number, uint8_t* buffer, uint32_t size, uint32_t timeout_ms)
{
  return uart_read_bytes((uart_port_t)uart_number, buffer, size, pdMS_TO_TICKS(timeout_ms));
}

void lsx_uart_clear(int32_t uart_number)
{
  uart_flush((uart_port_t)uart_number);
}
