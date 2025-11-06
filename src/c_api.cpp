#include "hydra/c_api.h"

#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/interpolation.hpp"
#include "hydra/overrides.hpp"
#include "hydra/yaml_emitter.hpp"
#include "hydra/yaml_loader.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct hydra_config {
  hydra::ConfigNode node;
};

namespace {

char* dup_string(const std::string& value) {
  char* buffer = static_cast<char*>(std::malloc(value.size() + 1));
  if (buffer == nullptr) {
    return nullptr;
  }
  std::memcpy(buffer, value.data(), value.size());
  buffer[value.size()] = '\0';
  return buffer;
}

void assign_error(char** error_out, const std::string& message) {
  if (error_out != nullptr) {
    *error_out = dup_string(message);
  }
}

void ensure_resolved(hydra_config_t* config) {
  if (config != nullptr) {
    resolve_interpolations(config->node);
  }
}

std::vector<std::string> parse_path(const char* expression) {
  if (expression == nullptr) {
    throw std::runtime_error("Path expression is null");
  }
  return hydra::parse_override_path(expression);
}

const hydra::ConfigNode* locate(const hydra_config_t* config,
                                const char* path_expression) {
  if (config == nullptr) {
    throw std::runtime_error("Config is null");
  }
  std::vector<std::string> path = parse_path(path_expression);
  return hydra::find_path(config->node, path);
}

} // namespace

hydra_config_t* hydra_config_create(void) {
  try {
    hydra_config* cfg = new hydra_config();
    cfg->node         = hydra::make_mapping();
    return cfg;
  } catch (...) {
    return nullptr;
  }
}

void hydra_config_destroy(hydra_config_t* config) {
  delete config;
}

hydra_status_t hydra_config_clear(hydra_config_t* config) {
  if (config == nullptr) {
    return HYDRA_STATUS_ERROR;
  }
  config->node = hydra::make_mapping();
  return HYDRA_STATUS_OK;
}

