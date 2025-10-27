#include <driver/i2c_master.h>

#include "platform.h"

bool lsx_i2c_master_create(uint8_t port_number, int32_t sda, int32_t scl,
                           lsx_i2c_handle_t* bus_handle)
{
  i2c_master_bus_config_t bus_config = {};
  bus_config.i2c_port = (i2c_port_num_t)port_number;
  bus_config.sda_io_num = sda;
  bus_config.scl_io_num = scl;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&bus_config, (i2c_master_bus_handle_t*)bus_handle) ==
         ESP_OK;
}

bool lsx_i2c_master_destroy(lsx_i2c_handle_t bus_handle)
{
  return i2c_del_master_bus((i2c_master_bus_handle_t)bus_handle) == ESP_OK;
}

bool lsx_i2c_master_add_device(lsx_i2c_handle_t master_handle, uint16_t device_adress,
                               lsx_i2c_handle_t* device_handle)
{
  i2c_device_config_t device_config = {};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = device_adress;
  device_config.scl_speed_hz = I2C_MASTER_FREQ_HZ;
  device_config.scl_wait_us = 2000;
  return i2c_master_bus_add_device((i2c_master_bus_handle_t)master_handle,
                                   &device_config,
                                   (i2c_master_dev_handle_t*)device_handle) == ESP_OK;
}

bool lsx_i2c_master_remove_device(lsx_i2c_handle_t device_handle)
{
  return i2c_master_bus_rm_device((i2c_master_dev_handle_t)device_handle) == ESP_OK;
}

bool lsx_i2c_device_write(lsx_i2c_handle_t device_handle, const uint8_t* data,
                          uint32_t size)
{
  return i2c_master_transmit((i2c_master_dev_handle_t)device_handle, data, size,
                             1000) == ESP_OK;
}

bool lsx_i2c_device_read(lsx_i2c_handle_t device_handle, const uint8_t* write_data,
                         uint32_t write_size, uint8_t* read_data, uint32_t read_size)
{
  return i2c_master_transmit_receive((i2c_master_dev_handle_t)device_handle, write_data,
                                     write_size, read_data, read_size, 1000) == ESP_OK;
}

bool lsx_i2c_device_probe(lsx_i2c_handle_t master_handle, uint16_t device_address,
                          int timeout_ms)
{
  return i2c_master_probe(master_handle, device_address, timeout_ms) == ESP_OK;
}
