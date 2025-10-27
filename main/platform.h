#ifndef LSX_PLATFORM_H
#define LSX_PLATFORM_H
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern c {
#endif

#define I2C_MASTER_FREQ_HZ 100000

typedef void* lsx_i2c_handle_t;
typedef void* lsx_timer_handle_t;

typedef enum lsx_gpio_level_t
{
    LSX_GPIO_LOW = 0,
    LSX_GPIO_HIGH = 1,
} lsx_gpio_level_t;

typedef enum lsx_gpio_mode_t
{
    LSX_GPIO_MODE_INPUT = 1,
    LSX_GPIO_MODE_OUTPUT = 2,
} lsx_gpio_mode_t;

typedef enum lsx_gpio_interrupt_t
{
    LSX_GPIO_INTR_DISABLE = 0,
    LSX_GPIO_INTR_POSEDGE = 1,
    LSX_GPIO_INTR_NEGEDGE = 2,
    LSX_GPIO_INTR_ANYEDGE = 3,
    LSX_GPIO_INTR_LOW_LEVEL = 4,
    LSX_GPIO_INTR_HIGH_LEVEL = 5,
} lsx_gpio_interrupt_t;

typedef struct nvs_t
{
    uint32_t handle;
} nvs_t;

typedef struct time_info_t
{
    uint16_t second;
    uint16_t minute;
    uint16_t hour;
    uint16_t month_day;
    uint16_t month;
    uint16_t year;
    uint16_t week_day;
    uint16_t year_day;
    uint16_t day_light_saving;
} time_info_t;

void    lsx_gpio_config(uint8_t gpio_number, lsx_gpio_mode_t mode, lsx_gpio_interrupt_t interrupt_type, bool pullup, bool pulldown);
void    lsx_gpio_write(uint8_t gpio_number, lsx_gpio_level_t level);
uint8_t lsx_gpio_read(uint8_t gpio_number);

void lsx_gpio_install_interrupt_service(void);
void lsx_gpio_add_pin_interrput(uint8_t gpio_number, void (*interrupt_callback)(void*), void* arguments);
void lsx_gpio_remove_pin_interrput(uint8_t gpio_number);

bool lsx_i2c_master_create(uint8_t port_number, int32_t sda, int32_t scl, lsx_i2c_handle_t* master_handle);
bool lsx_i2c_master_destroy(lsx_i2c_handle_t master_handle);
bool lsx_i2c_master_add_device(lsx_i2c_handle_t master_handle, uint16_t device_adress, lsx_i2c_handle_t* device_handle);
bool lsx_i2c_master_remove_device(lsx_i2c_handle_t device_handle);
bool lsx_i2c_device_write(lsx_i2c_handle_t device_handle, const uint8_t* data, uint32_t size);
bool lsx_i2c_device_read(lsx_i2c_handle_t device_handle, const uint8_t* write_data, uint32_t write_size, uint8_t* read_data, uint32_t read_size);
bool lsx_i2c_device_probe(lsx_i2c_handle_t master_handle, uint16_t device_address, int timeout_ms);

void     lsx_uart_initialize(int32_t uart_number, int32_t baud_rate, int32_t tx_pin, int32_t rx_pin, int32_t tx_buffer_size, int32_t rx_buffer_size);
void     lsx_uart_set_baudrate(int32_t uart_number, uint32_t new_baudrate);
uint32_t lsx_uart_get_baudrate(int32_t uart_number);
void     lsx_uart_write(int32_t uart_number, const void* bytes, uint32_t size);
int32_t  lsx_uart_available(int32_t uart_number);
int32_t lsx_uart_read(int32_t uart_number, uint8_t* buffer, uint32_t size, uint32_t timeout_ms);
uint32_t lsx_uart_read_until_string(int32_t uart_number, char* buffer, uint32_t buffer_max_size, char character, uint32_t timeout_ms, bool* timedout);
uint32_t lsx_uart_read_until(int32_t uart_number, uint8_t* buffer, uint32_t buffer_max_size, char character, uint32_t timeout_ms, bool* timedout);
void     lsx_uart_clear(int32_t uart_number);

void lsx_nvs_initialize(void);
void lsx_nvs_open(nvs_t* nvs, const char* name);
void lsx_nvs_close(nvs_t* nvs);
void lsx_nvs_clear(nvs_t* nvs);
void lsx_nvs_remove_key(nvs_t* nvs, const char* key);
void lsx_nvs_remove(nvs_t* nvs, const char* key);
void lsx_nvs_commit(nvs_t* nvs);
void lsx_nvs_set_int8(nvs_t* nvs, const char* key, int8_t value);
void lsx_nvs_set_int16(nvs_t* nvs, const char* key, int16_t value);
void lsx_nvs_set_int32(nvs_t* nvs, const char* key, int32_t value);
void lsx_nvs_set_uint8(nvs_t* nvs, const char* key, uint8_t value);
void lsx_nvs_set_uint16(nvs_t* nvs, const char* key, uint16_t value);
void lsx_nvs_set_uint32(nvs_t* nvs, const char* key, uint32_t value);
void lsx_nvs_set_float(nvs_t* nvs, const char* key, float value);
void lsx_nvs_set_string(nvs_t* nvs, const char* key, const char* value);
void lsx_nvs_set_bytes(nvs_t* nvs, const char* key, const void* value, uint32_t size);
bool lsx_nvs_set_int8_ram(nvs_t* nvs, const char* key, int8_t value);
bool lsx_nvs_set_int16_ram(nvs_t* nvs, const char* key, int16_t value);
bool lsx_nvs_set_int32_ram(nvs_t* nvs, const char* key, int32_t value);
bool lsx_nvs_set_uint8_ram(nvs_t* nvs, const char* key, uint8_t value);
bool lsx_nvs_set_uint16_ram(nvs_t* nvs, const char* key, uint16_t value);
bool lsx_nvs_set_uint32_ram(nvs_t* nvs, const char* key, uint32_t value);
bool lsx_nvs_set_float_ram(nvs_t* nvs, const char* key, float value);
bool lsx_nvs_set_string_ram(nvs_t* nvs, const char* key, const char* value);
bool lsx_nvs_set_bytes_ram(nvs_t* nvs, const char* key, const void* value, uint32_t size);
bool lsx_nvs_get_int8(nvs_t* nvs, const char* key, int8_t* value, int8_t default_value);
bool lsx_nvs_get_int16(nvs_t* nvs, const char* key, int16_t* value, int16_t default_value);
bool lsx_nvs_get_int32(nvs_t* nvs, const char* key, int32_t* value, int32_t default_value);
bool lsx_nvs_get_uint8(nvs_t* nvs, const char* key, uint8_t* value, uint8_t default_value);
bool lsx_nvs_get_uint16(nvs_t* nvs, const char* key, uint16_t* value, uint16_t default_value);
bool lsx_nvs_get_uint32(nvs_t* nvs, const char* key, uint32_t* value, uint32_t default_value);
bool lsx_nvs_get_float(nvs_t* nvs, const char* key, float* value, float default_value);
bool lsx_nvs_get_string(nvs_t* nvs, const char* key, char* value, uint32_t* value_length, uint32_t max_length);
bool lsx_nvs_get_bytes(nvs_t* nvs, const char* key, void* value, uint32_t* value_size, uint32_t max_size);
void lsx_nvs_erase_all(nvs_t* nvs);

uint32_t lsx_get_time(void);
void     lsx_get_time_info(uint32_t time_sec, time_info_t* time_info);
void     lsx_get_time_info_default(time_info_t* time_info);
uint32_t lsx_make_time(time_info_t* time_info);
void     lsx_set_time(uint32_t time_sec);
void     lsx_set_time_info(time_info_t* time_info);

uint32_t lsx_get_millis(void);
uint32_t lsx_get_micro(void);
void     lsx_delay_millis(int64_t ms);
void     lsx_delay_micro(int64_t us);

lsx_timer_handle_t lsx_timer_create(void (*timer_callback)(void*), void* args);
void               lsx_timer_destroy(lsx_timer_handle_t handle);
bool               lsx_timer_start(lsx_timer_handle_t handle, uint64_t when, bool repeated);


#ifdef __cplusplus
}
#endif
#endif
