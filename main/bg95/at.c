#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_task_wdt.h>
#include <string.h>

#include "at.h"
#include "pin_define.h"
#include "time_simple.h"
#include "platform.h"

#define TOTAL_RECV       4
#define RECV_BUFFER_SIZE STRING2048_BUFFER_SIZE
#define AT_UART_NUMBER   1

static uint32_t last_sent_time = 0;
static uint32_t last_receive_time = 0;
static bool disconnected_flag = false;

static StaticQueue_t recv_queue_structure = {};
static uint8_t recv_buffer_storage[TOTAL_RECV * RECV_BUFFER_SIZE] = {};
static QueueHandle_t recv_queue = NULL;

static string2048_t response = {};

void at_initialize(uint32_t rate)
{
  lsx_uart_initialize(AT_UART_NUMBER, rate, BG95_TX_PIN, BG95_RX_PIN, 4 * 1024,
                      6 * 1024);

  recv_queue = xQueueCreateStatic(TOTAL_RECV, RECV_BUFFER_SIZE, recv_buffer_storage,
                                  &recv_queue_structure);
}

void at_end(void)
{
}

void* at_recv_queue(void)
{
  return recv_queue;
}

bool at_disconnected_flag(void)
{
  return disconnected_flag;
}

void at_reset_disconnected_flag(void)
{
  disconnected_flag = false;
}

uint32_t at_last_sent_time(void)
{
  return last_sent_time;
}

uint32_t at_last_receive_time(void)
{
  return last_receive_time;
}

void at_set_last_sent_time(uint32_t last_time)
{
  last_sent_time = last_time;
}

void at_set_last_receive_time(uint32_t last_time)
{
  last_receive_time = last_time;
}

int32_t at_available(void)
{
  return lsx_uart_available(AT_UART_NUMBER);
}

void at_clear(void)
{
  lsx_uart_clear(AT_UART_NUMBER);
}

void at_serial_send(const char* command)
{
  lsx_uart_write(AT_UART_NUMBER, command, strlen(command));
  lsx_uart_write(AT_UART_NUMBER, "\r\n", 2);
  last_sent_time = lsx_get_millis();
}

uint32_t at_serial_read_line(char* buffer, uint32_t buffer_size)
{
  uint32_t bytes_read =
    lsx_uart_read_until_string(AT_UART_NUMBER, buffer, buffer_size, '\n', 1000, NULL);
  last_receive_time = lsx_get_millis();
  return bytes_read;
}

