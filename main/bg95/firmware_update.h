#ifndef LSX_UPDATE_FIRMWARE_H
#define LSX_UPDATE_FIRMWARE_H

#ifdef __cplusplus
extern "C" {
#endif

bool del_BG_firmware(void);
bool update_firmware(const char* product, const char* variant, const char* version, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
