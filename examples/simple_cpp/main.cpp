#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/logging.hpp"

#include <cinttypes>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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

int main(int argc, char** argv) try {
  // Initialize Hydra configuration (loads config, applies overrides, resolves interpolations)
  hydra::ConfigNode config = hydra::utils::initialize(argc, argv);

  // Set default experiment name if not specified
  if (!hydra::utils::has_node(config, {"experiment", "name"})) {
    hydra::assign_path(config, {"experiment", "name"},
                       hydra::make_string("cpp_example"), true);
  }

  // Write Hydra outputs (.hydra directory with configs)
  fs::path run_dir_path = hydra::utils::write_hydra_outputs(config, {});

  // Initialize logging (console + file based on config)
  hydra::init_logging(config);

  AppConfig app = bind_config(config);

  log_info("=== hydra example (C++) ===");
  log_info("Experiment         : %s", app.experiment.name.c_str());
  log_info("Model              : %s (depth=%" PRId64 ", activation=%s)",
           app.model.name.c_str(), app.model.depth,
           app.model.activation.c_str());
  log_info("Trainer            : batch_size=%" PRId64 ", max_epochs=%" PRId64,
           app.trainer.batch_size, app.trainer.max_epochs);
  log_debug("Database endpoint  : %s (port=%" PRId64 ", user=%s)",
            app.database.host.c_str(), app.database.port,
            app.database.user.c_str());
  log_debug("hydra.run.dir      : %s", app.experiment.run_dir.c_str());

  log_info("--- simulated training job ---");
  const int64_t dataset_size = 512;
  const int64_t steps_per_epoch =
      (dataset_size + app.trainer.batch_size - 1) /
      (app.trainer.batch_size > 0 ? app.trainer.batch_size : 1);
  for (int epoch = 1; epoch <= app.trainer.max_epochs && epoch <= 3; ++epoch) {
    log_info("Epoch %d/%" PRId64 " - running %" PRId64 " steps", epoch,
             app.trainer.max_epochs, steps_per_epoch);
  }
  if (app.trainer.max_epochs > 3) {
    log_info("... (%" PRId64 " more epochs omitted) ...",
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
