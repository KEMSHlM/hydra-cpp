#include "hydra/overrides.hpp"

#include "hydra/yaml_loader.hpp"

#include <sstream>
#include <stdexcept>

namespace hydra {

namespace {

std::vector<std::string> split_path_expression(const std::string& expression) {
  std::vector<std::string> components;
  std::string current;
  bool escape = false;

  for (char ch : expression) {
    if (escape) {
      current.push_back(ch);
      escape = false;
    } else if (ch == '\\') {
      escape = true;
    } else if (ch == '.') {
      if (current.empty()) {
        throw std::runtime_error("Empty path component in override expression");
      }
      components.push_back(current);
      current.clear();
    } else {
      current.push_back(ch);
    }
  }

  if (escape) {
    throw std::runtime_error("Dangling escape in override path");
  }
  if (current.empty()) {
    throw std::runtime_error("Override path cannot end with '.'");
  }
  components.push_back(current);
  return components;
}

ConfigNode parse_value_expression(const std::string& expression) {
  std::string yaml_snippet = "value: " + expression + "\n";
  ConfigNode wrapper       = load_yaml_string(yaml_snippet, "<override>");
  if (!wrapper.is_mapping()) {
    throw std::runtime_error(
        "Override value parsing failed: unexpected YAML structure");
  }
  const auto& map = wrapper.as_mapping();
  auto it         = map.find("value");
  if (it == map.end()) {
    throw std::runtime_error(
        "Override value parsing failed: missing 'value' key");
  }
  return it->second;
}

} // namespace

std::vector<std::string> parse_override_path(const std::string& expression) {
  return split_path_expression(expression);
}

Override parse_override(const std::string& expression) {
  if (expression.empty()) {
    throw std::runtime_error("Empty override expression");
  }

  bool require_new    = false;
  std::string working = expression;
  if (working.front() == '+') {
    require_new = true;
    working.erase(working.begin());
    if (working.empty()) {
      throw std::runtime_error("Override expression missing key after '+'");
    }
  }

  auto eq_pos = working.find('=');
  if (eq_pos == std::string::npos) {
    std::ostringstream oss;
    oss << "Override expression '" << expression << "' is missing '='";
    throw std::runtime_error(oss.str());
  }
  std::string path_part  = working.substr(0, eq_pos);
  std::string value_part = working.substr(eq_pos + 1);
  if (path_part.empty()) {
    std::ostringstream oss;
    oss << "Override expression '" << expression << "' has empty key";
    throw std::runtime_error(oss.str());
  }
  if (value_part.empty()) {
    std::ostringstream oss;
    oss << "Override expression '" << expression << "' has empty value";
    throw std::runtime_error(oss.str());
  }

  std::vector<std::string> path = split_path_expression(path_part);
  ConfigNode value              = parse_value_expression(value_part);

  return {std::move(path), std::move(value), require_new};
}

} // namespace hydra
