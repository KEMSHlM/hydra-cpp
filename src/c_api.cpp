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

struct hydra_config_iter {
  enum class Kind { Sequence, Mapping } kind;
  std::string base_path;
  const hydra::ConfigNode::seq_t* sequence = nullptr;
  const hydra::ConfigNode::map_t* mapping  = nullptr;
  size_t index                             = 0;
  hydra::ConfigNode::map_t::const_iterator map_it;
  hydra::ConfigNode::map_t::const_iterator map_end;
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

std::string escape_path_segment(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char ch : value) {
    if (ch == '.' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string build_path_expression(const std::vector<std::string>& components) {
  if (components.empty()) {
    return "";
  }
  std::string rendered;
  bool first = true;
  for (const auto& component : components) {
    if (!first) {
      rendered.push_back('.');
    }
    rendered += escape_path_segment(component);
    first = false;
  }
  return rendered;
}

std::string append_segment(const std::string& base,
                           const std::string& component) {
  std::string escaped = escape_path_segment(component);
  if (base.empty()) {
    return escaped;
  }
  if (escaped.empty()) {
    return base;
  }
  std::string combined = base;
  if (!base.empty()) {
    combined.push_back('.');
  }
  combined += escaped;
  return combined;
}

const hydra::ConfigNode*
locate_with_rendered(const hydra_config_t* config, const char* path_expression,
                     std::string& rendered_expression) {
  if (config == nullptr) {
    throw std::runtime_error("Config is null");
  }
  if (path_expression == nullptr || path_expression[0] == '\0') {
    rendered_expression.clear();
    return &config->node;
  }
  std::vector<std::string> path = parse_path(path_expression);
  rendered_expression           = build_path_expression(path);
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

hydra_status_t hydra_config_subnode(hydra_config_t* config,
                                    const char* path_expression,
                                    hydra_config_t** out_subconfig,
                                    char** error_message) {
  if (out_subconfig != nullptr) {
    *out_subconfig = nullptr;
  }
  if (config == nullptr || out_subconfig == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    ensure_resolved(config);
    const hydra::ConfigNode* source = nullptr;
    if (path_expression == nullptr || path_expression[0] == '\0') {
      source = &config->node;
    } else {
      source = locate(config, path_expression);
    }
    if (source == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }

    hydra_config_t* child = hydra_config_create();
    if (child == nullptr) {
      assign_error(error_message, "Failed to allocate config");
      return HYDRA_STATUS_ERROR;
    }
    child->node    = hydra::deep_copy(*source);
    *out_subconfig = child;
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

hydra_status_t hydra_config_sequence_iter(const hydra_config_t* config,
                                          const char* path_expression,
                                          hydra_config_iter_t** out_iter,
                                          char** error_message) {
  if (out_iter != nullptr) {
    *out_iter = nullptr;
  }
  if (config == nullptr || out_iter == nullptr) {
    assign_error(error_message, "Config or iterator output is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    std::string rendered_path;
    const hydra::ConfigNode* node =
        locate_with_rendered(config, path_expression, rendered_path);
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (!node->is_sequence()) {
      assign_error(error_message, "Requested node is not a sequence");
      return HYDRA_STATUS_ERROR;
    }
    hydra_config_iter* iter = new hydra_config_iter();
    iter->kind              = hydra_config_iter::Kind::Sequence;
    iter->sequence          = &node->as_sequence();
    iter->base_path         = rendered_path;
    iter->index             = 0;
    *out_iter               = iter;
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

hydra_status_t hydra_config_map_iter(const hydra_config_t* config,
                                     const char* path_expression,
                                     hydra_config_iter_t** out_iter,
                                     char** error_message) {
  if (out_iter != nullptr) {
    *out_iter = nullptr;
  }
  if (config == nullptr || out_iter == nullptr) {
    assign_error(error_message, "Config or iterator output is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    std::string rendered_path;
    const hydra::ConfigNode* node =
        locate_with_rendered(config, path_expression, rendered_path);
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (!node->is_mapping()) {
      assign_error(error_message, "Requested node is not a mapping");
      return HYDRA_STATUS_ERROR;
    }
    hydra_config_iter* iter = new hydra_config_iter();
    iter->kind              = hydra_config_iter::Kind::Mapping;
    iter->mapping           = &node->as_mapping();
    iter->map_it            = iter->mapping->cbegin();
    iter->map_end           = iter->mapping->cend();
    iter->base_path         = rendered_path;
    iter->index             = 0;
    *out_iter               = iter;
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

int hydra_config_iter_next(hydra_config_iter_t* iter, char** child_path,
                           char** key, size_t* index, char** error_message) {
  if (child_path != nullptr) {
    *child_path = nullptr;
  }
  if (key != nullptr) {
    *key = nullptr;
  }
  if (index != nullptr) {
    *index = 0;
  }
  if (error_message != nullptr) {
    *error_message = nullptr;
  }
  if (iter == nullptr) {
    assign_error(error_message, "Iterator is null");
    return -1;
  }

  auto assign_string = [&](char** target, const std::string& value) -> bool {
    if (target == nullptr) {
      return true;
    }
    char* copy = dup_string(value);
    if (copy == nullptr) {
      assign_error(error_message, "Out of memory");
      return false;
    }
    *target = copy;
    return true;
  };

  if (iter->kind == hydra_config_iter::Kind::Sequence) {
    if (iter->sequence == nullptr || iter->index >= iter->sequence->size()) {
      return 0;
    }
    size_t current_index = iter->index++;
    if (index != nullptr) {
      *index = current_index;
    }
    std::string path_segment  = std::to_string(current_index);
    std::string rendered_path = append_segment(iter->base_path, path_segment);
    if (!assign_string(child_path, rendered_path)) {
      return -1;
    }
    return 1;
  }

  if (iter->map_it == iter->map_end) {
    return 0;
  }
  const std::string& entry_key = iter->map_it->first;
  if (index != nullptr) {
    *index = iter->index;
  }
  ++iter->map_it;
  ++iter->index;
  std::string rendered_path = append_segment(iter->base_path, entry_key);
  if (!assign_string(child_path, rendered_path)) {
    return -1;
  }
  if (!assign_string(key, entry_key)) {
    if (child_path != nullptr) {
      hydra_string_free(*child_path);
      *child_path = nullptr;
    }
    return -1;
  }
  return 1;
}

void hydra_config_iter_destroy(hydra_config_iter_t* iter) {
  delete iter;
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
      std::string job_name = "app"; // default fallback
      if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
        // Extract basename from argv[0] (e.g., "./build/hydra-cpp-example" ->
        // "hydra-cpp-example")
        std::filesystem::path prog_path = argv[0];
        job_name                        = prog_path.filename().string();
      }
      hydra::assign_path(config->node, {"hydra", "job", "name"},
                         hydra::make_string(job_name), false);
    }
  } catch (const std::exception& ex) {
    assign_error(error_message,
                 std::string("Failed to set job name: ") + ex.what());
    return HYDRA_STATUS_ERROR;
  }

  // Resolve interpolations after loading all configs and overrides
  try {
    hydra::resolve_interpolations(config->node);
  } catch (const std::exception& ex) {
    assign_error(error_message,
                 std::string("Failed to resolve interpolations: ") + ex.what());
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

hydra_status_t hydra_config_clone_string(const hydra_config_t* config,
                                         const char* path_expression,
                                         char** out_value,
                                         char** error_message) {
  return hydra_config_get_string(config, path_expression, out_value,
                                 error_message);
}

hydra_status_t hydra_config_clone_string_list(const hydra_config_t* config,
                                              const char* path_expression,
                                              char*** out_items,
                                              size_t* out_count,
                                              char** error_message) {
  if (out_items != nullptr) {
    *out_items = nullptr;
  }
  if (out_count != nullptr) {
    *out_count = 0;
  }
  if (config == nullptr || out_items == nullptr || out_count == nullptr) {
    assign_error(error_message, "Config or output pointer is null");
    return HYDRA_STATUS_ERROR;
  }
  try {
    hydra_config_t* mutable_config = const_cast<hydra_config_t*>(config);
    ensure_resolved(mutable_config);
    const hydra::ConfigNode* node = nullptr;
    if (path_expression == nullptr || path_expression[0] == '\0') {
      node = &config->node;
    } else {
      node = locate(config, path_expression);
    }
    if (node == nullptr) {
      assign_error(error_message, "Requested node does not exist");
      return HYDRA_STATUS_ERROR;
    }
    if (!node->is_sequence()) {
      assign_error(error_message, "Requested node is not a sequence");
      return HYDRA_STATUS_ERROR;
    }
    const auto& sequence = node->as_sequence();
    if (sequence.empty()) {
      return HYDRA_STATUS_OK;
    }
    char** buffer =
        static_cast<char**>(std::calloc(sequence.size(), sizeof(char*)));
    if (buffer == nullptr) {
      assign_error(error_message, "Out of memory");
      return HYDRA_STATUS_ERROR;
    }
    size_t count = 0;
    for (const auto& element : sequence) {
      if (!element.is_string()) {
        assign_error(error_message, "Sequence element is not a string");
        hydra_string_list_free(buffer, count);
        return HYDRA_STATUS_ERROR;
      }
      buffer[count] = dup_string(element.as_string());
      if (buffer[count] == nullptr) {
        assign_error(error_message, "Out of memory");
        hydra_string_list_free(buffer, count);
        return HYDRA_STATUS_ERROR;
      }
      ++count;
    }
    *out_items = buffer;
    *out_count = count;
    return HYDRA_STATUS_OK;
  } catch (const std::exception& ex) {
    assign_error(error_message, ex.what());
    return HYDRA_STATUS_ERROR;
  }
}

void hydra_string_list_free(char** items, size_t count) {
  if (items == nullptr) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    hydra_string_free(items[i]);
  }
  std::free(items);
}

hydra_status_t hydra_config_ensure_directory(const hydra_config_t* config,
                                             const char* path_expression,
                                             char** error_message) {
  if (config == nullptr || path_expression == nullptr) {
    assign_error(error_message, "Config or path is null");
    return HYDRA_STATUS_ERROR;
  }
  char* path_value      = nullptr;
  hydra_status_t status = hydra_config_get_string(config, path_expression,
                                                  &path_value, error_message);
  if (status != HYDRA_STATUS_OK) {
    return status;
  }
  std::string directory_path(path_value ? path_value : "");
  hydra_string_free(path_value);
  if (directory_path.empty()) {
    assign_error(error_message, "Directory path is empty");
    return HYDRA_STATUS_ERROR;
  }
  try {
    std::filesystem::path dir(directory_path);
    std::filesystem::create_directories(dir);
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
