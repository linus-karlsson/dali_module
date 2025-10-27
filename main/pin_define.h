#ifndef LSX_PIN_DEFINE_H
#define LSX_PIN_DEFINE_H

#define DALI_PIN_0 3
#define DALI_PIN_1 1
#define DALI_PIN_2 0

#if defined(LSX_ZHAGA_DALI)

#define BG95_PIN    2
#define BG95_RX_PIN 20
#define BG95_TX_PIN 21

#define LED_PIN  7
#define NUM_LEDS 1

#define LED_LTE  0

#define LIGHT_SENSOR_SDA_PIN 8
#define LIGHT_SENSOR_SCL_PIN 10

#elif defined(LSX_C3_MINI)

#define BUZ_PIN 4

#define BG95_PIN    2
#define BG95_RX_PIN 20
#define BG95_TX_PIN 21

#define LED_PIN  7
#define NUM_LEDS 6

#define LIGHT_SENSOR_SDA_PIN 8
#define LIGHT_SENSOR_SCL_PIN 10

#define RELAY_1_PIN 0
#define RELAY_2_PIN 1
#define RELAY_3_PIN 3

#elif defined(LSX_S3)

#define BUZ_PIN 35

#define BG95_PIN    11
#define BG95_RX_PIN 18
#define BG95_TX_PIN 17

#define LED_PIN  48
#define NUM_LEDS 6

#define LIGHT_SENSOR_SDA_PIN 5
#define LIGHT_SENSOR_SCL_PIN 7

#define RTC_SDA_PIN 46
#define RTC_SCL_PIN 45

#define RELAY_1_PIN 36
#define RELAY_2_PIN 37
#define RELAY_3_PIN 38

#endif

#if defined(LSX_C3_MINI) || defined(LSX_S3)

#define LED_MAIN 5
#define LED_LORA 5
#define LED_GPS  4
#define LED_LTE  3
#define LED_R1   2
#define LED_R2   1
#define LED_R3   0

#endif

#if defined(LSX_LORA)

#define GPS_PIN_RESET 16
#define GPS_PIN_RX 18
#define GPS_PIN_TX 17
#endif

#endif
