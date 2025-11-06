#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_config hydra_config_t;

typedef enum hydra_status {
  HYDRA_STATUS_OK    = 0,
  HYDRA_STATUS_ERROR = 1
} hydra_status_t;

hydra_config_t* hydra_config_create(void);
void hydra_config_destroy(hydra_config_t* config);

hydra_status_t hydra_config_clear(hydra_config_t* config);

hydra_status_t hydra_config_merge_file(hydra_config_t* config, const char* path,
                                       char** error_message);

hydra_status_t hydra_config_merge_string(hydra_config_t* config,
                                         const char* yaml_content,
                                         const char* name,
                                         char** error_message);

hydra_status_t hydra_config_apply_override(hydra_config_t* config,
                                           const char* expression,
                                           char** error_message);

int hydra_config_has(const hydra_config_t* config, const char* path_expression);

hydra_status_t hydra_config_get_bool(const hydra_config_t* config,
                                     const char* path_expression,
                                     int* out_value, char** error_message);

hydra_status_t hydra_config_get_int(const hydra_config_t* config,
                                    const char* path_expression,
                                    int64_t* out_value, char** error_message);

hydra_status_t hydra_config_get_double(const hydra_config_t* config,
                                       const char* path_expression,
                                       double* out_value, char** error_message);

hydra_status_t hydra_config_get_string(const hydra_config_t* config,
                                       const char* path_expression,
                                       char** out_value, char** error_message);

char* hydra_config_to_yaml_string(const hydra_config_t* config,
                                  char** error_message);

void hydra_string_free(char* str);

#ifdef __cplusplus
}
#endif
