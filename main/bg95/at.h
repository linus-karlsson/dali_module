#ifndef LSX_AT_H
#define LSX_AT_H
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

void at_initialize(uint32_t rate);
void at_end(void);

bool at_disconnected_flag(void);
void at_reset_disconnected_flag(void);

int32_t at_available(void);
void at_clear(void);

uint32_t at_last_sent_time(void);
uint32_t at_last_receive_time(void);
void at_set_last_sent_time(uint32_t last_time);
void at_set_last_receive_time(uint32_t last_time);

void* at_recv_queue(void);

void at_serial_send(const char* command);
uint32_t at_serial_read_line(char* buffer, uint32_t buffer_size);

#define at_send_and_wait_for_response_d0(command, expected_response) \
    at_send_and_wait_for_response(command, expected_response, 40000, false, NULL, true, false)
#define at_send_and_wait_for_response_d1(command, expected_response, timeout) \
    at_send_and_wait_for_response(command, expected_response, timeout, false, NULL, true, false)
#define at_send_and_wait_for_response_d2(command, expected_response, timeout, record) \
    at_send_and_wait_for_response(command, expected_response, timeout, record, NULL, true, false)
#define at_send_and_wait_for_response_d3(command, expected_response, timeout, record, response_out) \
    at_send_and_wait_for_response(command, expected_response, timeout, record, response_out, true, false)
#define at_send_and_wait_for_response_d4(command, expected_response, timeout, record, response_out, check_for_ok) \
    at_send_and_wait_for_response(command, expected_response, timeout, record, response_out, check_for_ok, false)

#define at_send_and_wait_for_response_d5(command, expected_response, timeout, response_out, check_for_ok, leave_if_disconnected) \
    at_send_and_wait_for_response(command, expected_response, timeout, false, response_out, check_for_ok, leave_if_disconnected)
#define at_send_and_wait_for_response_d6(command, expected_response, timeout, response_out) \
    at_send_and_wait_for_response(command, expected_response, timeout, false, response_out, true, false)
#define at_send_and_wait_for_response_d7(command, expected_response, timeout, response_out) \
    at_send_and_wait_for_response(command, expected_response, timeout, true, response_out, true, false)

bool at_send_and_wait_for_response(const char* command, const char* expected_response, uint32_t timeout_ms, bool record_recv, string2048_t* response_out, bool check_for_ok, bool leave_if_disconnected);

bool at_send_and_wait_for_response_no_log(const char* command,
                                          const char* expected_response,
                                          uint32_t timeout_ms, bool record_recv,
                                          string2048_t* response_out, bool check_for_ok,
                                          bool leave_if_disconnected);

uint32_t at_read_file(uint32_t file_handle, uint8_t* buffer, uint32_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
