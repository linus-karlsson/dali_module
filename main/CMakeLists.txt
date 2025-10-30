#set(TARGET_MODULE C3_MINI) 
set(TARGET_MODULE ZHAGA_DALI) 
#set(TARGET_MODULE LSX_S3) 
#set(TARGET_MODULE LSX_S3_LORA) 
 
file(GLOB COMMON_SOURCES
    "${CMAKE_SOURCE_DIR}/main/*.c"
    "${CMAKE_SOURCE_DIR}/main/esp/*.c"
)

if(${TARGET_MODULE} STREQUAL "ZHAGA_DALI")
    file(GLOB MODULE_SOURCES 
        "${CMAKE_SOURCE_DIR}/main/web/*.c"
    )
    #set(MODULE_DEFINE LSX_ZHAGA_DALI LSX_RELEASE)
    set(MODULE_DEFINE LSX_ZHAGA_DALI)
elseif(${TARGET_MODULE} STREQUAL "C3_MINI")
    file(GLOB MODULE_SOURCES 
        "${CMAKE_SOURCE_DIR}/main/switch/*.c"
        "${CMAKE_SOURCE_DIR}/main/web/*.c"
        "${CMAKE_SOURCE_DIR}/main/bg95/*.c"
        "${CMAKE_SOURCE_DIR}/main/option_board/*.c"
    )
    #set(MODULE_DEFINE LSX_C3_MINI LSX_RELEASE)
    set(MODULE_DEFINE LSX_C3_MINI)
elseif(${TARGET_MODULE} STREQUAL "LSX_S3")
    file(GLOB MODULE_SOURCES 
        "${CMAKE_SOURCE_DIR}/main/switch/*.c"
        "${CMAKE_SOURCE_DIR}/main/bg95/*.c"
        "${CMAKE_SOURCE_DIR}/main/switch/s3/*.c"
    )
    #set(MODULE_DEFINE LSX_S3 LSX_RELEASE)
    set(MODULE_DEFINE LSX_S3)
elseif(${TARGET_MODULE} STREQUAL "LSX_S3_LORA")
    file(GLOB MODULE_SOURCES 
        "${CMAKE_SOURCE_DIR}/main/switch/*.c"
        "${CMAKE_SOURCE_DIR}/main/lora/*.c"
        "${CMAKE_SOURCE_DIR}/main/switch/s3/*.c"
        "${CMAKE_SOURCE_DIR}/main/web/*.c"
    )
    #set(MODULE_DEFINE LSX_S3 LSX_LORA LSX_RELEASE)
    set(MODULE_DEFINE LSX_S3 LSX_LORA)
else()
    message(FATAL_ERROR "Unknown TARGET_MODULE: ${TARGET_MODULE}")
endif()

set(SOURCES ${COMMON_SOURCES} ${MODULE_SOURCES})

idf_component_register(SRCS ${SOURCES}
                       PRIV_REQUIRES app_update nvs_flash driver esp_timer mbedtls esp_wifi esp_http_server
                       INCLUDE_DIRS ".")

                   #idf_component_register(SRCS ${SOURCES}
                   #    PRIV_REQUIRES app_update nvs_flash driver esp_timer mbedtls esp_wifi esp_http_server
                   #    INCLUDE_DIRS "." "../components/cJSON")

target_compile_definitions(${COMPONENT_LIB} PRIVATE ${MODULE_DEFINE})

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wno-unused-function
)

