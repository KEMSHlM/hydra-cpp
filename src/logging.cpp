#include "hydra/logging.h"
#include "hydra/logging.hpp"

#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/log.h"

#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

FILE* log_file_handle = nullptr;

int parse_log_level(const char* level_str) {
  if (level_str == nullptr) {
    return LOG_INFO;
  }

  std::string level = level_str;
  // Convert to uppercase for comparison
  for (char& c : level) {
    if (c >= 'a' && c <= 'z') {
      c = c - 'a' + 'A';
    }
  }

  if (level == "TRACE") {
    return LOG_TRACE;
  }
  if (level == "DEBUG") {
    return LOG_DEBUG;
  }
  if (level == "INFO") {
    return LOG_INFO;
  }
  if (level == "WARN" || level == "WARNING") {
    return LOG_WARN;
  }
  if (level == "ERROR") {
    return LOG_ERROR;
  }
  if (level == "FATAL") {
    return LOG_FATAL;
  }

  // Default to INFO if unknown
  return LOG_INFO;
}

void assign_error(char** error_message, const std::string& msg) {
  if (error_message != nullptr) {
    *error_message = strdup(msg.c_str());
  }
}

}  // namespace

// C++ API
void hydra::init_logging(const ConfigNode& config) {
  std::string level_str = "INFO";

  try {
    // Try to read log level from config
    const ConfigNode* level_node =
        find_path(config, {"hydra", "job_logging", "root", "level"});
    if (level_node && level_node->is_string()) {
      level_str = level_node->as_string();
    }
  } catch (...) {
    // Use default INFO if any error occurs
    level_str = "INFO";
  }

  int log_level = parse_log_level(level_str.c_str());
  log_set_level(log_level);

  // Auto-setup file logging if run.dir exists
  try {
    const ConfigNode* run_dir_node = find_path(config, {"hydra", "run", "dir"});
    if (run_dir_node && run_dir_node->is_string()) {
      std::string run_dir = run_dir_node->as_string();
      if (run_dir != "null" && !run_dir.empty()) {
        // Get job name
        std::string job_name = "app";  // default
        const ConfigNode* job_name_node =
            find_path(config, {"hydra", "job", "name"});
        if (job_name_node && job_name_node->is_string()) {
          job_name = job_name_node->as_string();
        }

        std::string log_filename = job_name + ".log";
        fs::path run_path         = run_dir;
        fs::path log_path         = run_path / log_filename;

        // Close existing log file if any
        if (log_file_handle != nullptr) {
          std::fclose(log_file_handle);
          log_file_handle = nullptr;
        }

        // Open log file for writing
        log_file_handle = std::fopen(log_path.string().c_str(), "w");
        if (log_file_handle != nullptr) {
          // Add file to log.c callbacks
          log_add_fp(log_file_handle, LOG_TRACE);
        }
      }
    }
  } catch (...) {
    // Silently ignore file logging errors - console logging still works
  }
}

void hydra::log_config(const ConfigNode& config) {
  // Use C API implementation
  // First we need to convert ConfigNode to hydra_config_t
  // For now, use YAML string approach
  std::ostringstream yaml_ss;
  hydra::utils::write_yaml(yaml_ss, config);
  std::string yaml_str = yaml_ss.str();

  log_debug("--- resolved config ---");
  std::istringstream yaml_stream(yaml_str);
  std::string line;
  while (std::getline(yaml_stream, line)) {
    if (!line.empty()) {
      log_debug("%s", line.c_str());
    }
  }
}

void hydra::setup_log_file(const std::string& run_dir) {
  char* err = nullptr;
  hydra_logging_setup_file(run_dir.c_str(), &err);
  if (err != nullptr) {
    // Silently ignore file logging errors
    hydra_string_free(err);
  }
}

