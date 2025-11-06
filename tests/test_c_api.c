#include "hydra/c_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_status(const char* step, hydra_status_t status,
                          const char* error) {
  if (status != HYDRA_STATUS_OK) {
    fprintf(stderr, "%s failed: %s\n", step, error ? error : "(unknown error)");
    hydra_string_free((char*)error);
    exit(1);
  }
  if (error != NULL) {
    hydra_string_free((char*)error);
  }
}

int main(void) {
  hydra_config_t* cfg = hydra_config_create();
  if (cfg == NULL) {
    fprintf(stderr, "Failed to create config\n");
    return 1;
  }

  char* error      = NULL;
  const char* yaml = "trainer:\n"
                     "  batch_size: 16\n"
                     "  max_epochs: 10\n";

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
    fprintf(stderr, "Expected max_epochs=32 but got %lld\n", (long long)epochs);
    hydra_config_destroy(cfg);
    return 1;
  }

  char* dump = hydra_config_to_yaml_string(cfg, &error);
  if (dump == NULL) {
    fprintf(stderr, "Failed to render config: %s\n",
            error ? error : "(unknown)");
    hydra_string_free(error);
    hydra_config_destroy(cfg);
    return 1;
  }

  if (strstr(dump, "max_epochs: 32") == NULL) {
    fprintf(stderr, "Rendered YAML missing override:\n%s\n", dump);
    hydra_string_free(dump);
    hydra_config_destroy(cfg);
    return 1;
  }

  hydra_string_free(dump);
  hydra_config_destroy(cfg);
  return 0;
}
