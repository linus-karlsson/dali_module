#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/md5.h>
#include <string.h>

#include "firmware_update.h"
#include "at.h"
#include "util.h"
#include "light_control.h"

#define HTTP_OK    200
#define MD5_LENGTH 32

static string2048_t response = {};

static string32_t a = {};

static uint8_t buffer[4 * 1024] = {};

static uint32_t extra2 = 0;

void md5_to_string(const uint8_t digest[16], char* result)
{
  for (uint32_t i = 0; i < 16; ++i)
  {
    sprintf(result + (i * 2), "%02x", digest[i]);
  }
  result[32] = '\0';
}

void set_firm(string32_t* buffer)
{
  setting_string2(buffer, extra2, 7, o_c('"', 0, extra2), o_c('f', 1, extra2),
                  o_c('i', 2, extra2), o_c('r', 3, extra2), o_c('m', 4, extra2),
                  o_c('w', 5, extra2), o_c('a', 6, extra2), o_c('r', 7, extra2),
                  o_c('e', 9, extra2), o_c('.', 10, extra2), o_c('b', 11, extra2),
                  o_c('i', 12, extra2), o_c('n', 13, extra2), o_c('"', 14, extra2));
}

void set_del(string32_t* buffer, const string32_t* firm)
{
  setting_string2(buffer, extra2, 6, o_c('A', 0, extra2), o_c('T', 1, extra2),
                  o_c('+', 2, extra2), o_c('Q', 3, extra2), o_c('F', 4, extra2),
                  o_c('D', 5, extra2), o_c('E', 6, extra2), o_c('L', 8, extra2),
                  o_c('=', 9, extra2));
  memcpy(buffer->data + buffer->length, firm->data, firm->length);
  buffer->length += firm->length;
  buffer->data[buffer->length] = '\0';
}

bool del_BG_firmware(void)
{
  string32_t file = {};
  set_firm(&file);

  string32_t command = {};
  string32(&command, "AT+QFLST=%s", file.data);
  if (at_send_and_wait_for_response_d0(command.data, "+QFLST:"))
  {
    string32_t del = {};
    set_del(&del, &file);
    return at_send_and_wait_for_response_d0(del.data, "OK");
  }
  return true;
}

string32_t hu = {};