// C API
extern "C" hydra_status_t hydra_logging_init(const hydra_config_t* config,
                                             char** error_message) {
  if (config == nullptr) {
    assign_error(error_message, "Config is null");
    return HYDRA_STATUS_ERROR;
  }

  if (error_message != nullptr) {
    *error_message = nullptr;
  }

  char* err = nullptr;

  // Read log level from config (hydra.job_logging.root.level)
  char* level_str   = nullptr;
  hydra_status_t status =
      hydra_config_get_string(config, "hydra.job_logging.root.level",
                              &level_str, &err);

  int log_level = LOG_INFO;  // default
  if (status == HYDRA_STATUS_OK && level_str != nullptr) {
    log_level = parse_log_level(level_str);
    hydra_string_free(level_str);
  }
  hydra_string_free(err);
  err = nullptr;

  // Set log level
  log_set_level(log_level);

  // Check if file handler is enabled (hydra.job_logging.handlers)
  // Default behavior: file logging is enabled if run_dir exists
  bool enable_file_logging = true;

  // Try to read handlers list
  if (hydra_config_has(config, "hydra.job_logging.handlers")) {
    // For now, we assume file logging is enabled by default
    // TODO: Parse the handlers list properly
    enable_file_logging = true;
  }

  // Auto-setup file logging if enabled
  if (enable_file_logging) {
    char* run_dir = nullptr;
    status = hydra_config_get_string(config, "hydra.run.dir", &run_dir, &err);

    if (status == HYDRA_STATUS_OK && run_dir != nullptr &&
        std::strcmp(run_dir, "null") != 0) {
      char* job_name = nullptr;
      hydra_status_t job_status =
          hydra_config_get_string(config, "hydra.job.name", &job_name, &err);

      std::string log_filename = "app.log";  // default
      if (job_status == HYDRA_STATUS_OK && job_name != nullptr) {
        log_filename = std::string(job_name) + ".log";
        hydra_string_free(job_name);
      }
      hydra_string_free(err);
      err = nullptr;

      try {
        fs::path run_path = run_dir;
        fs::path log_path = run_path / log_filename;

        // Close existing log file if any
        if (log_file_handle != nullptr) {
          std::fclose(log_file_handle);
          log_file_handle = nullptr;
        }

        // Open log file for writing
        log_file_handle = std::fopen(log_path.string().c_str(), "w");
        if (log_file_handle != nullptr) {
          // Add file to log.c callbacks
          log_add_fp(log_file_handle, LOG_TRACE);
        }
      } catch (const std::exception& ex) {
        // Silently ignore file logging errors - console logging still works
      }
    }

    hydra_string_free(run_dir);
    hydra_string_free(err);
  }

  return HYDRA_STATUS_OK;
}

extern "C" hydra_status_t hydra_logging_debug_config(
    const hydra_config_t* config, char** error_message) {
  if (config == nullptr) {
    assign_error(error_message, "Config is null");
    return HYDRA_STATUS_ERROR;
  }

  if (error_message != nullptr) {
    *error_message = nullptr;
  }

  char* yaml_str = hydra_config_to_yaml_string(config, error_message);
  if (yaml_str == nullptr) {
    return HYDRA_STATUS_ERROR;
  }

  log_debug("--- resolved config ---");

  // Log each line
  char* line      = yaml_str;
  char* next_line = nullptr;
  while ((next_line = std::strchr(line, '\n')) != nullptr) {
    *next_line = '\0';
    if (std::strlen(line) > 0) {
      log_debug("%s", line);
    }
    line = next_line + 1;
  }
  // Log last line if no trailing newline
  if (std::strlen(line) > 0) {
    log_debug("%s", line);
  }

  hydra_string_free(yaml_str);
  return HYDRA_STATUS_OK;
}

extern "C" hydra_status_t hydra_logging_setup_file(const char* run_dir,
                                                    char** error_message) {
  if (run_dir == nullptr) {
    assign_error(error_message, "Run directory is null");
    return HYDRA_STATUS_ERROR;
  }

  if (error_message != nullptr) {
    *error_message = nullptr;
  }

  try {
    fs::path run_path = run_dir;
    fs::path log_path = run_path / "app.log";  // Default to app.log

    // Close existing log file if any
    if (log_file_handle != nullptr) {
      std::fclose(log_file_handle);
      log_file_handle = nullptr;
    }

    // Open log file for writing
    log_file_handle = std::fopen(log_path.string().c_str(), "w");
    if (log_file_handle == nullptr) {
      assign_error(error_message,
                   "Failed to open log file: " + log_path.string());
      return HYDRA_STATUS_ERROR;
    }

    // Add file to log.c callbacks
    log_add_fp(log_file_handle, LOG_TRACE);

    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, std::string("Failed to setup log file: ") +
                                    ex.what());
    return HYDRA_STATUS_ERROR;
  }
}