hydra_status_t hydra_config_merge_file(hydra_config_t* config, const char* path,
                                       char** error_message) {
  if (config == nullptr || path == nullptr) {
    assign_error(error_message, "Config or path is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra::ConfigNode loaded = hydra::load_yaml_file(path);
    hydra::merge(config->node, loaded);
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_merge_string(hydra_config_t* config,
                                         const char* yaml_content,
                                         const char* name,
                                         char** error_message) {
  if (config == nullptr || yaml_content == nullptr) {
    assign_error(error_message, "Config or YAML content is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    std::string source_name =
        name != nullptr ? std::string(name) : std::string("<string>");
    hydra::ConfigNode loaded =
        hydra::load_yaml_string(yaml_content, source_name);
    hydra::merge(config->node, loaded);
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_apply_override(hydra_config_t* config,
                                           const char* expression,
                                           char** error_message) {
  if (config == nullptr || expression == nullptr) {
    assign_error(error_message, "Config or override expression is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra::Override ov = hydra::parse_override(expression);
    hydra::assign_path(config->node, ov.path, std::move(ov.value),
                       ov.require_new);
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

int hydra_config_has(const hydra_config_t* config,
                     const char* path_expression) {
  if (config == nullptr || path_expression == nullptr) {
    return 0;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    return locate(config, path_expression) != nullptr;
  } catch (...) {
    return 0;
  }
}

hydra_status_t hydra_config_apply_cli(hydra_config_t* config, int argc,
                                      char** argv, const char* default_config,
                                      hydra_cli_overrides_t* captured_overrides,
                                      char** error_message) {
  if (config == nullptr) {
    assign_error(error_message, "Config is null");
    return HYDRA_STATUS_ERROR;
  }

  if (error_message != nullptr) {
    *error_message = nullptr;
  }
  if (captured_overrides != nullptr) {
    captured_overrides->items = nullptr;
    captured_overrides->count = 0;
  }

  std::vector<std::string> config_paths;
  std::vector<std::string> overrides;
  config_paths.reserve(static_cast<size_t>(argc));
  overrides.reserve(static_cast<size_t>(argc));

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strncmp(arg, "--config=", 9) == 0) {
      config_paths.emplace_back(arg + 9);
    } else if (std::strcmp(arg, "--config") == 0 ||
               std::strcmp(arg, "-c") == 0) {
      if (i + 1 >= argc) {
        assign_error(error_message, "--config requires an argument");
        return HYDRA_STATUS_ERROR;
      }
      config_paths.emplace_back(argv[++i]);
    } else {
      overrides.emplace_back(arg);
    }
  }

  if (config_paths.empty() && default_config != nullptr) {
    config_paths.emplace_back(default_config);
  }

  for (const auto& path : config_paths) {
    hydra_status_t status =
        hydra_config_merge_file(config, path.c_str(), error_message);
    if (status != HYDRA_STATUS_OK) {
      return status;
    }
  }

  for (const auto& expr : overrides) {
    hydra_status_t status =
        hydra_config_apply_override(config, expr.c_str(), error_message);
    if (status != HYDRA_STATUS_OK) {
      return status;
    }
  }

  if (captured_overrides != nullptr && !overrides.empty()) {
    captured_overrides->items =
        static_cast<char**>(std::malloc(sizeof(char*) * overrides.size()));
    if (captured_overrides->items == nullptr) {
      assign_error(error_message, "Out of memory while capturing overrides");
      return HYDRA_STATUS_ERROR;
    }
    captured_overrides->count = overrides.size();
    for (size_t i = 0; i < overrides.size(); ++i) {
      captured_overrides->items[i] = dup_string(overrides[i]);
      if (captured_overrides->items[i] == nullptr) {
        assign_error(error_message, "Out of memory while capturing overrides");
        for (size_t j = 0; j < i; ++j) {
          hydra_string_free(captured_overrides->items[j]);
        }
        std::free(captured_overrides->items);
        captured_overrides->items = nullptr;
        captured_overrides->count = 0;
        return HYDRA_STATUS_ERROR;
      }
    }
  }

  // Set job name from program name if not already set
  try {
    const hydra::ConfigNode* job_name_node =
        hydra::find_path(config->node, {"hydra", "job", "name"});
    if (!job_name_node || job_name_node->is_null()) {
      std::string job_name = "app";  // default fallback
      if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        // Extract basename from argv[0] (e.g., "./build/hydra-cpp-example" -> "hydra-cpp-example")
        std::filesystem::path prog_path = argv[0];
        job_name = prog_path.filename().string();
      }
      hydra::assign_path(config->node, {"hydra", "job", "name"},
                         hydra::make_string(job_name), false);
    }
  } catch (const std::exception& ex) {
    assign_error(error_message, std::string("Failed to set job name: ") + ex.what());
    return HYDRA_STATUS_ERROR;
  }

  // Resolve interpolations after loading all configs and overrides
  try {
    hydra::resolve_interpolations(config->node);
  } catch (const std::exception& ex) {
    assign_error(error_message, std::string("Failed to resolve interpolations: ") + ex.what());
    return HYDRA_STATUS_ERROR;
  }

  return HYDRA_STATUS_OK;
}

hydra_status_t hydra_config_get_bool(const hydra_config_t* config,
                                     const char* path_expression,
                                     int* out_value, char** error_message) {
  if (config == nullptr || out_value == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    const hydra::ConfigNode* node = locate(config, path_expression);
    if (node == nullptr || !node->is_bool()) {
      assign_error(error_message, "Requested node is not a bool");
      return HYDRA_STATUS_ERROR;
    }
    *out_value = node->as_bool() ? 1 : 0;
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_get_int(const hydra_config_t* config,
                                    const char* path_expression,
                                    int64_t* out_value, char** error_message) {
  if (config == nullptr || out_value == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    const hydra::ConfigNode* node = locate(config, path_expression);
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (node->is_int()) {
      *out_value = node->as_int();
      return HYDRA_STATUS_OK;
    }
    assign_error(error_message, "Requested node is not an integer");
    return HYDRA_STATUS_ERROR;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_get_double(const hydra_config_t* config,
                                       const char* path_expression,
                                       double* out_value,
                                       char** error_message) {
  if (config == nullptr || out_value == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    const hydra::ConfigNode* node = locate(config, path_expression);
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (node->is_double() || node->is_int()) {
      *out_value = node->as_double();
      return HYDRA_STATUS_OK;
    }
    assign_error(error_message, "Requested node is not numeric");
    return HYDRA_STATUS_ERROR;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_get_string(const hydra_config_t* config,
                                       const char* path_expression,
                                       char** out_value, char** error_message) {
  if (config == nullptr || out_value == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    const hydra::ConfigNode* node = locate(config, path_expression);
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (!node->is_string()) {
      assign_error(error_message, "Requested node is not a string");
      return HYDRA_STATUS_ERROR;
    }
    *out_value = dup_string(node->as_string());
    if (*out_value == nullptr) {
      assign_error(error_message, "Out of memory");
      return HYDRA_STATUS_ERROR;
    }
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

char* hydra_config_to_yaml_string(const hydra_config_t* config,
                                  char** error_message) {
  if (config == nullptr) {
    assign_error(error_message, "Config is null");
    return nullptr;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    std::string rendered = hydra::to_yaml_string(config->node);
    return dup_string(rendered);
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return nullptr;
  }
}

void hydra_string_free(char* str) {
  std::free(str);
}