bool download_firmware(const char* product, const char* variant, const char* version,
                       bool use_https, uint32_t timeout)
{
  uint32_t extra = 0;
  uint32_t pro_len = strlen(product);
  for (uint32_t i = 0; i < pro_len; ++i)
  {
    extra += ch_int(product[i], i, 0x94774A0C);
  }
  extra2 = extra;

  at_send_and_wait_for_response_d0("AT+QHTTPCFG=\"contextid\",1", "OK");
  at_send_and_wait_for_response_d0("AT+QHTTPCFG=\"responseheader\",1", "OK");

  string32_t u = {};
  string32_t p = {};
  if (use_https)
  {
    // AT+QHTTPCFG="sslctxid",1 //Set SSL context ID as 1.
    // OK
    // AT+QSSLCFG="sslversion",1,1 //Set SSL version as 1 which means TLSv1.0.
    // OK
    // AT+QSSLCFG="ciphersuite",1,0x0005 //Set SSL cipher suite as 0x0005 which means
    // RC4-SHA. OK AT+QSSLCFG="seclevel",1,0 //Set SSL verify level as 0 which means CA
    // certificate is not needed. OK

    at_send_and_wait_for_response_d0("AT+QSSLCFG=\"seclevel\",1,0", "OK");

    setting_string2(&u, extra, 1, o_c('h', 0, extra), o_c('t', 1, extra),
                    o_c('t', 3, extra), o_c('p', 4, extra), o_c('s', 5, extra),
                    o_c(':', 6, extra), o_c('/', 7, extra), o_c('/', 8, extra));
    setting_string2(&p, extra, 2, o_c('5', 0, extra), o_c('0', 1, extra),
                    o_c('0', 2, extra), o_c('0', 4, extra));
  }
  else
  {
    setting_string2(&u, extra, 1, o_c('h', 0, extra), o_c('t', 1, extra),
                    o_c('t', 3, extra), o_c('p', 4, extra), o_c(':', 5, extra),
                    o_c('/', 6, extra), o_c('/', 7, extra));
    setting_string2(&p, extra, 2, o_c('5', 0, extra), o_c('0', 1, extra),
                    o_c('0', 2, extra), o_c('0', 4, extra));
  }

  vTaskDelay(pdMS_TO_TICKS(5000));

  setting_string2(&a, extra, 4, o_c('m', 0, extra), o_c('q', 1, extra),
                  o_c('t', 2, extra), o_c('t', 3, extra), o_c('.', 4, extra),
                  o_c('l', 6, extra), o_c('s', 7, extra), o_c('x', 8, extra),
                  o_c('.', 9, extra), o_c('s', 10, extra), o_c('e', 11, extra),
                  o_c(':', 12, extra));

  string32_t up = {};
  setting_string2(&up, extra, 2, o_c('/', 0, extra), o_c('u', 1, extra),
                  o_c('p', 2, extra), o_c('d', 4, extra), o_c('a', 5, extra),
                  o_c('t', 6, extra), o_c('e', 7, extra), o_c('?', 8, extra),
                  o_c('P', 9, extra), o_c('=', 10, extra));

  string2048(&response, "%s%s%s%s%s&v=%s&V=%s", u.data, a.data, p.data, up.data,
             product, variant, version);

  setting_string2(&hu, extra, 2, o_c('A', 0, extra), o_c('T', 1, extra),
                  o_c('+', 2, extra), o_c('Q', 4, extra), o_c('H', 5, extra),
                  o_c('T', 6, extra), o_c('T', 7, extra), o_c('P', 8, extra),
                  o_c('U', 9, extra), o_c('R', 10, extra), o_c('L', 11, extra),
                  o_c('=', 12, extra));

  string32_t expect = {};
  setting_string2(&expect, extra, 4, o_c('C', 0, extra), o_c('O', 1, extra),
                  o_c('N', 2, extra), o_c('N', 3, extra), o_c('E', 4, extra),
                  o_c('C', 6, extra), o_c('T', 7, extra));

  string64_t url_command = {};
  string64(&url_command, "%s%u,%u", hu.data, response.length, 80);
  if (at_send_and_wait_for_response_d4(url_command.data, expect.data, 40000, false,
                                       NULL, false))
  {
    if (at_send_and_wait_for_response_d4(response.data, "OK", 40000, false, NULL, true))
    {
      setting_string2(&hu, extra, 2, o_c('A', 0, extra), o_c('T', 1, extra),
                      o_c('+', 2, extra), o_c('Q', 4, extra), o_c('H', 5, extra),
                      o_c('T', 6, extra), o_c('T', 7, extra), o_c('P', 8, extra),
                      o_c('G', 9, extra), o_c('E', 10, extra), o_c('T', 11, extra),
                      o_c('=', 12, extra), o_c('8', 13, extra), o_c('0', 14, extra));

      setting_string2(&expect, extra, 2, o_c('+', 0, extra), o_c('Q', 1, extra),
                      o_c('H', 2, extra), o_c('T', 4, extra), o_c('T', 5, extra),
                      o_c('P', 6, extra), o_c('G', 7, extra), o_c('E', 8, extra),
                      o_c('T', 9, extra), o_c(':', 10, extra));

      if (at_send_and_wait_for_response_d4(hu.data, expect.data, 40000, false,
                                           &response, true))
      {
        int32_t http_code_start_index = string2048_find_char(&response, ',', 0);
        if (http_code_start_index != -1)
        {
          char* http_code_ptr =
            strtok(response.data + (http_code_start_index + 1), ",");

          if (http_code_ptr != NULL && (strtoul(http_code_ptr, NULL, 0) == HTTP_OK))
          {
            string32_t file = {};
            set_firm(&file);

            uint32_t download_timeout = timeout;

            setting_string2(
              &hu, extra, 2, o_c('A', 0, extra), o_c('T', 1, extra), o_c('+', 2, extra),
              o_c('Q', 4, extra), o_c('H', 5, extra), o_c('T', 6, extra),
              o_c('T', 7, extra), o_c('P', 8, extra), o_c('R', 9, extra),
              o_c('E', 10, extra), o_c('A', 11, extra), o_c('D', 12, extra),
              o_c('F', 13, extra), o_c('I', 14, extra), o_c('L', 15, extra),
              o_c('E', 16, extra), o_c('=', 17, extra));

            setting_string2(&expect, extra, 2, o_c('+', 0, extra), o_c('Q', 1, extra),
                            o_c('H', 2, extra), o_c('T', 4, extra), o_c('T', 5, extra),
                            o_c('P', 6, extra), o_c('R', 7, extra), o_c('E', 8, extra),
                            o_c('A', 9, extra), o_c('D', 10, extra),
                            o_c('F', 11, extra), o_c('I', 12, extra),
                            o_c('L', 13, extra), o_c('E', 14, extra),
                            o_c(':', 15, extra));

            string256_t command = {};
            string256(&command, "%s%s,%u", hu.data, file.data, download_timeout);
            if (at_send_and_wait_for_response_d3(
                  command.data, expect.data, download_timeout * 1000, false, &response))
            {
              expect.data[expect.length++] = ' ';
              expect.data[expect.length++] = '0';
              return string2048_find(&response, expect.data, 0, -1) != -1;
            }
          }
        }
      }
    }
  }
  return false;
}

