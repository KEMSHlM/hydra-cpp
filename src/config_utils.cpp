#include "hydra/config_utils.hpp"

#include "hydra/yaml_loader.hpp"

#include <filesystem>
#include <vector>

namespace hydra::utils {

namespace fs = std::filesystem;

fs::path write_hydra_outputs(const ConfigNode& root,
                             const std::vector<std::string>& overrides) {
  std::string run_dir_value = expect_string(root, {"hydra", "run", "dir"});
  fs::path run_dir          = fs::path(run_dir_value);
  fs::create_directories(run_dir);

  fs::path hydra_dir = run_dir / ".hydra";
  fs::create_directories(hydra_dir);

  write_yaml(root, hydra_dir / "config.yaml");

  ConfigNode hydra_config = make_mapping();
  if (const ConfigNode* hydra_node = hydra::find_path(root, {"hydra"})) {
    hydra_config = hydra::deep_copy(*hydra_node);
  }
  write_yaml(hydra_config, hydra_dir / "hydra.yaml");

  ConfigNode overrides_node = make_sequence();
  auto& seq                 = overrides_node.as_sequence();
  seq.reserve(overrides.size());
  for (const std::string& expr : overrides) {
    seq.emplace_back(expr);
  }
  write_yaml(overrides_node, hydra_dir / "overrides.yaml");

  return run_dir;
}

} // namespace hydra::utils
