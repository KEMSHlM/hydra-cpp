#include "hydra/logging.h"

#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/log.h"
#include "hydra/logging.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

struct hydra_config {
  hydra::ConfigNode node;
};

namespace {

FILE* log_file_handle = nullptr;
std::string current_log_file_path;
int log_file_callback_index = -1; // Track registered callback index

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
    size_t len = msg.length();
    char* copy = static_cast<char*>(std::malloc(len + 1));
    if (copy != nullptr) {
      std::memcpy(copy, msg.c_str(), len + 1);
    }
    *error_message = copy;
  }
}

} // namespace

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

  // Check if file handler is enabled in hydra.job_logging.root.handlers
  bool enable_file_logging = false;
  try {
    const ConfigNode* handlers_node =
        find_path(config, {"hydra", "job_logging", "root", "handlers"});
    if (handlers_node && handlers_node->is_sequence()) {
      const auto& handlers = handlers_node->as_sequence();
      for (const auto& handler : handlers) {
        if (handler.is_string() && handler.as_string() == "file") {
          enable_file_logging = true;
          break;
        }
      }
    }
  } catch (...) {
    // If handlers config is missing or invalid, disable file logging
    enable_file_logging = false;
  }

  // Setup file logging if enabled
  if (enable_file_logging) {
    try {
      // Read filename from config (hydra.job_logging.handlers.file.filename)
      std::string log_path_str;
      const ConfigNode* filename_node = find_path(
          config, {"hydra", "job_logging", "handlers", "file", "filename"});

      if (filename_node && filename_node->is_string()) {
        log_path_str = filename_node->as_string();
      } else {
        // Default filename if not specified
        const ConfigNode* run_dir_node =
            find_path(config, {"hydra", "run", "dir"});
        const ConfigNode* job_name_node =
            find_path(config, {"hydra", "job", "name"});

        std::string run_dir  = run_dir_node && run_dir_node->is_string()
                                   ? run_dir_node->as_string()
                                   : ".";
        std::string job_name = job_name_node && job_name_node->is_string()
                                   ? job_name_node->as_string()
                                   : "app";

        log_path_str = run_dir + "/" + job_name + ".log";
      }

      if (!log_path_str.empty() && log_path_str != "null") {
        fs::path log_path = log_path_str;

        // Skip if already logging to the same file
        if (log_file_handle != nullptr &&
            current_log_file_path == log_path.string()) {
          // Already logging to this file, nothing to do
          return;
        }

        // Close existing log file if opening a different file
        if (log_file_handle != nullptr) {
          std::fclose(log_file_handle);
          log_file_handle = nullptr;
          current_log_file_path.clear();
        }

        // Open log file for writing
        log_file_handle = std::fopen(log_path.string().c_str(), "w");
        if (log_file_handle != nullptr) {
          current_log_file_path = log_path.string();
          // Add file callback only on first initialization
          // (log.c doesn't provide callback removal, so we reuse the same
          // callback)
          if (log_file_callback_index < 0) {
            log_file_callback_index = log_add_fp(log_file_handle, LOG_TRACE);
          }
        }
      }
    } catch (...) {
      // Silently ignore file logging errors - console logging still works
    }
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

  try {
    // Use C++ API internally
    hydra::init_logging(config->node);
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

extern "C" hydra_status_t
hydra_logging_debug_config(const hydra_config_t* config, char** error_message) {
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
    fs::path log_path = run_path / "app.log"; // Default to app.log

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

    // Add file callback only on first initialization
    // (log.c doesn't provide callback removal, so we reuse the same callback)
    if (log_file_callback_index < 0) {
      log_file_callback_index = log_add_fp(log_file_handle, LOG_TRACE);
    }

    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message,
                 std::string("Failed to setup log file: ") + ex.what());
    return HYDRA_STATUS_ERROR;
  }
}
