#include "hydra/c_api_utils.h"

#include "hydra/config_utils.hpp"
#include "hydra/logging.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

struct hydra_config {
  hydra::ConfigNode node;
};

namespace {

char* duplicate_string(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  size_t len = std::strlen(text);
  char* copy = static_cast<char*>(std::malloc(len + 1));
  if (copy == nullptr) {
    return nullptr;
  }
  std::memcpy(copy, text, len + 1);
  return copy;
}

void set_error(char** error_out, const std::string& message) {
  if (error_out != nullptr) {
    *error_out = duplicate_string(message.c_str());
  }
}

[[noreturn]] void handle_expect_failure(const char* path, const char* expected,
                                        char* err) {
  std::fprintf(stderr, "[hydra] expected %s at '%s': %s\n", expected, path,
               err ? err : "(unknown error)");
  hydra_string_free(err);
  std::exit(EXIT_FAILURE);
}

} // namespace

extern "C" {

int64_t hydra_config_expect_int(hydra_config_t* config, const char* path) {
  char* err             = nullptr;
  int64_t value         = 0;
  hydra_status_t status = hydra_config_get_int(config, path, &value, &err);
  if (status != HYDRA_STATUS_OK) {
    handle_expect_failure(path, "an integer", err);
  }
  return value;
}

double hydra_config_expect_double(hydra_config_t* config, const char* path) {
  char* err             = nullptr;
  double value          = 0.0;
  hydra_status_t status = hydra_config_get_double(config, path, &value, &err);
  if (status != HYDRA_STATUS_OK) {
    handle_expect_failure(path, "a double", err);
  }
  return value;
}

char* hydra_config_expect_string(hydra_config_t* config, const char* path) {
  char* err             = nullptr;
  char* value           = nullptr;
  hydra_status_t status = hydra_config_get_string(config, path, &value, &err);
  if (status != HYDRA_STATUS_OK) {
    handle_expect_failure(path, "a string", err);
  }
  return value;
}

int hydra_config_expect_bool(hydra_config_t* config, const char* path) {
  char* err             = nullptr;
  int value             = 0;
  hydra_status_t status = hydra_config_get_bool(config, path, &value, &err);
  if (status != HYDRA_STATUS_OK) {
    handle_expect_failure(path, "a boolean", err);
  }
  return value;
}

hydra_status_t hydra_config_write_yaml(hydra_config_t* config, const char* path,
                                       char** error_message) {
  if (config == nullptr || path == nullptr) {
    set_error(error_message, "Config or path is null");
    return HYDRA_STATUS_ERROR;
  }
  char* yaml = hydra_config_to_yaml_string(config, error_message);
  if (yaml == nullptr) {
    return HYDRA_STATUS_ERROR;
  }

  FILE* out = std::fopen(path, "wb");
  if (!out) {
    set_error(error_message, "Failed to open output file");
    hydra_string_free(yaml);
    return HYDRA_STATUS_ERROR;
  }
  size_t len     = std::strlen(yaml);
  size_t written = std::fwrite(yaml, 1, len, out);
  std::fclose(out);
  hydra_string_free(yaml);
  if (written != len) {
    set_error(error_message, "Failed to write full YAML output");
    return HYDRA_STATUS_ERROR;
  }
  return HYDRA_STATUS_OK;
}

hydra_status_t hydra_config_stream_yaml(hydra_config_t* config, FILE* stream,
                                        char** error_message) {
  if (config == nullptr || stream == nullptr) {
    set_error(error_message, "Config or stream is null");
    return HYDRA_STATUS_ERROR;
  }
  char* yaml = hydra_config_to_yaml_string(config, error_message);
  if (yaml == nullptr) {
    return HYDRA_STATUS_ERROR;
  }
  std::fputs(yaml, stream);
  if (yaml[0] && yaml[std::strlen(yaml) - 1] != '\n') {
    std::fputc('\n', stream);
  }
  hydra_string_free(yaml);
  return HYDRA_STATUS_OK;
}

void hydra_cli_overrides_free(hydra_cli_overrides_t* overrides) {
  if (overrides == nullptr || overrides->items == nullptr) {
    return;
  }
  for (size_t i = 0; i < overrides->count; ++i) {
    hydra_string_free(overrides->items[i]);
  }
  std::free(overrides->items);
  overrides->items = nullptr;
  overrides->count = 0;
}

hydra_status_t hydra_config_finalize_run(hydra_config_t* config,
                                         const char* const* overrides,
                                         size_t override_count,
                                         char** run_dir_out,
                                         char** error_message) {
  if (run_dir_out) {
    *run_dir_out = nullptr;
  }
  if (config == nullptr) {
    set_error(error_message, "Config is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    std::vector<std::string> override_vec;
    override_vec.reserve(override_count);
    for (size_t i = 0; i < override_count; ++i) {
      override_vec.emplace_back(overrides[i] ? overrides[i] : "");
    }
    std::filesystem::path run_dir =
        hydra::utils::write_hydra_outputs(config->node, override_vec);
    if (run_dir_out) {
      *run_dir_out = duplicate_string(run_dir.string().c_str());
    }

    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    set_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

} // extern "C"
