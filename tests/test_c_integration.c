#include "hydra/c_api.h"
#include "hydra/c_api_utils.h"
#include "hydra/logging.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define rmdir _rmdir
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

typedef struct {
  const char* name;
  void (*fn)(void);
} test_case_t;

static int test_failures = 0;

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      fprintf(stderr, "[FAIL] %s:%d: assertion failed: %s\n", __FILE__,        \
              __LINE__, #expr);                                                \
      test_failures++;                                                         \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ_INT(a, b)                                                    \
  do {                                                                         \
    int64_t _a = (a);                                                          \
    int64_t _b = (b);                                                          \
    if (_a != _b) {                                                            \
      fprintf(stderr, "[FAIL] %s:%d: %s == %s failed: %lld != %lld\n",         \
              __FILE__, __LINE__, #a, #b, (long long)_a, (long long)_b);       \
      test_failures++;                                                         \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ_STR(a, b)                                                    \
  do {                                                                         \
    const char* _a = (a);                                                      \
    const char* _b = (b);                                                      \
    if (strcmp(_a, _b) != 0) {                                                 \
      fprintf(stderr, "[FAIL] %s:%d: %s == %s failed: \"%s\" != \"%s\"\n",     \
              __FILE__, __LINE__, #a, #b, _a, _b);                             \
      test_failures++;                                                         \
      return;                                                                  \
    }                                                                          \
  } while (0)

static int file_exists(const char* path) {
#ifdef _WIN32
  struct _stat st;
  return _stat(path, &st) == 0;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

static void test_hydra_initialize_basic(void) {
  const char* config_path = "../../tests/configs/integration/simple.yaml";
  if (!file_exists(config_path)) {
    return;
  }

  char* err           = NULL;
  const char* argv[]  = {"test_program", NULL};
  hydra_config_t* cfg = hydra_initialize(1, (char**)argv, config_path, &err);

  ASSERT_TRUE(cfg != NULL);
  if (err != NULL) {
    hydra_string_free(err);
  }

  // Check job name was auto-derived
  char* job_name = NULL;
  hydra_status_t status =
      hydra_config_get_string(cfg, "hydra.job.name", &job_name, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_TRUE(job_name != NULL);
  ASSERT_EQ_STR(job_name, "test_program");
  hydra_string_free(job_name);

  // Check model config
  char* model_name = NULL;
  status = hydra_config_get_string(cfg, "model.name", &model_name, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_TRUE(model_name != NULL);
  ASSERT_EQ_STR(model_name, "resnet");
  hydra_string_free(model_name);

  int64_t depth = 0;
  status        = hydra_config_get_int(cfg, "model.depth", &depth, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_EQ_INT(depth, 50);

  hydra_config_destroy(cfg);
}

static void test_hydra_initialize_with_overrides(void) {
  const char* config_path = "../../tests/configs/integration/simple.yaml";
  if (!file_exists(config_path)) {
    return;
  }

  char* err           = NULL;
  const char* argv[]  = {"test_program", "trainer.batch_size=64", NULL};
  hydra_config_t* cfg = hydra_initialize(2, (char**)argv, config_path, &err);

  ASSERT_TRUE(cfg != NULL);
  if (err != NULL) {
    hydra_string_free(err);
  }

  int64_t batch_size = 0;
  hydra_status_t status =
      hydra_config_get_int(cfg, "trainer.batch_size", &batch_size, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_EQ_INT(batch_size, 64);

  hydra_config_destroy(cfg);
}

static void test_hydra_write_outputs(void) {
  const char* config_path = "../../tests/configs/integration/simple.yaml";
  if (!file_exists(config_path)) {
    return;
  }

  char* err           = NULL;
  const char* argv[]  = {"test_program", NULL};
  hydra_config_t* cfg = hydra_initialize(1, (char**)argv, config_path, &err);

  ASSERT_TRUE(cfg != NULL);
  if (err != NULL) {
    hydra_string_free(err);
    err = NULL;
  }

  char* run_dir         = NULL;
  hydra_status_t status = hydra_write_outputs(cfg, NULL, 0, &run_dir, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_TRUE(run_dir != NULL);

  // Check that run directory exists
  ASSERT_TRUE(file_exists(run_dir));

  // Build .hydra subdirectory path
  char hydra_dir[512];
  snprintf(hydra_dir, sizeof(hydra_dir), "%s/.hydra", run_dir);
  ASSERT_TRUE(file_exists(hydra_dir));

  // Build config.yaml path
  char config_file[512];
  snprintf(config_file, sizeof(config_file), "%s/config.yaml", hydra_dir);
  ASSERT_TRUE(file_exists(config_file));

  hydra_string_free(run_dir);
  hydra_config_destroy(cfg);
}

static void test_logging_level_config(void) {
  const char* config_path = "../../tests/configs/logging/level_debug.yaml";
  if (!file_exists(config_path)) {
    return;
  }

  char* err           = NULL;
  const char* argv[]  = {"test_program", NULL};
  hydra_config_t* cfg = hydra_initialize(1, (char**)argv, config_path, &err);

  ASSERT_TRUE(cfg != NULL);
  if (err != NULL) {
    hydra_string_free(err);
    err = NULL;
  }

  // Check log level
  char* level           = NULL;
  hydra_status_t status = hydra_config_get_string(
      cfg, "hydra.job_logging.root.level", &level, &err);
  ASSERT_TRUE(status == HYDRA_STATUS_OK);
  ASSERT_TRUE(level != NULL);
  ASSERT_EQ_STR(level, "DEBUG");
  hydra_string_free(level);

  hydra_config_destroy(cfg);
}

static void test_config_expect_helpers(void) {
  const char* config_path = "../../tests/configs/integration/simple.yaml";
  if (!file_exists(config_path)) {
    return;
  }

  char* err           = NULL;
  const char* argv[]  = {"test_program", NULL};
  hydra_config_t* cfg = hydra_initialize(1, (char**)argv, config_path, &err);

  ASSERT_TRUE(cfg != NULL);
  if (err != NULL) {
    hydra_string_free(err);
  }

  // Test expect_int
  int64_t batch_size = hydra_config_expect_int(cfg, "trainer.batch_size");
  ASSERT_EQ_INT(batch_size, 32);

  // Test expect_string
  char* model_name = hydra_config_expect_string(cfg, "model.name");
  ASSERT_TRUE(model_name != NULL);
  ASSERT_EQ_STR(model_name, "resnet");
  hydra_string_free(model_name);

  // Test expect_double
  double lr = hydra_config_expect_double(cfg, "trainer.learning_rate");
  ASSERT_TRUE(lr > 0.0009 && lr < 0.0011); // Check approximately equal to 0.001

  hydra_config_destroy(cfg);
}

int main(void) {
  test_case_t tests[] = {
      {"hydra_initialize_basic", test_hydra_initialize_basic},
      {"hydra_initialize_with_overrides", test_hydra_initialize_with_overrides},
      {"hydra_write_outputs", test_hydra_write_outputs},
      {"logging_level_config", test_logging_level_config},
      {"config_expect_helpers", test_config_expect_helpers},
      {NULL, NULL}};

  int total = 0;
  for (test_case_t* t = tests; t->name != NULL; ++t) {
    t->fn();
    total++;
  }

  if (test_failures == 0) {
    printf("[OK] %d tests passed\n", total);
    return 0;
  }

  fprintf(stderr, "%d test(s) failed\n", test_failures);
  return 1;
}
