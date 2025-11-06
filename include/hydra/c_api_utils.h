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

/**
 * Initialize Hydra configuration from command-line arguments (simplified API).
 * Combines: config creation, CLI parsing, override application, job.name
 * derivation, and interpolation resolution.
 *
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @param default_config Default config file path (e.g., "configs/main.yaml")
 * @param error_message Output parameter for error message (if any)
 * @return Config object on success, NULL on failure
 */
hydra_config_t* hydra_initialize(int argc, char** argv,
                                 const char* default_config,
                                 char** error_message);

#ifdef __cplusplus
}
#endif
