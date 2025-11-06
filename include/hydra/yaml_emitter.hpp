#pragma once

#include "hydra/config_node.hpp"

#include <filesystem>
#include <ostream>
#include <string>

namespace hydra {

void emit_yaml(const ConfigNode& node, std::ostream& out, int indent = 0);
std::string to_yaml_string(const ConfigNode& node);
void write_yaml_file(const ConfigNode& node, const std::filesystem::path& path);

} // namespace hydra
