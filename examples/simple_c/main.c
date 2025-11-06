#include "hydra/c_api.h"
#include "hydra/c_api_utils.h"
#include "hydra/logging.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void exit_on_error(const char* step, hydra_status_t status, char* err) {
  if (status == HYDRA_STATUS_OK) {
    return;
  }
  fprintf(stderr, "[hydra-example] %s failed: %s\n", step,
          err ? err : "(unknown error)");
  hydra_string_free(err);
  exit(EXIT_FAILURE);
}

static void ensure_experiment_name(hydra_config_t* cfg) {
  char* err = NULL;
  if (!hydra_config_has(cfg, "experiment.name")) {
    exit_on_error("set default experiment",
                  hydra_config_apply_override(
                      cfg, "+experiment.name=\"c_example\"", &err),
                  err);
  }
}

typedef struct {
  char* host;
  char* user;
} DatabaseConfig;

typedef struct {
  char* name;
  int64_t depth;
  char* activation;
} ModelConfig;

typedef struct {
  int64_t batch_size;
  int64_t max_epochs;
} TrainerConfig;

typedef struct {
  char* name;
  char* run_dir;
} ExperimentConfig;

typedef struct {
  DatabaseConfig database;
  ModelConfig model;
  TrainerConfig trainer;
  ExperimentConfig experiment;
} AppConfig;

static AppConfig load_app_config(hydra_config_t* cfg) {
  ensure_experiment_name(cfg);

  AppConfig config = {
      .database =
          {
              .host = hydra_config_expect_string(cfg, "database.host"),
              .user = hydra_config_expect_string(cfg, "database.user"),
          },
      .model =
          {
              .name       = hydra_config_expect_string(cfg, "model.name"),
              .depth      = hydra_config_expect_int(cfg, "model.depth"),
              .activation = hydra_config_expect_string(cfg, "model.activation"),
          },
      .trainer =
          {
              .batch_size = hydra_config_expect_int(cfg, "trainer.batch_size"),
              .max_epochs = hydra_config_expect_int(cfg, "trainer.max_epochs"),
          },
      .experiment =
          {
              .name    = hydra_config_expect_string(cfg, "experiment.name"),
              .run_dir = hydra_config_expect_string(cfg, "hydra.run.dir"),
          },
  };

  return config;
}

static void free_app_config(AppConfig* config) {
  hydra_string_free(config->database.host);
  hydra_string_free(config->database.user);
  hydra_string_free(config->model.name);
  hydra_string_free(config->model.activation);
  hydra_string_free(config->experiment.name);
  hydra_string_free(config->experiment.run_dir);
}

static void print_config_summary(const AppConfig* config) {
  log_info("=== hydra example (C API) ===");
  log_info("Experiment         : %s", config->experiment.name);
  log_info("Model              : %s (depth=%" PRId64 ", activation=%s)",
           config->model.name, config->model.depth, config->model.activation);
  log_info("Trainer            : batch_size=%" PRId64 ", max_epochs=%" PRId64,
           config->trainer.batch_size, config->trainer.max_epochs);
  log_debug("Database endpoint  : %s (user=%s)", config->database.host,
            config->database.user);
  log_debug("hydra.run.dir      : %s", config->experiment.run_dir);
}

static void simulate_training_job(const AppConfig* config) {
  const int64_t dataset_size = 512;
  const int64_t steps_per_epoch =
      (dataset_size + config->trainer.batch_size - 1) /
      (config->trainer.batch_size > 0 ? config->trainer.batch_size : 1);

  log_info("--- simulated training job ---");
  for (int64_t epoch = 1; epoch <= config->trainer.max_epochs && epoch <= 3;
       ++epoch) {
    log_info("Epoch %" PRId64 "/%" PRId64 " - running %" PRId64 " steps", epoch,
             config->trainer.max_epochs, steps_per_epoch);
  }
  if (config->trainer.max_epochs > 3) {
    log_info("... (%" PRId64 " more epochs omitted) ...",
             config->trainer.max_epochs - 3);
  }
  log_info("Training completed successfully");
}

int main(int argc, char** argv) {
  char* err = NULL;

  /* Initialize Hydra configuration (loads config, applies overrides, resolves
   * interpolations) */
  hydra_config_t* cfg = hydra_initialize(argc, argv, "configs/main.yaml", &err);
  exit_on_error("initialize Hydra", cfg ? HYDRA_STATUS_OK : HYDRA_STATUS_ERROR,
                err);
  err = NULL;

  /* Set default experiment name if not specified */
  ensure_experiment_name(cfg);

  /* Write Hydra outputs (.hydra directory with configs) */
  char* run_dir_path = NULL;
  exit_on_error("write hydra outputs",
                hydra_write_outputs(cfg, NULL, 0, &run_dir_path, &err), err);
  err = NULL;

  /* Initialize logging (console + file based on config) */
  exit_on_error("initialize logging", hydra_init_logging(cfg, &err), err);
  err = NULL;

  AppConfig app = load_app_config(cfg);

  print_config_summary(&app);
  simulate_training_job(&app);

  /* Dump resolved configuration for inspection */
  exit_on_error("log config", hydra_log_config(cfg, &err), err);
  if (run_dir_path != NULL) {
    log_info("Hydra outputs written under %s/.hydra", run_dir_path);
    hydra_string_free(run_dir_path);
  }

  free_app_config(&app);
  hydra_config_destroy(cfg);

  return EXIT_SUCCESS;
}