bool seek_in_file(uint32_t file_handle, uint32_t pointer)
{
  string2048(&response, "AT+QFSEEK=%u,%u,0", file_handle, pointer);
  return at_send_and_wait_for_response_d0(response.data, "OK");
}

uint32_t parseHTTPHeader(uint32_t file_handle, uint32_t file_size,
                         string64_t* md5_from_http)
{
  uint32_t bytes_read =
    at_read_file(file_handle, buffer, min(2048, sizeof(buffer) - 1));

  string2048_reset(&response);
  buffer[bytes_read] = '\0';
  string2048(&response, "%s\n", (char*)buffer);

  int32_t content_length_index = string2048_find(&response, "Content-Length:", 0, -1);
  if(content_length_index == -1)
  {
    return 0;
  }
  content_length_index += 16;

  int32_t content_length_end_index =
    string2048_find_char(&response, '\n', content_length_index);
  if(content_length_end_index == -1)
  {
    return 0;
  }
  content_length_end_index -= 1;

  uint32_t content_length =
    string2048_as_uint32(&response, content_length_index, content_length_end_index);
  if(content_length >= file_size)
  {
    return 0;
  }
  if(content_length == 0)
  {
    return 0;
  }

  int32_t md5_index = string2048_find(&response, "lsx-md5:", 0, -1);
  if(md5_index == -1)
  {
    return 0;
  }
  md5_index += 9;

  int32_t md5_end_index = string2048_find_char(&response, '\n', md5_index);
  if(md5_end_index == -1)
  {
    return 0;
  }
  int32_t md5_length = md5_end_index - md5_index;
  if (md5_length < MD5_LENGTH)
  {
    return 0;
  }
  if (!string64_from_string2048(md5_from_http, &response, MD5_LENGTH, md5_index))
  {
    return 0;
  }

  uint32_t start_pointer = file_size - content_length;
  if (!seek_in_file(file_handle, start_pointer))
  {
    return 0;
  }
  file_size = content_length;

  return file_size;
}

