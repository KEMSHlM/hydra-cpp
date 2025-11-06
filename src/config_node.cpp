#include "hydra/config_node.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

namespace hydra {

ConfigNode::ConfigNode() : value_(nullptr) {
}
ConfigNode::ConfigNode(std::nullptr_t) : value_(nullptr) {
}
ConfigNode::ConfigNode(bool value) : value_(value) {
}
ConfigNode::ConfigNode(int64_t value) : value_(value) {
}
ConfigNode::ConfigNode(double value) : value_(value) {
}
ConfigNode::ConfigNode(const std::string& value) : value_(value) {
}
ConfigNode::ConfigNode(std::string&& value) : value_(std::move(value)) {
}
ConfigNode::ConfigNode(const char* value) : value_(std::string(value)) {
}
ConfigNode::ConfigNode(const seq_t& sequence) : value_(sequence) {
}
ConfigNode::ConfigNode(seq_t&& sequence) : value_(std::move(sequence)) {
}
ConfigNode::ConfigNode(const map_t& mapping) : value_(mapping) {
}
ConfigNode::ConfigNode(map_t&& mapping) : value_(std::move(mapping)) {
}

bool ConfigNode::is_null() const {
  return std::holds_alternative<std::nullptr_t>(value_);
}
bool ConfigNode::is_bool() const {
  return std::holds_alternative<bool>(value_);
}
bool ConfigNode::is_int() const {
  return std::holds_alternative<int64_t>(value_);
}
bool ConfigNode::is_double() const {
  return std::holds_alternative<double>(value_);
}
bool ConfigNode::is_string() const {
  return std::holds_alternative<std::string>(value_);
}
bool ConfigNode::is_sequence() const {
  return std::holds_alternative<seq_t>(value_);
}
bool ConfigNode::is_mapping() const {
  return std::holds_alternative<map_t>(value_);
}

bool ConfigNode::empty() const {
  if (is_null()) {
    return true;
  }
  if (is_sequence()) {
    return as_sequence().empty();
  }
  if (is_mapping()) {
    return as_mapping().empty();
  }
  return false;
}

bool ConfigNode::as_bool() const {
  if (!is_bool()) {
    throw std::runtime_error("ConfigNode: value is not a bool");
  }
  return std::get<bool>(value_);
}

int64_t ConfigNode::as_int() const {
  if (!is_int()) {
    throw std::runtime_error("ConfigNode: value is not an int");
  }
  return std::get<int64_t>(value_);
}

double ConfigNode::as_double() const {
  if (is_double()) {
    return std::get<double>(value_);
  }
  if (is_int()) {
    return static_cast<double>(std::get<int64_t>(value_));
  }
  throw std::runtime_error("ConfigNode: value is not numeric");
}

const std::string& ConfigNode::as_string() const {
  if (!is_string()) {
    throw std::runtime_error("ConfigNode: value is not a string");
  }
  return std::get<std::string>(value_);
}

const ConfigNode::seq_t& ConfigNode::as_sequence() const {
  if (!is_sequence()) {
    throw std::runtime_error("ConfigNode: value is not a sequence");
  }
  return std::get<seq_t>(value_);
}

const ConfigNode::map_t& ConfigNode::as_mapping() const {
  if (!is_mapping()) {
    throw std::runtime_error("ConfigNode: value is not a mapping");
  }
  return std::get<map_t>(value_);
}

ConfigNode::seq_t& ConfigNode::as_sequence() {
  if (!is_sequence()) {
    throw std::runtime_error("ConfigNode: value is not a sequence");
  }
  return std::get<seq_t>(value_);
}

ConfigNode::map_t& ConfigNode::as_mapping() {
  if (!is_mapping()) {
    throw std::runtime_error("ConfigNode: value is not a mapping");
  }
  return std::get<map_t>(value_);
}

std::string ConfigNode::type_name() const {
  if (is_null())
    return "null";
  if (is_bool())
    return "bool";
  if (is_int())
    return "int";
  if (is_double())
    return "double";
  if (is_string())
    return "string";
  if (is_sequence())
    return "sequence";
  if (is_mapping())
    return "mapping";
  return "unknown";
}

ConfigNode make_null() {
  return ConfigNode(nullptr);
}
ConfigNode make_bool(bool value) {
  return ConfigNode(value);
}
ConfigNode make_int(int64_t value) {
  return ConfigNode(value);
}
ConfigNode make_double(double value) {
  return ConfigNode(value);
}
ConfigNode make_string(std::string value) {
  return ConfigNode(std::move(value));
}
ConfigNode make_sequence() {
  return ConfigNode(ConfigNode::seq_t{});
}
ConfigNode make_mapping() {
  return ConfigNode(ConfigNode::map_t{});
}

namespace {

ConfigNode deep_copy_impl(const ConfigNode& node) {
  if (node.is_sequence()) {
    ConfigNode::seq_t seq_copy;
    seq_copy.reserve(node.as_sequence().size());
    for (const auto& child : node.as_sequence()) {
      seq_copy.push_back(deep_copy_impl(child));
    }
    return ConfigNode(std::move(seq_copy));
  }
  if (node.is_mapping()) {
    ConfigNode::map_t map_copy;
    for (const auto& entry : node.as_mapping()) {
      map_copy.emplace(entry.first, deep_copy_impl(entry.second));
    }
    return ConfigNode(std::move(map_copy));
  }
  return node;
}

} // namespace

ConfigNode deep_copy(const ConfigNode& node) {
  return deep_copy_impl(node);
}

namespace {

void merge_maps(ConfigNode::map_t& destination,
                const ConfigNode::map_t& source) {
  for (const auto& entry : source) {
    auto it = destination.find(entry.first);
    if (it == destination.end()) {
      destination.emplace(entry.first, deep_copy(entry.second));
    } else {
      merge(it->second, entry.second);
    }
  }
}

} // namespace

void merge(ConfigNode& destination, const ConfigNode& source) {
  if (source.is_null()) {
    destination = ConfigNode(nullptr);
    return;
  }

  if (destination.is_null()) {
    destination = deep_copy(source);
    return;
  }

  if (destination.is_mapping() && source.is_mapping()) {
    merge_maps(destination.as_mapping(), source.as_mapping());
    return;
  }

  // Replace destination with source when types differ or are non-map
  // containers.
  destination = deep_copy(source);
}

ConfigNode merged(const ConfigNode& base, const ConfigNode& override_node) {
  ConfigNode result = deep_copy(base);
  merge(result, override_node);
  return result;
}

ConfigNode* find_path(ConfigNode& root, const std::vector<std::string>& path) {
  ConfigNode* current = &root;
  for (const auto& component : path) {
    if (!current->is_mapping()) {
      return nullptr;
    }
    auto& mapping = current->as_mapping();
    auto it       = mapping.find(component);
    if (it == mapping.end()) {
      return nullptr;
    }
    current = &it->second;
  }
  return current;
}

const ConfigNode* find_path(const ConfigNode& root,
                            const std::vector<std::string>& path) {
  const ConfigNode* current = &root;
  for (const auto& component : path) {
    if (!current->is_mapping()) {
      return nullptr;
    }
    const auto& mapping = current->as_mapping();
    auto it             = mapping.find(component);
    if (it == mapping.end()) {
      return nullptr;
    }
    current = &it->second;
  }
  return current;
}

void assign_path(ConfigNode& root, const std::vector<std::string>& path,
                 ConfigNode value, bool require_new) {
  if (path.empty()) {
    throw std::runtime_error("Cannot assign empty path");
  }

  if (!root.is_mapping() && !root.is_null()) {
    throw std::runtime_error("Root configuration is not a mapping");
  }

  if (root.is_null()) {
    root = make_mapping();
  }

  ConfigNode* current = &root;
  for (size_t i = 0; i < path.size(); ++i) {
    auto& mapping              = current->as_mapping();
    const std::string& segment = path[i];
    bool is_leaf               = (i + 1 == path.size());
    auto it                    = mapping.find(segment);

    if (is_leaf) {
      if (it == mapping.end()) {
        if (!require_new) {
          std::ostringstream oss;
          oss << "Key '" << segment << "' does not exist. Use '+" << segment
              << "=...' to add new parameters.";
          throw std::runtime_error(oss.str());
        }
        mapping.emplace(segment, std::move(value));
      } else {
        if (require_new) {
          std::ostringstream oss;
          oss << "Cannot add new key '" << segment
              << "' because it already exists";
          throw std::runtime_error(oss.str());
        }
        it->second = std::move(value);
      }
    } else {
      if (it == mapping.end()) {
        if (!require_new) {
          std::ostringstream oss;
          oss << "Path component '" << segment << "' does not exist. Use '+"
              << segment << "=...' to introduce new nested parameters.";
          throw std::runtime_error(oss.str());
        }
        it = mapping.emplace(segment, make_mapping()).first;
      } else if (!it->second.is_mapping()) {
        std::ostringstream oss;
        oss << "Path component '" << segment
            << "' refers to a non-mapping node (" << it->second.type_name()
            << ")";
        throw std::runtime_error(oss.str());
      }
      current = &it->second;
    }
  }
}

} // namespace hydra
