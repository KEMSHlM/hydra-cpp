#pragma once

#include "hydra/config_node.hpp"

#include <filesystem>
#include <string>

namespace hydra {

ConfigNode load_yaml_file(const std::filesystem::path& path);
ConfigNode load_yaml_string(const std::string& content,
                            const std::string& name = "<string>");

} // namespace hydra
