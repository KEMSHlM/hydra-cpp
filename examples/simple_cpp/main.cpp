#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/interpolation.hpp"
#include "hydra/logging.hpp"
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

struct DatabaseConfig {
  std::string host;
  int64_t port;
  std::string user;
};

struct ModelConfig {
  std::string name;
  int64_t depth;
  std::string activation;
};

struct TrainerConfig {
  int64_t batch_size;
  int64_t max_epochs;
};

struct ExperimentConfig {
  std::string name;
  std::string run_dir;
};

struct AppConfig {
  DatabaseConfig database;
  ModelConfig model;
  TrainerConfig trainer;
  ExperimentConfig experiment;
};

AppConfig bind_config(const hydra::ConfigNode& root) {
  AppConfig cfg;
  cfg.database.host = hydra::utils::expect_string(root, {"database", "host"});
  cfg.database.port = hydra::utils::expect_int(root, {"database", "port"});
  cfg.database.user = hydra::utils::expect_string(root, {"database", "user"});

  cfg.model.name  = hydra::utils::expect_string(root, {"model", "name"});
  cfg.model.depth = hydra::utils::expect_int(root, {"model", "depth"});
  cfg.model.activation =
      hydra::utils::expect_string(root, {"model", "activation"});

  cfg.trainer.batch_size =
      hydra::utils::expect_int(root, {"trainer", "batch_size"});
  cfg.trainer.max_epochs =
      hydra::utils::expect_int(root, {"trainer", "max_epochs"});

  cfg.experiment.name =
      hydra::utils::expect_string(root, {"experiment", "name"});
  cfg.experiment.run_dir =
      hydra::utils::expect_string(root, {"hydra", "run", "dir"});

  return cfg;
}

struct CliParseResult {
  std::vector<fs::path> config_files;
  std::vector<std::string> overrides;
};

CliParseResult parse_cli(int argc, char** argv) {
  CliParseResult result;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-c" || arg == "--config") {
      if (i + 1 >= argc) {
        throw std::runtime_error("--config requires an argument");
      }
      result.config_files.emplace_back(argv[++i]);
    } else if (arg.rfind("--config=", 0) == 0) {
      result.config_files.emplace_back(arg.substr(9));
    } else {
      result.overrides.emplace_back(std::move(arg));
    }
  }

  if (result.config_files.empty()) {
    result.config_files.emplace_back("configs/main.yaml");
  }
  return result;
}

int main(int argc, char** argv) try {
  CliParseResult cli = parse_cli(argc, argv);

  hydra::ConfigNode config = hydra::make_mapping();

  for (const auto& path : cli.config_files) {
    hydra::ConfigNode loaded = hydra::load_yaml_file(path);
    hydra::merge(config, loaded);
  }

  for (const auto& expr : cli.overrides) {
    hydra::Override ov = hydra::parse_override(expr);
    hydra::assign_path(config, ov.path, std::move(ov.value), ov.require_new);
  }

  hydra::resolve_interpolations(config);

  if (!hydra::utils::has_node(config, {"experiment", "name"})) {
    hydra::assign_path(config, {"experiment", "name"},
                       hydra::make_string("cpp_example"), true);
  }

  fs::path run_dir_path =
      hydra::utils::write_hydra_outputs(config, cli.overrides);

  // Initialize logging after run directory is created
  hydra::init_logging(config);

  AppConfig app = bind_config(config);

  log_info("=== hydra example (C++) ===");
  log_info("Experiment         : %s", app.experiment.name.c_str());
  log_info("Model              : %s (depth=%ld, activation=%s)",
           app.model.name.c_str(), app.model.depth,
           app.model.activation.c_str());
  log_info("Trainer            : batch_size=%ld, max_epochs=%ld",
           app.trainer.batch_size, app.trainer.max_epochs);
  log_debug("Database endpoint  : %s (port=%ld, user=%s)",
            app.database.host.c_str(), app.database.port,
            app.database.user.c_str());
  log_debug("hydra.run.dir      : %s", app.experiment.run_dir.c_str());

  log_info("--- simulated training job ---");
  const int64_t dataset_size = 512;
  const int64_t steps_per_epoch =
      (dataset_size + app.trainer.batch_size - 1) /
      (app.trainer.batch_size > 0 ? app.trainer.batch_size : 1);
  for (int epoch = 1; epoch <= app.trainer.max_epochs && epoch <= 3; ++epoch) {
    log_info("Epoch %d/%ld - running %ld steps", epoch,
             app.trainer.max_epochs, steps_per_epoch);
  }
  if (app.trainer.max_epochs > 3) {
    log_info("... (%ld more epochs omitted) ...",
             app.trainer.max_epochs - 3);
  }
  log_info("Training completed successfully");

  hydra::log_config(config);

  log_info("Hydra outputs written under %s/.hydra",
           run_dir_path.string().c_str());

  return 0;
} catch (const std::exception& ex) {
  std::cerr << "Error: " << ex.what() << "\n";
  return 1;
}
