#ifndef LSX_PIN_DEFINE_H
#define LSX_PIN_DEFINE_H

#if defined (LSX_ZHAGA_DALI) && defined (LSX_S33)
#define DALI_PIN_0 36
#define DALI_PIN_1 37
#define DALI_PIN_2 38

#define LED_PIN  48

#define DALI_TX 5
#define DALI_RX 6

#else
#define DALI_PIN_0 3
#define DALI_PIN_1 1
#define DALI_PIN_2 0

#define LED_PIN  7

#define DALI_TX 5
#define DALI_RX 6

#endif

#define NUM_LEDS 6

#define LED_MAIN  5
#define LED_DALI  4
#define LED_TIMER  3
#define LED_I1  2
#define LED_I2  1
#define LED_I3  0

#endif
