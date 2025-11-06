#pragma once

#include "hydra/c_api.h"
#include "hydra/log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Hydra logging system from configuration.
 *
 * Reads logging settings from config (hydra.job_logging.root.level)
 * and configures the underlying log.c library accordingly.
 *
 * If hydra.run.dir is set, also creates a log file at:
 *   ${hydra.run.dir}/.hydra/job.log
 *
 * @param config Configuration object containing logging settings
 * @param error_message Optional pointer to receive error message
 * @return HYDRA_STATUS_OK on success, HYDRA_STATUS_ERROR on failure
 */
hydra_status_t hydra_logging_init(const hydra_config_t* config,
                                  char** error_message);

/**
 * Log configuration as YAML at DEBUG level.
 *
 * Converts the configuration to YAML and logs each line at DEBUG level.
 * Useful for inspecting resolved configuration.
 *
 * @param config Configuration object to log
 * @param error_message Optional pointer to receive error message
 * @return HYDRA_STATUS_OK on success, HYDRA_STATUS_ERROR on failure
 */
hydra_status_t hydra_logging_debug_config(const hydra_config_t* config,
                                          char** error_message);

/**
 * Setup log file output to ${run_dir}/.hydra/job.log
 *
 * Creates the .hydra directory if needed and opens job.log for writing.
 * Logs will be written to both console and file.
 *
 * @param run_dir Run directory path (e.g., "./outputs/2025-11-06_12-34-56")
 * @param error_message Optional pointer to receive error message
 * @return HYDRA_STATUS_OK on success, HYDRA_STATUS_ERROR on failure
 */
hydra_status_t hydra_logging_setup_file(const char* run_dir,
                                        char** error_message);

#ifdef __cplusplus
}
#endif
