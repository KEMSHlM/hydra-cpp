#include "hydra/config_node.hpp"
#include "hydra/interpolation.hpp"
#include "hydra/overrides.hpp"
#include "hydra/yaml_emitter.hpp"
#include "hydra/yaml_loader.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using hydra::assign_path;
using hydra::ConfigNode;
using hydra::load_yaml_file;
using hydra::make_mapping;
using hydra::make_null;
using hydra::make_string;
using hydra::merge;
using hydra::Override;
using hydra::parse_override;
using hydra::to_yaml_string;

namespace {

struct Options {
  std::vector<fs::path> config_files;
  bool show_help = false;
};

void print_usage() {
  std::cout << "hydra-cpp - lightweight configuration orchestration\n\n"
            << "Usage:\n"
            << "  hydra-cpp [options] [overrides]\n\n"
            << "Options:\n"
            << "  -c, --config <file>       Load a configuration YAML file "
               "(can be repeated)\n"
            << "  -h, --help                Show this help message\n\n"
            << "Overrides:\n"
            << "  Provide override expressions like `trainer.max_epochs=100` "
               "or `+new.key=value`.\n"
            << "  Nested keys use dot-notation. Use backslash to escape dots "
               "in key names.\n"
            << "  Use overrides like `hydra.run.dir=null` to disable Hydra run "
               "directory creation.\n";
}

std::optional<fs::path> resolve_run_directory(const ConfigNode& config) {
  std::string template_value = "outputs/${now:%Y-%m-%d_%H-%M-%S}";
  if (const ConfigNode* node =
          hydra::find_path(config, {"hydra", "run", "dir"})) {
    if (node->is_null()) {
      return std::nullopt;
    }
    if (!node->is_string()) {
      throw std::runtime_error("hydra.run.dir must be a string or null");
    }
    template_value = node->as_string();
  }
  if (template_value.empty()) {
    return std::nullopt;
  }
  return fs::path(template_value);
}

void write_overrides_file(const fs::path& path,
                          const std::vector<std::string>& overrides) {
  ConfigNode data = hydra::make_sequence();
  auto& seq       = data.as_sequence();
  seq.reserve(overrides.size());
  for (const auto& expr : overrides) {
    seq.emplace_back(expr);
  }
  hydra::write_yaml_file(data, path);
}

void write_hydra_artifacts(const ConfigNode& config,
                           const std::vector<std::string>& overrides,
                           const std::optional<fs::path>& run_dir_opt) {
  if (!run_dir_opt) {
    std::cout << "# hydra.run.dir is null; skipping run directory creation\n";
    return;
  }

  const fs::path& run_dir = *run_dir_opt;
  std::error_code ec;
  fs::create_directories(run_dir, ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to create run directory '" << run_dir
        << "': " << ec.message();
    throw std::runtime_error(oss.str());
  }

  fs::path hydra_dir = run_dir / ".hydra";
  fs::create_directories(hydra_dir, ec);
  if (ec) {
    std::ostringstream oss;
    oss << "Failed to create Hydra metadata directory '" << hydra_dir
        << "': " << ec.message();
    throw std::runtime_error(oss.str());
  }

  hydra::write_yaml_file(config, hydra_dir / "config.yaml");

  if (const ConfigNode* hydra_node = hydra::find_path(config, {"hydra"})) {
    hydra::write_yaml_file(*hydra_node, hydra_dir / "hydra.yaml");
  }

  write_overrides_file(hydra_dir / "overrides.yaml", overrides);

  std::cout << "# Hydra run directory: " << run_dir << "\n"
            << "# Stored configuration: " << (hydra_dir / "config.yaml")
            << "\n";
}

void ensure_hydra_defaults(ConfigNode& config) {
  if (config.is_null()) {
    config = make_mapping();
  }
  if (!config.is_mapping()) {
    throw std::runtime_error("Root configuration is not a mapping");
  }

  auto& root_map = config.as_mapping();
  auto hydra_it  = root_map.find("hydra");
  if (hydra_it == root_map.end()) {
    ConfigNode hydra_node = make_mapping();
    ConfigNode run_node   = make_mapping();
    run_node.as_mapping().emplace(
        "dir", make_string("outputs/${now:%Y-%m-%d_%H-%M-%S}"));
    hydra_node.as_mapping().emplace("run", std::move(run_node));
    hydra_it = root_map.emplace("hydra", std::move(hydra_node)).first;
  } else if (!hydra_it->second.is_mapping()) {
    throw std::runtime_error("'hydra' key must be a mapping");
  }

  auto& hydra_map = hydra_it->second.as_mapping();
  auto run_it     = hydra_map.find("run");
  if (run_it == hydra_map.end()) {
    ConfigNode run_node = make_mapping();
    run_node.as_mapping().emplace(
        "dir", make_string("outputs/${now:%Y-%m-%d_%H-%M-%S}"));
    run_it = hydra_map.emplace("run", std::move(run_node)).first;
  } else if (!run_it->second.is_mapping()) {
    throw std::runtime_error("'hydra.run' must be a mapping");
  }

  auto& run_map = run_it->second.as_mapping();
  if (run_map.find("dir") == run_map.end()) {
    run_map.emplace("dir", make_string("outputs/${now:%Y-%m-%d_%H-%M-%S}"));
  }
}

Options parse_options(int argc, char** argv,
                      std::vector<std::string>& override_expressions) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      options.show_help = true;
      return options;
    } else if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        throw std::runtime_error("Missing argument for --config");
      }
      options.config_files.emplace_back(argv[++i]);
    } else if (!arg.empty() && arg.front() == '-') {
      std::ostringstream oss;
      oss << "Unknown option '" << arg << "'";
      throw std::runtime_error(oss.str());
    } else {
      override_expressions.push_back(arg);
    }
  }
  return options;
}

bool file_exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec);
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> override_expressions;
    Options options = parse_options(argc, argv, override_expressions);

    if (options.show_help) {
      print_usage();
      return 0;
    }

    if (options.config_files.empty()) {
      if (file_exists("config.yaml")) {
        options.config_files.emplace_back("config.yaml");
      } else {
        std::cerr << "Warning: no configuration files provided; starting from "
                     "empty mapping.\n";
      }
    }

    ConfigNode config = make_mapping();
    bool loaded_any   = false;
    for (const auto& path : options.config_files) {
      ConfigNode node = load_yaml_file(path);
      merge(config, node);
      loaded_any = true;
    }

    if (!loaded_any) {
      config = make_mapping();
    }

    ensure_hydra_defaults(config);

    for (const auto& expr : override_expressions) {
      Override ov = parse_override(expr);
      assign_path(config, ov.path, std::move(ov.value), ov.require_new);
    }

    resolve_interpolations(config);

    std::optional<fs::path> run_dir = resolve_run_directory(config);
    std::optional<fs::path> absolute_run_dir;

    if (run_dir) {
      fs::path abs     = fs::absolute(*run_dir);
      abs              = abs.lexically_normal();
      absolute_run_dir = abs;
      assign_path(config, {"hydra", "run", "dir"}, make_string(abs.string()),
                  false);
    } else {
      assign_path(config, {"hydra", "run", "dir"}, make_null(), false);
    }

    std::string rendered = to_yaml_string(config);
    std::cout << rendered;
    if (!rendered.empty() && rendered.back() != '\n') {
      std::cout << "\n";
    }

    write_hydra_artifacts(config, override_expressions, absolute_run_dir);

  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
