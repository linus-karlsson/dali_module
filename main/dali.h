#ifndef DALI_H
#define DALI_H
#include <stdint.h>

#define DALI_BROADCAST_DP        0b11111110
#define DALI_BROADCAST           0b11111111
#define DALI_ON_DP               0b11111110
#define DALI_OFF_DP              0b00000000
#define DALI_ON                  0x05
#define DALI_OFF                 0x00
#define DALI_RESET               0b00100000
#define DALI_DIMMING_LINEAR      0x01
#define DALI_DIMMING_LOGARITHMIC 0x00

#define DALI_SET_MIN_LEVEL            0x2B
#define DALI_SET_MAX_LEVEL            0x2A
#define DALI_SET_FADE_TIME            0x2E
#define DALI_SET_FADE_RATE            0x2F
#define DALI_SET_SYSTEM_FAILURE_LEVEL 0x2C
#define DALI_SET_POWER_ON_LEVEL       0x2D

#define DALI_UNKNOWN                    0x00
#define DALI_QUERY_STATUS               0x90
#define DALI_QUERY_ACTUAL_OUTPUT        0xA0
#define DALI_QUERY_VERSION_NUMBER       0x97
#define DALI_QUERY_MIN_LEVEL            0xA2
#define DALI_QUERY_MAX_LEVEL            0xA1
#define DALI_QUERY_LIGHT_SOURCE_TYPE    0x9F
#define DALI_QUERY_CONTENT_DTR0         0x98
#define DALI_QUERY_CONTENT_DTR1         0x9C
#define DALI_QUERY_CONTENT_DTR2         0x9D
#define DALI_QUERY_OPERATING_MODE       0x9E
#define DALI_QUERY_POWER_ON_LEVEL       0xA3
#define DALI_QUERY_SYSTEM_FAILURE_LEVEL 0xA4
#define DALI_QUERY_FADE_TIME            0xA5
#define DALI_QUERY_PHYSICAL_MINIMUM     0x9A

#define DALI_EX_REFERENCE_SYSTEM_POWER      0xE0
#define DALI_EX_ENABLE_CURRENT_PROTECTOR    0xE1
#define DALI_EX_DISABLE_CURRENT_PROTECTOR   0xE2
#define DALI_EX_SELECT_DIMMING_CURVE        0xE3
#define DALI_EX_STORE_DTR_AS_FAST_FADE_TIME 0xE4

#define DALI_EX_QUERY_GEAR_TYPE                    0xED
#define DALI_EX_QUERY_DIMMING_CURVE                0xEE
#define DALI_EX_QUERY_POSSIBLE_OPERATING_MODES     0xEF
#define DALI_EX_QUERY_FEATURES                     0xF0
#define DALI_EX_QUERY_FAILURE_STATUS               0xF1
#define DALI_EX_QUERY_SHORT_CIRCUIT                0xF2
#define DALI_EX_QUERY_OPEN_CIRCUIT                 0xF3
#define DALI_EX_QUERY_LOAD_DECREASE                0xF4
#define DALI_EX_QUERY_LOAD_INCREASE                0xF5
#define DALI_EX_QUERY_CURRENT_PROTECTOR_ACTIVE     0xF6
#define DALI_EX_QUERY_THERMAL_SHUT_DOWN            0xF7
#define DALI_EX_QUERY_THERMAL_OVERLOAD             0xF8
#define DALI_EX_QUERY_REFERENCE_RUNNING            0xF9
#define DALI_EX_QUERY_REFERENCE_MEASUREMENT_FAILED 0xFA
#define DALI_EX_QUERY_CURRENT_PROTECTOR_ENABLED    0xFB
#define DALI_EX_QUERY_OPERATING_MODE               0xFC
#define DALI_EX_QUERY_FAST_FADE_TIME               0xFD
#define DALI_EX_QUERY_MIN_FAST_FADE_TIME           0xFE
#define DALI_EX_QUERY_EXTENDED_VERSION_NUMBER      0xFF


typedef struct light_response_t
{
    uint8_t type;
    uint8_t response;
} light_response_t;

void dali_initialize(void);
void light_control_add_interrupt(void);
void light_control_remove_interrupt(void);

#endif
