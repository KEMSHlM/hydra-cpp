#include "hydra/yaml_loader.hpp"

#include "hydra/config_node.hpp"
#include "hydra/overrides.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <yaml.h>

namespace hydra {

namespace {

[[noreturn]] void throw_yaml_error(const std::string& context,
                                   yaml_parser_t& parser) {
  std::ostringstream oss;
  oss << "YAML parse error in " << context << ": ";
  if (parser.problem != nullptr) {
    oss << parser.problem;
    if (parser.problem_mark.line || parser.problem_mark.column) {
      oss << " (line " << (parser.problem_mark.line + 1) << ", column "
          << (parser.problem_mark.column + 1) << ")";
    }
  } else {
    oss << "unknown problem";
  }
  throw std::runtime_error(oss.str());
}

std::string event_scalar_to_string(const yaml_event_t& event) {
  const unsigned char* value = event.data.scalar.value;
  return std::string(reinterpret_cast<const char*>(value),
                     event.data.scalar.length);
}

bool is_integer_literal(std::string_view text) {
  if (text.empty())
    return false;
  size_t pos = 0;
  if (text[pos] == '-' || text[pos] == '+') {
    ++pos;
    if (pos >= text.size())
      return false;
  }
  if (text[pos] == '0' && text.size() > pos + 1) {
    return false; // prevent octal/hex for simplicity
  }
  for (; pos < text.size(); ++pos) {
    if (!std::isdigit(static_cast<unsigned char>(text[pos]))) {
      return false;
    }
  }
  return true;
}

bool is_float_literal(std::string_view text) {
  if (text.empty())
    return false;
  bool has_digit = false;
  bool has_dot   = false;
  bool has_exp   = false;
  size_t pos     = 0;
  if (text[pos] == '-' || text[pos] == '+') {
    ++pos;
    if (pos >= text.size())
      return false;
  }
  for (; pos < text.size(); ++pos) {
    char ch = text[pos];
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      has_digit = true;
    } else if (ch == '.') {
      if (has_dot || has_exp)
        return false;
      has_dot = true;
    } else if (ch == 'e' || ch == 'E') {
      if (has_exp || !has_digit)
        return false;
      has_exp   = true;
      has_digit = false;
      if (pos + 1 < text.size() &&
          (text[pos + 1] == '+' || text[pos + 1] == '-')) {
        ++pos;
      }
    } else {
      return false;
    }
  }
  return has_digit && (has_dot || has_exp);
}

std::string to_lower(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (char ch : text) {
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return result;
}

ConfigNode interpret_scalar(const yaml_event_t& event) {
  std::string value = event_scalar_to_string(event);
  std::string lower = to_lower(value);

  if (lower == "null" || lower == "~") {
    return make_null();
  }
  if (lower == "true") {
    return make_bool(true);
  }
  if (lower == "false") {
    return make_bool(false);
  }
  if (is_integer_literal(value)) {
    try {
      int64_t parsed = std::stoll(value);
      return make_int(parsed);
    } catch (...) {
      // fall back to string if out of range
    }
  }
  if (is_float_literal(value)) {
    try {
      double parsed = std::stod(value);
      return make_double(parsed);
    } catch (...) {
      // fall back to string if parsing fails
    }
  }

  return make_string(std::move(value));
}

ConfigNode parse_node(yaml_parser_t& parser, yaml_event_t& event,
                      const std::string& context);

ConfigNode parse_sequence(yaml_parser_t& parser, yaml_event_t& event,
                          const std::string& context) {
  yaml_event_delete(&event);
  ConfigNode::seq_t sequence;

  while (true) {
    yaml_event_t next;
    if (!yaml_parser_parse(&parser, &next)) {
      throw_yaml_error(context, parser);
    }
    if (next.type == YAML_SEQUENCE_END_EVENT) {
      yaml_event_delete(&next);
      break;
    }
    sequence.push_back(parse_node(parser, next, context));
  }
  return ConfigNode(std::move(sequence));
}

ConfigNode parse_mapping(yaml_parser_t& parser, yaml_event_t& event,
                         const std::string& context) {
  yaml_event_delete(&event);
  ConfigNode::map_t mapping;

  while (true) {
    yaml_event_t key_event;
    if (!yaml_parser_parse(&parser, &key_event)) {
      throw_yaml_error(context, parser);
    }
    if (key_event.type == YAML_MAPPING_END_EVENT) {
      yaml_event_delete(&key_event);
      break;
    }
    ConfigNode key_node = parse_node(parser, key_event, context);
    if (!key_node.is_string()) {
      throw std::runtime_error("YAML mapping keys must be scalars");
    }
    yaml_event_t value_event;
    if (!yaml_parser_parse(&parser, &value_event)) {
      throw_yaml_error(context, parser);
    }
    ConfigNode value_node = parse_node(parser, value_event, context);
    mapping.emplace(key_node.as_string(), std::move(value_node));
  }
  return ConfigNode(std::move(mapping));
}

ConfigNode parse_node(yaml_parser_t& parser, yaml_event_t& event,
                      const std::string& context) {
  switch (event.type) {
  case YAML_SCALAR_EVENT: {
    ConfigNode node = interpret_scalar(event);
    yaml_event_delete(&event);
    return node;
  }
  case YAML_SEQUENCE_START_EVENT:
    return parse_sequence(parser, event, context);
  case YAML_MAPPING_START_EVENT:
    return parse_mapping(parser, event, context);
  case YAML_ALIAS_EVENT:
    yaml_event_delete(&event);
    throw std::runtime_error("YAML aliases are not supported");
  default:
    yaml_event_delete(&event);
    throw std::runtime_error("Unexpected YAML event while parsing node");
  }
}

ConfigNode parse_stream(yaml_parser_t& parser, const std::string& context) {
  yaml_event_t event;

  if (!yaml_parser_parse(&parser, &event)) {
    throw_yaml_error(context, parser);
  }
  if (event.type != YAML_STREAM_START_EVENT) {
    yaml_event_delete(&event);
    throw std::runtime_error("YAML stream did not start correctly");
  }
  yaml_event_delete(&event);

  if (!yaml_parser_parse(&parser, &event)) {
    throw_yaml_error(context, parser);
  }
  if (event.type == YAML_STREAM_END_EVENT) {
    yaml_event_delete(&event);
    return make_null();
  }

  if (event.type != YAML_DOCUMENT_START_EVENT) {
    yaml_event_delete(&event);
    throw std::runtime_error("Expected YAML document start");
  }
  yaml_event_delete(&event);

  if (!yaml_parser_parse(&parser, &event)) {
    throw_yaml_error(context, parser);
  }

  ConfigNode root = parse_node(parser, event, context);

  if (!yaml_parser_parse(&parser, &event)) {
    throw_yaml_error(context, parser);
  }
  if (event.type != YAML_DOCUMENT_END_EVENT) {
    yaml_event_delete(&event);
    throw std::runtime_error("Expected YAML document end");
  }
  yaml_event_delete(&event);

  if (!yaml_parser_parse(&parser, &event)) {
    throw_yaml_error(context, parser);
  }
  if (event.type != YAML_STREAM_END_EVENT) {
    yaml_event_delete(&event);
    throw std::runtime_error("Expected YAML stream end");
  }
  yaml_event_delete(&event);

  return root;
}

ConfigNode parse_yaml_file_raw(const std::filesystem::path& path) {
  FILE* file = fopen(path.string().c_str(), "rb");
  if (file == nullptr) {
    std::ostringstream oss;
    oss << "Failed to open YAML file '" << path
        << "': " << std::strerror(errno);
    throw std::runtime_error(oss.str());
  }

  yaml_parser_t parser;
  if (!yaml_parser_initialize(&parser)) {
    fclose(file);
    throw std::runtime_error("Failed to initialize YAML parser");
  }
  yaml_parser_set_input_file(&parser, file);

  ConfigNode result = parse_stream(parser, path.string());

  yaml_parser_delete(&parser);
  fclose(file);
  return result;
}

ConfigNode parse_yaml_string_raw(const std::string& content,
                                 const std::string& name) {
  yaml_parser_t parser;
  if (!yaml_parser_initialize(&parser)) {
    throw std::runtime_error("Failed to initialize YAML parser");
  }
  yaml_parser_set_input_string(
      &parser, reinterpret_cast<const unsigned char*>(content.data()),
      content.size());

  ConfigNode result = parse_stream(parser, name);

  yaml_parser_delete(&parser);
  return result;
}

std::filesystem::path normalize_path(const std::filesystem::path& path) {
  std::error_code ec;
  auto canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec) {
    return canonical;
  }
  return std::filesystem::absolute(path);
}

struct DefaultSpec {
  std::filesystem::path include_path;
  std::optional<std::vector<std::string>> target_path;
  bool optional = false;
};

std::string_view trim_view(std::string_view text) {
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

std::string trim_copy(std::string text) {
  auto view = trim_view(text);
  return std::string(view.begin(), view.end());
}

DefaultSpec parse_default_entry(const ConfigNode& entry,
                                const std::filesystem::path& base_dir) {
  if (entry.is_string()) {
    std::string value = entry.as_string();
    bool optional     = false;
    if (!value.empty() && value.front() == '?') {
      optional = true;
      value.erase(value.begin());
      if (!value.empty() && value.front() == ' ') {
        value.erase(value.begin());
      }
    }
    value                           = trim_copy(std::move(value));
    std::filesystem::path candidate = value;
    if (!candidate.has_extension()) {
      candidate += ".yaml";
    }
    if (candidate.is_relative()) {
      candidate = base_dir / candidate;
    }
    candidate = candidate.lexically_normal();
    return {candidate, std::nullopt, optional};
  }

  if (entry.is_mapping()) {
    const auto& map = entry.as_mapping();
    if (map.size() != 1) {
      throw std::runtime_error(
          "defaults entries as mappings must contain exactly one key");
    }
    const auto& pair = *map.begin();
    if (!pair.second.is_string()) {
      throw std::runtime_error("defaults mapping values must be strings");
    }
    std::string key = pair.first;
    bool optional   = false;
    if (!key.empty() && key.front() == '?') {
      optional = true;
      key.erase(key.begin());
      if (!key.empty() && key.front() == ' ') {
        key.erase(key.begin());
      }
    }
    key                                  = trim_copy(std::move(key));
    std::vector<std::string> target_path = hydra::parse_override_path(key);

    std::filesystem::path candidate = key;
    candidate /= pair.second.as_string();
    if (!candidate.has_extension()) {
      candidate += ".yaml";
    }
    if (candidate.is_relative()) {
      candidate = base_dir / candidate;
    }
    candidate = candidate.lexically_normal();
    return {candidate, std::move(target_path), optional};
  }

  throw std::runtime_error("Unsupported defaults entry type");
}

ConfigNode load_with_includes(const std::filesystem::path& path,
                              std::set<std::filesystem::path>& stack) {
  std::filesystem::path normalized = normalize_path(path);
  if (!stack.insert(normalized).second) {
    std::ostringstream oss;
    oss << "Detected recursive configuration include involving '" << normalized
        << "'";
    throw std::runtime_error(oss.str());
  }

  ConfigNode root = parse_yaml_file_raw(normalized);

  ConfigNode result;
  if (root.is_mapping()) {
    result           = make_mapping();
    auto& mapping    = root.as_mapping();
    auto defaults_it = mapping.find("defaults");
    if (defaults_it != mapping.end()) {
      const ConfigNode& defaults_node = defaults_it->second;
      if (!defaults_node.is_sequence()) {
        throw std::runtime_error("'defaults' must be a sequence");
      }
      const auto& defaults = defaults_node.as_sequence();
      const auto base_dir  = normalized.parent_path();
      for (const auto& entry : defaults) {
        if (entry.is_string() && entry.as_string() == "_self_") {
          continue;
        }
        DefaultSpec spec = parse_default_entry(entry, base_dir);
        std::error_code ec;
        if (!std::filesystem::exists(spec.include_path, ec)) {
          if (spec.optional) {
            continue;
          }
          std::ostringstream oss;
          oss << "Included configuration '" << spec.include_path
              << "' not found";
          throw std::runtime_error(oss.str());
        }
        ConfigNode child = load_with_includes(spec.include_path, stack);
        if (spec.target_path) {
          ConfigNode* existing = find_path(result, *spec.target_path);
          if (existing == nullptr) {
            assign_path(result, *spec.target_path, std::move(child), true);
          } else {
            merge(*existing, child);
          }
        } else {
          merge(result, child);
        }
      }
      mapping.erase(defaults_it);
    }

    merge(result, root);
  } else {
    result = root;
  }

  stack.erase(normalized);
  return result;
}

} // namespace

ConfigNode load_yaml_file(const std::filesystem::path& path) {
  std::set<std::filesystem::path> stack;
  return load_with_includes(path, stack);
}

ConfigNode load_yaml_string(const std::string& content,
                            const std::string& name) {
  return parse_yaml_string_raw(content, name);
}

} // namespace hydra
