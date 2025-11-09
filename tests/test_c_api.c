#include "hydra/c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#endif

static void assert_status(const char* step, hydra_status_t status,
                          const char* error) {
  if (status != HYDRA_STATUS_OK) {
    fprintf(stderr, "[FAIL] %s: %s\n", step, error ? error : "(unknown error)");
    hydra_string_free((char*)error);
    exit(1);
  }
  if (error != NULL) {
    hydra_string_free((char*)error);
  }
}

static int directory_exists(const char* path) {
#if defined(_WIN32)
  struct _stat info;
  if (_stat(path, &info) != 0) {
    return 0;
  }
  return (info.st_mode & _S_IFDIR) != 0;
#else
  struct stat info;
  if (stat(path, &info) != 0) {
    return 0;
  }
  return S_ISDIR(info.st_mode);
#endif
}

static void fail_with(const char* step, const char* message) {
  fprintf(stderr, "[FAIL] %s: %s\n", step, message);
  exit(1);
}

int main(void) {
  hydra_config_t* cfg = hydra_config_create();
  if (cfg == NULL) {
    fprintf(stderr, "[FAIL] Failed to create config\n");
    return 1;
  }

  char* error      = NULL;
  const char* yaml = "trainer:\n"
                     "  batch_size: 16\n"
                     "  max_epochs: 10\n"
                     "  tags:\n"
                     "    - baseline\n"
                     "    - sweep\n"
                     "plots:\n"
                     "  - field: acc\n"
                     "    title: Accuracy\n"
                     "  - field: loss\n"
                     "    title: Loss\n"
                     "params:\n"
                     "  alpha: 10\n"
                     "  beta: 20\n"
                     "visualization:\n"
                     "  layouts:\n"
                     "    primary: grid\n"
                     "output:\n"
                     "  data_dir: \"outputs/c_api_dir/subdir\"\n";

  assert_status("merge string",
                hydra_config_merge_string(cfg, yaml, "inline", &error), error);

  assert_status(
      "apply override",
      hydra_config_apply_override(cfg, "trainer.max_epochs=32", &error), error);

  int64_t epochs = 0;
  assert_status(
      "get int",
      hydra_config_get_int(cfg, "trainer.max_epochs", &epochs, &error), error);

  if (epochs != 32) {
    fprintf(stderr, "[FAIL] Expected max_epochs=32 but got %lld\n",
            (long long)epochs);
    hydra_config_destroy(cfg);
    return 1;
  }

  char* dump = hydra_config_to_yaml_string(cfg, &error);
  if (dump == NULL) {
    fprintf(stderr, "[FAIL] Failed to render config: %s\n",
            error ? error : "(unknown)");
    hydra_string_free(error);
    hydra_config_destroy(cfg);
    return 1;
  }

  if (strstr(dump, "max_epochs: 32") == NULL) {
    fprintf(stderr, "[FAIL] Rendered YAML missing override:\n%s\n", dump);
    hydra_string_free(dump);
    hydra_config_destroy(cfg);
    return 1;
  }
  hydra_string_free(dump);

  // Sequence iterator
  hydra_config_iter_t* seq_iter = NULL;
  assert_status("sequence iter init",
                hydra_config_sequence_iter(cfg, "plots", &seq_iter, &error),
                error);
  const char* expected_plot_paths[] = {"plots.0", "plots.1"};
  size_t expected_plot_count        = 2;
  size_t seen_plots                 = 0;
  while (1) {
    size_t iter_index = 0;
    char* path_out    = NULL;
    int rc = hydra_config_iter_next(seq_iter, &path_out, NULL, &iter_index,
                                    &error);
    if (rc == -1) {
      fprintf(stderr, "[FAIL] iter plots: %s\n",
              error ? error : "(unknown)");
      hydra_string_free(error);
      hydra_config_iter_destroy(seq_iter);
      hydra_config_destroy(cfg);
      return 1;
    }
    if (rc == 0) {
      break;
    }
    if (seen_plots >= expected_plot_count) {
      fail_with("iter plots", "too many elements");
    }
    if (strcmp(path_out, expected_plot_paths[seen_plots]) != 0) {
      fprintf(stderr, "[FAIL] iter plots path mismatch: %s vs %s\n", path_out,
              expected_plot_paths[seen_plots]);
      hydra_string_free(path_out);
      hydra_config_iter_destroy(seq_iter);
      hydra_config_destroy(cfg);
      return 1;
    }
    if (iter_index != seen_plots) {
      fprintf(stderr, "[FAIL] iter plots index mismatch: %zu vs %zu\n",
              iter_index, seen_plots);
      hydra_string_free(path_out);
      hydra_config_iter_destroy(seq_iter);
      hydra_config_destroy(cfg);
      return 1;
    }
    hydra_string_free(path_out);
    ++seen_plots;
  }
  if (seen_plots != expected_plot_count) {
    fail_with("iter plots", "missing elements");
  }
  hydra_config_iter_destroy(seq_iter);

  // Map iterator
  hydra_config_iter_t* map_iter = NULL;
  assert_status("map iter init",
                hydra_config_map_iter(cfg, "params", &map_iter, &error), error);
  const char* expected_keys[]  = {"alpha", "beta"};
  const char* expected_paths[] = {"params.alpha", "params.beta"};
  size_t seen_keys             = 0;
  while (1) {
    size_t iter_index = 0;
    char* path_out    = NULL;
    char* key_out     = NULL;
    int rc = hydra_config_iter_next(map_iter, &path_out, &key_out, &iter_index,
                                    &error);
    if (rc == -1) {
      fprintf(stderr, "[FAIL] iter params: %s\n",
              error ? error : "(unknown)");
      hydra_string_free(error);
      hydra_config_iter_destroy(map_iter);
      hydra_config_destroy(cfg);
      return 1;
    }
    if (rc == 0) {
      break;
    }
    if (seen_keys >= 2) {
      fail_with("iter params", "too many keys");
    }
    if (strcmp(key_out, expected_keys[seen_keys]) != 0 ||
        strcmp(path_out, expected_paths[seen_keys]) != 0) {
      fprintf(stderr, "[FAIL] iter params mismatch (%s, %s)\n", path_out,
              key_out);
      hydra_string_free(path_out);
      hydra_string_free(key_out);
      hydra_config_iter_destroy(map_iter);
      hydra_config_destroy(cfg);
      return 1;
    }
    hydra_string_free(path_out);
    hydra_string_free(key_out);
    ++seen_keys;
  }
  if (seen_keys != 2) {
    fail_with("iter params", "missing keys");
  }
  hydra_config_iter_destroy(map_iter);

  // Subnode copying
  hydra_config_t* layouts = NULL;
  assert_status("subnode",
                hydra_config_subnode(cfg, "visualization.layouts", &layouts,
                                     &error),
                error);
  char* layout_value = NULL;
  assert_status("subnode read",
                hydra_config_get_string(layouts, "primary", &layout_value,
                                        &error),
                error);
  if (strcmp(layout_value, "grid") != 0) {
    fprintf(stderr, "[FAIL] subnode primary mismatch: %s\n", layout_value);
    hydra_string_free(layout_value);
    hydra_config_destroy(layouts);
    hydra_config_destroy(cfg);
    return 1;
  }
  hydra_string_free(layout_value);
  hydra_config_destroy(layouts);

  // Clone helpers
  char* cloned = NULL;
  assert_status("clone string",
                hydra_config_clone_string(cfg, "visualization.layouts.primary",
                                          &cloned, &error),
                error);
  if (strcmp(cloned, "grid") != 0) {
    fprintf(stderr, "[FAIL] clone string mismatch: %s\n", cloned);
    hydra_string_free(cloned);
    hydra_config_destroy(cfg);
    return 1;
  }
  hydra_string_free(cloned);

  char** tags   = NULL;
  size_t count  = 0;
  assert_status("clone string list",
                hydra_config_clone_string_list(cfg, "trainer.tags", &tags,
                                                &count, &error),
                error);
  if (count != 2) {
    fprintf(stderr, "[FAIL] clone string list count mismatch: %zu\n", count);
    hydra_string_list_free(tags, count);
    hydra_config_destroy(cfg);
    return 1;
  }
  if (strcmp(tags[0], "baseline") != 0 || strcmp(tags[1], "sweep") != 0) {
    fprintf(stderr, "[FAIL] clone string list values mismatch\n");
    hydra_string_list_free(tags, count);
    hydra_config_destroy(cfg);
    return 1;
  }
  hydra_string_list_free(tags, count);

  // Directory helper
  assert_status("ensure directory",
                hydra_config_ensure_directory(cfg, "output.data_dir", &error),
                error);
  if (!directory_exists("outputs/c_api_dir") ||
      !directory_exists("outputs/c_api_dir/subdir")) {
    fail_with("ensure directory", "expected directory missing");
  }

  hydra_config_destroy(cfg);
  printf("[OK] hydra c api tests passed\n");
  return 0;
}