uint32_t at_read_file(uint32_t file_handle, uint8_t* buffer, uint32_t buffer_size)
{
  string64_t command = {};
  string64(&command, "AT+QFREAD=%u,%u", file_handle, buffer_size);

  at_serial_send(command.data);

  last_sent_time = lsx_get_millis();

  bool read_header = false;
  uint32_t size_read = 0;

  int64_t start_time = lsx_get_micro();
  while ((lsx_get_micro() - start_time) <= 10000000ULL)
  {
    esp_task_wdt_reset();

    if (!read_header)
    {
      if (lsx_uart_available(AT_UART_NUMBER) > 0)
      {
        const char* header_title = "CONNECT ";
        const uint32_t header_title_len = (uint32_t)strlen(header_title);

        response.length = at_serial_read_line(response.data, sizeof(response.data));

        if ((response.length < sizeof(response.data)) &&
            (response.length > header_title_len))
        {
          response.data[response.length] = '\0';
          int32_t header_index = string2048_find(&response, header_title, 0, -1);
          if (header_index != -1)
          {
            header_index += header_title_len;
            if (header_index < response.length)
            {
              size_read = (uint32_t)strtoul(response.data + header_index, NULL, 0);
              if (size_read > buffer_size)
              {
                return 0;
              }
              read_header = true;
            }
          }
        }
        last_receive_time = lsx_get_millis();
      }
    }
    else if (lsx_uart_available(AT_UART_NUMBER) >= size_read)
    {
      lsx_uart_read(AT_UART_NUMBER, buffer, size_read, 100);
      last_receive_time = lsx_get_millis();
      lsx_uart_clear(AT_UART_NUMBER);
      return size_read;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  return 0;
}

bool at_send_and_wait_for_response_internal(const char* command,
                                            const char* expected_response,
                                            uint32_t timeout_ms, bool record_recv,
                                            string2048_t* response_out,
                                            bool check_for_ok,
                                            bool leave_if_disconnected, bool log)
{

  if (response_out != NULL)
  {
    string2048_reset(response_out);
  }

  at_serial_send(command);
  if (log)
  {
    lsx_log("%s\n", command);
  }

  bool expected_is_ok = strcmp(expected_response, "OK") == 0;
  bool okFound = !check_for_ok || expected_is_ok;
  bool expected_found = false;
  bool echo_read = !expected_is_ok;
  bool read_response = false;

  int64_t timeout_micro = ((int64_t)timeout_ms) * 1000ULL;
  uint32_t start_time = lsx_get_micro();
  while ((!okFound || !expected_found) &&
         (lsx_get_micro() - start_time) <= timeout_micro)
  {
    esp_task_wdt_reset();

    if (lsx_uart_available(AT_UART_NUMBER) > 0)
    {
      string2048_reset(&response);

      response.length = at_serial_read_line(response.data, sizeof(response.data));
      if (log)
      {
        lsx_log("%s\n", response.data);
      }

      if (!echo_read && (string2048_find(&response, command, 0, -1) != -1))
      {
        echo_read = true;
      }
      else if (expected_is_ok && echo_read && !read_response)
      {
        read_response = true;
        if (response_out != NULL && (response.length > 2))
        {
          string2048_copy(response_out, &response);
        }
      }

      if (!expected_found &&
          (string2048_find(&response, expected_response, 0, -1) != -1))
      {
        expected_found = true;
        if (response_out != NULL && !read_response)
        {
          string2048_copy(response_out, &response);
        }
      }
      else if (!okFound && (string2048_find(&response, "OK", 0, -1) != -1))
      {
        okFound = true;
      }
      else if ((string2048_find(&response, "ERROR", 0, -1) != -1))
      {
        if (response_out != NULL)
        {
          string2048_copy(response_out, &response);
        }
        return false;
      }
      else if ((string2048_find(&response, "APP RDY", 0, -1) != -1) ||
               (string2048_find(&response, "+QMTSTAT:", 0, -1) != -1) ||
               (string2048_find(&response, "+QIURC: \"pdpdeact\"", 0, -1) != -1))
      {
        disconnected_flag = true;
        if (leave_if_disconnected)
        {
          return false;
        }
      }
      else if (record_recv && (string2048_find(&response, "+QMTRECV:", 0, -1) != -1))
      {
        if (recv_queue != NULL)
        {
          xQueueSend(recv_queue, response.data, pdMS_TO_TICKS(100));
        }
      }
      else if (string2048_find(&response, "+QNTP: 0,", 0, -1) != -1)
      {
        time_info_t te = {};
        if (parse_ntp_response(&response, &te))
        {
          uint32_t ntp_time = (uint32_t)lsx_make_time(&te);
          lsx_log("Updated time: %lu\n", ntp_time);
          time_simple_set_time(ntp_time);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  return okFound && expected_found;
}

bool at_send_and_wait_for_response(const char* command, const char* expected_response,
                                   uint32_t timeout_ms, bool record_recv,
                                   string2048_t* response_out, bool check_for_ok,
                                   bool leave_if_disconnected)
{
  return at_send_and_wait_for_response_internal(command, expected_response, timeout_ms,
                                                record_recv, response_out, check_for_ok,
                                                leave_if_disconnected, true);
}

bool at_send_and_wait_for_response_no_log(const char* command,
                                          const char* expected_response,
                                          uint32_t timeout_ms, bool record_recv,
                                          string2048_t* response_out, bool check_for_ok,
                                          bool leave_if_disconnected)
{
  return at_send_and_wait_for_response_internal(command, expected_response, timeout_ms,
                                                record_recv, response_out, check_for_ok,
                                                leave_if_disconnected, false);
}
