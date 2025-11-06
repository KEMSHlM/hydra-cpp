#include "hydra/interpolation.hpp"

#include "hydra/overrides.hpp"
#include "hydra/time_utils.hpp"

#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace hydra {

namespace {

std::string join_path(const std::vector<std::string>& path) {
  if (path.empty()) {
    return "<root>";
  }
  std::ostringstream oss;
  for (size_t i = 0; i < path.size(); ++i) {
    if (i > 0) {
      oss << '.';
    }
    oss << path[i];
  }
  return oss.str();
}

std::string trim_copy(std::string text) {
  size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string node_to_string(const ConfigNode& node) {
  if (node.is_string()) {
    return node.as_string();
  }
  if (node.is_int()) {
    return std::to_string(node.as_int());
  }
  if (node.is_double()) {
    std::ostringstream oss;
    oss << node.as_double();
    return oss.str();
  }
  if (node.is_bool()) {
    return node.as_bool() ? "true" : "false";
  }
  if (node.is_null()) {
    return "null";
  }
  throw std::runtime_error("Cannot interpolate complex node types");
}

std::string resolve_string(ConfigNode& root,
                           const std::vector<std::string>& current_path,
                           const std::string& value,
                           std::set<std::string>& resolving,
                           std::set<std::string>& resolved);

void resolve_node(ConfigNode& root, ConfigNode& node,
                  const std::vector<std::string>& path,
                  std::set<std::string>& resolving,
                  std::set<std::string>& resolved);

std::string resolve_env_expression(ConfigNode& root,
                                   const std::vector<std::string>& current_path,
                                   const std::string& body,
                                   std::set<std::string>& resolving,
                                   std::set<std::string>& resolved) {
  auto comma           = body.find(',');
  std::string var      = trim_copy(body.substr(0, comma));
  std::string fallback = comma == std::string::npos
                             ? std::string()
                             : trim_copy(body.substr(comma + 1));

  const char* env_value = std::getenv(var.c_str());
  if (env_value != nullptr && *env_value != '\0') {
    return std::string(env_value);
  }
  if (fallback.empty()) {
    return std::string{};
  }
  return resolve_string(root, current_path, fallback, resolving, resolved);
}

std::string resolve_expression(ConfigNode& root,
                               const std::vector<std::string>& current_path,
                               const std::string& expression,
                               std::set<std::string>& resolving,
                               std::set<std::string>& resolved) {
  if (expression.rfind("now:", 0) == 0) {
    return format_now(expression.substr(4));
  }
  if (expression.rfind("oc.env:", 0) == 0) {
    return resolve_env_expression(root, current_path, expression.substr(7),
                                  resolving, resolved);
  }

  std::vector<std::string> target_path = parse_override_path(expression);
  ConfigNode* target                   = find_path(root, target_path);
  if (target == nullptr) {
    std::ostringstream oss;
    oss << "Interpolation reference '" << expression << "' not found";
    throw std::runtime_error(oss.str());
  }
  resolve_node(root, *target, target_path, resolving, resolved);
  return node_to_string(*target);
}

std::string resolve_string(ConfigNode& root,
                           const std::vector<std::string>& current_path,
                           const std::string& value,
                           std::set<std::string>& resolving,
                           std::set<std::string>& resolved) {
  std::string result;
  size_t pos = 0;
  while (pos < value.size()) {
    size_t start = value.find("${", pos);
    if (start == std::string::npos) {
      result.append(value.substr(pos));
      break;
    }
    result.append(value.substr(pos, start - pos));
    size_t end = value.find('}', start + 2);
    if (end == std::string::npos) {
      throw std::runtime_error("Unterminated ${...} placeholder");
    }
    std::string expr = value.substr(start + 2, end - (start + 2));
    result.append(
        resolve_expression(root, current_path, expr, resolving, resolved));
    pos = end + 1;
  }
  return result;
}

void resolve_node(ConfigNode& root, ConfigNode& node,
                  const std::vector<std::string>& path,
                  std::set<std::string>& resolving,
                  std::set<std::string>& resolved) {
  std::string key = join_path(path);
  if (resolved.count(key)) {
    return;
  }
  if (!resolving.insert(key).second) {
    std::ostringstream oss;
    oss << "Detected interpolation cycle involving '" << key << "'";
    throw std::runtime_error(oss.str());
  }

  if (node.is_mapping()) {
    for (auto& entry : node.as_mapping()) {
      auto child_path = path;
      child_path.push_back(entry.first);
      resolve_node(root, entry.second, child_path, resolving, resolved);
    }
  } else if (node.is_sequence()) {
    auto& seq = node.as_sequence();
    for (size_t idx = 0; idx < seq.size(); ++idx) {
      auto child_path = path;
      child_path.push_back(std::to_string(idx));
      resolve_node(root, seq[idx], child_path, resolving, resolved);
    }
  } else if (node.is_string()) {
    std::string resolved_value =
        resolve_string(root, path, node.as_string(), resolving, resolved);
    node = make_string(std::move(resolved_value));
  }

  resolving.erase(key);
  resolved.insert(std::move(key));
}

} // namespace

void resolve_interpolations(ConfigNode& root) {
  std::set<std::string> resolving;
  std::set<std::string> resolved;
  resolve_node(root, root, {}, resolving, resolved);
}

} // namespace hydra
