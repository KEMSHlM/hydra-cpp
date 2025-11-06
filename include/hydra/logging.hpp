#pragma once

#include "hydra/config_node.hpp"
#include "hydra/log.h"

namespace hydra {

/**
 * Initialize logging from ConfigNode.
 * Reads hydra.job_logging.root.level and configures log level.
 *
 * @param config Configuration node containing logging settings
 */
void init_logging(const ConfigNode& config);

/**
 * Log configuration as YAML at DEBUG level.
 * Useful for inspecting resolved configuration.
 *
 * @param config Configuration node to log
 */
void log_config(const ConfigNode& config);

/**
 * Setup log file output to ${run_dir}/.hydra/job.log
 *
 * @param run_dir Run directory path
 */
void setup_log_file(const std::string& run_dir);

} // namespace hydra
