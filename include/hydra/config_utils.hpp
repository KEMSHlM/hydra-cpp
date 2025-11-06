#pragma once

#include "hydra/config_node.hpp"
#include "hydra/yaml_emitter.hpp"
#include "hydra/yaml_loader.hpp"

#include <filesystem>
#include <initializer_list>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hydra::utils {

namespace detail {

inline std::vector<std::string>
to_vector(std::initializer_list<const char*> parts) {
  std::vector<std::string> result;
  result.reserve(parts.size());
  for (const char* part : parts) {
    result.emplace_back(part);
  }
  return result;
}

inline std::string join_path(const std::vector<std::string>& path) {
  std::string joined;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i)
      joined += '.';
    joined += path[i];
  }
  return joined;
}

} // namespace detail

inline const ConfigNode&
require_node(const ConfigNode& root,
             std::initializer_list<const char*> path_parts) {
  std::vector<std::string> path = detail::to_vector(path_parts);
  const ConfigNode* node        = find_path(root, path);
  if (node == nullptr) {
    throw std::runtime_error("Missing required configuration node: " +
                             detail::join_path(path));
  }
  return *node;
}

inline bool has_node(const ConfigNode& root,
                     std::initializer_list<const char*> path_parts) {
  return find_path(root, detail::to_vector(path_parts)) != nullptr;
}

inline std::string
expect_string(const ConfigNode& root,
              std::initializer_list<const char*> path_parts) {
  const ConfigNode& node = require_node(root, path_parts);
  if (!node.is_string()) {
    throw std::runtime_error("Expected string at " +
                             detail::join_path(detail::to_vector(path_parts)));
  }
  return node.as_string();
}

inline int64_t expect_int(const ConfigNode& root,
                          std::initializer_list<const char*> path_parts) {
  const ConfigNode& node = require_node(root, path_parts);
  if (!node.is_int()) {
    throw std::runtime_error("Expected integer at " +
                             detail::join_path(detail::to_vector(path_parts)));
  }
  return node.as_int();
}

inline double expect_double(const ConfigNode& root,
                            std::initializer_list<const char*> path_parts) {
  const ConfigNode& node = require_node(root, path_parts);
  if (node.is_double()) {
    return node.as_double();
  }
  if (node.is_int()) {
    return static_cast<double>(node.as_int());
  }
  throw std::runtime_error("Expected numeric value at " +
                           detail::join_path(detail::to_vector(path_parts)));
}

inline bool expect_bool(const ConfigNode& root,
                        std::initializer_list<const char*> path_parts) {
  const ConfigNode& node = require_node(root, path_parts);
  if (!node.is_bool()) {
    throw std::runtime_error("Expected boolean at " +
                             detail::join_path(detail::to_vector(path_parts)));
  }
  return node.as_bool();
}

inline void write_yaml(std::ostream& out, const ConfigNode& root) {
  hydra::emit_yaml(root, out);
  if (!out.good()) {
    throw std::runtime_error("Failed to write YAML to stream");
  }
}

inline void write_yaml(const ConfigNode& root,
                       const std::filesystem::path& path) {
  hydra::write_yaml_file(root, path);
}

std::filesystem::path
write_hydra_outputs(const ConfigNode& root,
                    const std::vector<std::string>& overrides);

// Initialize Hydra configuration from command-line arguments
// Performs: config loading, override application, job.name derivation, and
// interpolation
ConfigNode initialize(int argc, char** argv,
                      const std::string& default_config = "configs/main.yaml");

} // namespace hydra::utils