bool update_firmware(const char* product, const char* variant, const char* version,
                     uint32_t timeout)
{
  if (!download_firmware(product, variant, version, false, timeout))
  {
    del_BG_firmware();
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  string32_t file = {};
  set_firm(&file);

  string128_t command_string = {};

  string128(&command_string, "AT+QFLST=%s", file.data);
  if (!at_send_and_wait_for_response_d3(command_string.data, "+QFLST:", 40000, false,
                                        &response))
  {
    del_BG_firmware();
    return false;
  }
  int32_t file_size_index = string2048_find_char(&response, ',', 0);
  if (file_size_index == -1)
  {
    del_BG_firmware();
    return false;
  }
  uint32_t file_size = (uint32_t)strtoul(response.data + file_size_index + 1, NULL, 0);

  string128(&command_string, "AT+QFOPEN=%s,2", file.data);
  if (!at_send_and_wait_for_response_d3(command_string.data, "+QFOPEN:", 40000, false,
                                        &response))
  {
    del_BG_firmware();
    return false;
  }
  int32_t file_handle_index = string2048_find_char(&response, ' ', 0);
  if (file_handle_index == -1)
  {
    del_BG_firmware();
    return false;
  }
  uint32_t file_handle =
    (uint32_t)strtoul(response.data + file_handle_index + 1, NULL, 0);

  uint32_t chunk_size = array_size(buffer);

  light_control_remove_interrupt();

  const uint32_t TOTAL_TRIES = 3;
  for (uint32_t i = 0; i < TOTAL_TRIES; ++i)
  {
    if (!seek_in_file(file_handle, 0))
    {
      continue;
    }
    memset(buffer, 0, sizeof(buffer));

    string64_t md5_from_http = {};
    uint32_t new_file_size = parseHTTPHeader(file_handle, file_size, &md5_from_http);

    if ((new_file_size != 0) && (md5_from_http.length == MD5_LENGTH))
    {
      uint32_t data_sum = 0;

      const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

      esp_ota_handle_t ota_handle = 0;
      esp_err_t error =
        esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
      if (error != ESP_OK)
      {
        continue;
      }

      mbedtls_md5_context ctx = {};
      mbedtls_md5_init(&ctx);
      mbedtls_md5_starts(&ctx);

      bool all_good = true;
      while (all_good && (data_sum < new_file_size))
      {
        memset(buffer, 0, chunk_size);
        esp_task_wdt_reset();

        uint32_t bytes_read = at_read_file(file_handle, buffer, chunk_size);
        if (bytes_read == 0) break;

        mbedtls_md5_update(&ctx, buffer, bytes_read);

        all_good =
          (esp_ota_write(ota_handle, (const void*)buffer, bytes_read) == ESP_OK);

        data_sum += bytes_read;
        vTaskDelay(pdMS_TO_TICKS(50));
      }
      if (all_good)
      {
        uint8_t result[16] = {};
        mbedtls_md5_finish(&ctx, result);
        mbedtls_md5_free(&ctx);

        string64_t md5_from_builder = {};
        md5_to_string(result, md5_from_builder.data);
        md5_from_builder.length = MD5_LENGTH;

        if (string64_equal(&md5_from_builder, &md5_from_http))
        {
          if (esp_ota_end(ota_handle) == ESP_OK)
          {
            if (esp_ota_set_boot_partition(update_partition) == ESP_OK)
            {
              esp_task_wdt_reset();

              vTaskDelay(pdMS_TO_TICKS(300));

              at_clear();

              at_send_and_wait_for_response_d0("AT+QFCLOSE=1", "OK");
              at_send_and_wait_for_response_d0("AT+IPR=115200", "OK");

              vTaskDelay(pdMS_TO_TICKS(300));
              del_BG_firmware();
              vTaskDelay(pdMS_TO_TICKS(5000));

              esp_restart();
              light_control_add_interrupt();
              return true;
            }
          }
        }
      }
      esp_ota_abort(ota_handle);
    }
    memset(buffer, 0, chunk_size);
  }
  at_send_and_wait_for_response_d0("AT+QFCLOSE=1", "OK");
  del_BG_firmware();

  light_control_add_interrupt();
  return false;
}
