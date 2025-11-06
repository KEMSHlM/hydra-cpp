#pragma once

#include "hydra/c_api.h"

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t hydra_config_expect_int(hydra_config_t* config, const char* path);
double hydra_config_expect_double(hydra_config_t* config, const char* path);
char* hydra_config_expect_string(hydra_config_t* config, const char* path);
int hydra_config_expect_bool(hydra_config_t* config, const char* path);

hydra_status_t hydra_config_write_yaml(hydra_config_t* config, const char* path,
                                       char** error_message);

hydra_status_t hydra_config_stream_yaml(hydra_config_t* config, FILE* stream,
                                        char** error_message);

#ifdef __cplusplus
}
#endif
