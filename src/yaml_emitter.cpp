#include "hydra/yaml_emitter.hpp"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace hydra {

namespace {

std::string indentation(int indent) {
  return std::string(static_cast<size_t>(indent), ' ');
}

bool is_bool_keyword(const std::string& value) {
  return value == "true" || value == "True" || value == "false" ||
         value == "False";
}

bool is_null_keyword(const std::string& value) {
  return value == "null" || value == "Null" || value == "~";
}

bool looks_like_number(const std::string& value) {
  if (value.empty())
    return false;
  char* end = nullptr;
  std::strtod(value.c_str(), &end);
  return end != value.c_str() && *end == '\0';
}

bool needs_quoting(const std::string& value, bool is_key) {
  if (value.empty())
    return true;
  if (is_bool_keyword(value) || is_null_keyword(value) ||
      looks_like_number(value)) {
    return true;
  }
  if (value.find_first_of(":#&*?|-<>=!%@") != std::string::npos) {
    return true;
  }
  if (value.front() == '-' || value.front() == ' ' || value.back() == ' ') {
    return true;
  }
  if (value.find('\n') != std::string::npos ||
      value.find('\t') != std::string::npos) {
    return true;
  }
  if (is_key && value.find('.') != std::string::npos) {
    return true;
  }
  return false;
}

std::string escape_string(const std::string& value) {
  std::ostringstream oss;
  oss << '"';
  for (char ch : value) {
    switch (ch) {
    case '\\':
      oss << "\\\\";
      break;
    case '"':
      oss << "\\\"";
      break;
    case '\n':
      oss << "\\n";
      break;
    case '\r':
      oss << "\\r";
      break;
    case '\t':
      oss << "\\t";
      break;
    default:
      oss << ch;
    }
  }
  oss << '"';
  return oss.str();
}

std::string format_scalar(const ConfigNode& node) {
  if (node.is_null()) {
    return "null";
  }
  if (node.is_bool()) {
    return node.as_bool() ? "true" : "false";
  }
  if (node.is_int()) {
    return std::to_string(node.as_int());
  }
  if (node.is_double()) {
    std::ostringstream oss;
    oss << std::setprecision(15) << node.as_double();
    return oss.str();
  }
  if (node.is_string()) {
    const std::string& value = node.as_string();
    if (needs_quoting(value, false)) {
      return escape_string(value);
    }
    return value;
  }
  throw std::runtime_error("Cannot format non-scalar node directly");
}

std::string format_key(const std::string& key) {
  if (needs_quoting(key, true)) {
    return escape_string(key);
  }
  return key;
}

void emit_node(const ConfigNode& node, std::ostream& out, int indent);

void emit_sequence(const ConfigNode::seq_t& seq, std::ostream& out,
                   int indent) {
  if (seq.empty()) {
    out << indentation(indent) << "[]\n";
    return;
  }
  for (const auto& item : seq) {
    out << indentation(indent) << "-";
    if (item.is_mapping()) {
      if (item.as_mapping().empty()) {
        out << " {}\n";
      } else {
        out << "\n";
        emit_node(item, out, indent + 2);
      }
    } else if (item.is_sequence()) {
      if (item.as_sequence().empty()) {
        out << " []\n";
      } else {
        out << "\n";
        emit_node(item, out, indent + 2);
      }
    } else {
      out << " " << format_scalar(item) << "\n";
    }
  }
}

void emit_mapping(const ConfigNode::map_t& map, std::ostream& out, int indent) {
  if (map.empty()) {
    out << indentation(indent) << "{}\n";
    return;
  }
  for (const auto& entry : map) {
    const auto& key   = entry.first;
    const auto& value = entry.second;
    out << indentation(indent) << format_key(key) << ":";
    if (value.is_mapping()) {
      if (value.as_mapping().empty()) {
        out << " {}\n";
      } else {
        out << "\n";
        emit_node(value, out, indent + 2);
      }
    } else if (value.is_sequence()) {
      if (value.as_sequence().empty()) {
        out << " []\n";
      } else {
        out << "\n";
        emit_node(value, out, indent + 2);
      }
    } else {
      out << " " << format_scalar(value) << "\n";
    }
  }
}

void emit_node(const ConfigNode& node, std::ostream& out, int indent) {
  if (node.is_mapping()) {
    emit_mapping(node.as_mapping(), out, indent);
  } else if (node.is_sequence()) {
    emit_sequence(node.as_sequence(), out, indent);
  } else {
    out << indentation(indent) << format_scalar(node) << "\n";
  }
}

} // namespace

void emit_yaml(const ConfigNode& node, std::ostream& out, int indent) {
  emit_node(node, out, indent);
}

std::string to_yaml_string(const ConfigNode& node) {
  std::ostringstream oss;
  emit_yaml(node, oss, 0);
  return oss.str();
}

void write_yaml_file(const ConfigNode& node,
                     const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    std::ostringstream oss;
    oss << "Failed to open output file '" << path << "'";
    throw std::runtime_error(oss.str());
  }
  emit_yaml(node, out, 0);
}

} // namespace hydra
