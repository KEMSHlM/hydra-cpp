#pragma once

#include "hydra/config_node.hpp"

#include <string>
#include <vector>

namespace hydra {

struct Override {
  std::vector<std::string> path;
  ConfigNode value;
  bool require_new;
};

std::vector<std::string> parse_override_path(const std::string& expression);
Override parse_override(const std::string& expression);

} // namespace hydra
