#ifndef LSX_WEB_H 
#define LSX_WEB_H 

#ifdef __cplusplus
extern "C" {
#endif


/**
 * Initiate and start Wifi task.
 *
 * @param inQueue Queue for messages to task.
 * @param outQueue Queue for messages from task.
 * @param uid 64-bit Uid. Identifies switch on networks.
 */
void web_initialize(char *uid);

#ifdef __cplusplus
}
#endif

#endif
