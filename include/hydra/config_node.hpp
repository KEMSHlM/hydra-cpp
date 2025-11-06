#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace hydra {

class ConfigNode {
public:
  using map_t = std::map<std::string, ConfigNode>;
  using seq_t = std::vector<ConfigNode>;

  ConfigNode();
  ConfigNode(std::nullptr_t);
  ConfigNode(bool value);
  ConfigNode(int64_t value);
  ConfigNode(double value);
  ConfigNode(const std::string& value);
  ConfigNode(std::string&& value);
  ConfigNode(const char* value);
  ConfigNode(const seq_t& sequence);
  ConfigNode(seq_t&& sequence);
  ConfigNode(const map_t& mapping);
  ConfigNode(map_t&& mapping);

  bool is_null() const;
  bool is_bool() const;
  bool is_int() const;
  bool is_double() const;
  bool is_string() const;
  bool is_sequence() const;
  bool is_mapping() const;

  bool empty() const;

  bool as_bool() const;
  int64_t as_int() const;
  double as_double() const;
  const std::string& as_string() const;
  const seq_t& as_sequence() const;
  const map_t& as_mapping() const;
  seq_t& as_sequence();
  map_t& as_mapping();

  std::string type_name() const;

private:
  using variant_t = std::variant<std::nullptr_t, bool, int64_t, double,
                                 std::string, seq_t, map_t>;
  variant_t value_;
};

ConfigNode make_null();
ConfigNode make_bool(bool value);
ConfigNode make_int(int64_t value);
ConfigNode make_double(double value);
ConfigNode make_string(std::string value);
ConfigNode make_sequence();
ConfigNode make_mapping();

void merge(ConfigNode& destination, const ConfigNode& source);
ConfigNode merged(const ConfigNode& base, const ConfigNode& override_node);

ConfigNode deep_copy(const ConfigNode& node);

ConfigNode* find_path(ConfigNode& root, const std::vector<std::string>& path);
const ConfigNode* find_path(const ConfigNode& root,
                            const std::vector<std::string>& path);

void assign_path(ConfigNode& root, const std::vector<std::string>& path,
                 ConfigNode value, bool require_new);

} // namespace hydra
