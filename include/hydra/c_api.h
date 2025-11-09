#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hydra_config hydra_config_t;
typedef struct hydra_config_iter hydra_config_iter_t;

typedef enum hydra_status {
  HYDRA_STATUS_OK    = 0,
  HYDRA_STATUS_ERROR = 1
} hydra_status_t;

typedef struct hydra_cli_overrides {
  char** items;
  size_t count;
} hydra_cli_overrides_t;

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

hydra_status_t hydra_config_subnode(hydra_config_t* config,
                                    const char* path_expression,
                                    hydra_config_t** out_subconfig,
                                    char** error_message);

hydra_status_t hydra_config_sequence_iter(const hydra_config_t* config,
                                          const char* path_expression,
                                          hydra_config_iter_t** out_iter,
                                          char** error_message);

hydra_status_t hydra_config_map_iter(const hydra_config_t* config,
                                     const char* path_expression,
                                     hydra_config_iter_t** out_iter,
                                     char** error_message);

int hydra_config_iter_next(hydra_config_iter_t* iter, char** child_path,
                           char** key, size_t* index, char** error_message);

void hydra_config_iter_destroy(hydra_config_iter_t* iter);

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

hydra_status_t hydra_config_clone_string(const hydra_config_t* config,
                                         const char* path_expression,
                                         char** out_value,
                                         char** error_message);

hydra_status_t hydra_config_clone_string_list(const hydra_config_t* config,
                                              const char* path_expression,
                                              char*** out_items,
                                              size_t* out_count,
                                              char** error_message);

void hydra_string_list_free(char** items, size_t count);

hydra_status_t hydra_config_ensure_directory(const hydra_config_t* config,
                                             const char* path_expression,
                                             char** error_message);

char* hydra_config_to_yaml_string(const hydra_config_t* config,
                                  char** error_message);

void hydra_string_free(char* str);

void hydra_cli_overrides_free(hydra_cli_overrides_t* overrides);

hydra_status_t hydra_config_apply_cli(hydra_config_t* config, int argc,
                                      char** argv, const char* default_config,
                                      hydra_cli_overrides_t* captured_overrides,
                                      char** error_message);

/**
 * Write Hydra outputs (.hydra directory with configs).
 * Creates ${hydra.run.dir}/.hydra and writes configuration files.
 *
 * @param config Configuration object
 * @param overrides Array of override strings to save
 * @param override_count Number of overrides
 * @param run_dir_out Output parameter for run directory path
 * @param error_message Output parameter for error message
 * @return HYDRA_STATUS_OK on success
 */
hydra_status_t hydra_write_outputs(hydra_config_t* config,
                                   const char* const* overrides,
                                   size_t override_count, char** run_dir_out,
                                   char** error_message);

#ifdef __cplusplus
}
#endif
