#include "hydra/config_utils.hpp"

#include "hydra/interpolation.hpp"
#include "hydra/overrides.hpp"
#include "hydra/yaml_loader.hpp"

#include <filesystem>
#include <string>
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

ConfigNode initialize(int argc, char** argv,
                      const std::string& default_config) {
  // Parse command-line arguments
  std::vector<fs::path> config_files;
  std::vector<std::string> overrides;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--config requires an argument");
      }
      config_files.emplace_back(argv[++i]);
    } else if (arg.rfind("--config=", 0) == 0) {
      config_files.emplace_back(arg.substr(9));
    } else {
      overrides.emplace_back(std::move(arg));
    }
  }

  if (config_files.empty() && !default_config.empty()) {
    config_files.emplace_back(default_config);
  }

  // Load and merge config files
  ConfigNode config = make_mapping();
  for (const auto& path : config_files) {
    ConfigNode loaded = load_yaml_file(path);
    merge(config, loaded);
  }

  // Apply overrides
  for (const auto& expr : overrides) {
    Override ov = parse_override(expr);
    assign_path(config, ov.path, std::move(ov.value), ov.require_new);
  }

  // Set job name from program name if not already set
  const ConfigNode* job_name_node = find_path(config, {"hydra", "job", "name"});
  if (!job_name_node || job_name_node->is_null()) {
    std::string job_name = "app"; // default fallback
    if (argc > 0 && argv != nullptr && argv[0] != nullptr) {
      // Extract basename from argv[0]
      fs::path prog_path = argv[0];
      job_name           = prog_path.filename().string();
    }
    assign_path(config, {"hydra", "job", "name"}, make_string(job_name), false);
  }

  // Resolve interpolations
  resolve_interpolations(config);

  return config;
}

} // namespace hydra::utils
