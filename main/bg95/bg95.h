#ifndef LSX_BG95_H
#define LSX_BG95_H

#ifdef __cplusplus
extern "C" {
#endif

void bg95_initialize(void* incoming_queue, void* outgoing_queue, char* uid);
bool bg95_mqtt_is_connected(void);
bool bg95_gnss_found(void);

#ifdef __cplusplus
}
#endif

#endif
