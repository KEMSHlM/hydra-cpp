#include "hydra/config_node.hpp"
#include "hydra/config_utils.hpp"
#include "hydra/interpolation.hpp"
#include "hydra/logging.hpp"
#include "hydra/overrides.hpp"
#include "hydra/yaml_emitter.hpp"
#include "hydra/yaml_loader.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TestFailure : public std::runtime_error {
  TestFailure(const char* file, int line, const std::string& expr,
              const std::string& msg = {})
      : std::runtime_error(compose(file, line, expr, msg)) {
  }

  static std::string compose(const char* file, int line,
                             const std::string& expr, const std::string& msg) {
    std::string out = std::string(file) + ":" + std::to_string(line) +
                      " assertion failed: " + expr;
    if (!msg.empty()) {
      out += " (" + msg + ")";
    }
    return out;
  }
};

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      throw TestFailure(__FILE__, __LINE__, #expr);                            \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    auto _a = (a);                                                             \
    auto _b = (b);                                                             \
    if (!((_a) == (_b))) {                                                     \
      throw TestFailure(__FILE__, __LINE__, #a " == " #b,                      \
                        std::string("lhs=") + to_string(_a) +                  \
                            ", rhs=" + to_string(_b));                         \
    }                                                                          \
  } while (0)

std::string to_string(const std::string& value) {
  return value;
}
std::string to_string(const char* value) {
  return std::string(value);
}
std::string to_string(bool value) {
  return value ? "true" : "false";
}
template <typename T> std::string to_string(const T& value) {
  return std::to_string(value);
}

using TestFn = void (*)();

struct TestCase {
  const char* name;
  TestFn fn;
};

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Register {
  Register(const char* name, TestFn fn) {
    registry().push_back({name, fn});
  }
};

#define TEST_CASE(name)                                                        \
  void name();                                                                 \
  static Register name##_reg(#name, &name);                                    \
  void name()

std::string read_file(const fs::path& path) {
  std::ifstream in(path);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

fs::path create_temp_directory(const std::string& name) {
  fs::path dir = fs::temp_directory_path() / ("hydra_cpp_test_" + name);
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

} // namespace

TEST_CASE(override_parsing_basic) {
  hydra::Override ov = hydra::parse_override("trainer.max_epochs=42");
  ASSERT_EQ(ov.path.size(), static_cast<size_t>(2));
  ASSERT_EQ(ov.path[0], std::string("trainer"));
  ASSERT_EQ(ov.path[1], std::string("max_epochs"));
  ASSERT_TRUE(ov.value.is_int());
  ASSERT_EQ(ov.value.as_int(), static_cast<int64_t>(42));
  ASSERT_TRUE(!ov.require_new);
}

TEST_CASE(override_parsing_new_key) {
  hydra::Override ov = hydra::parse_override("+trainer.schedule=[1,2,3]");
  ASSERT_EQ(ov.path.size(), static_cast<size_t>(2));
  ASSERT_EQ(ov.path[0], std::string("trainer"));
  ASSERT_EQ(ov.path[1], std::string("schedule"));
  ASSERT_TRUE(ov.value.is_sequence());
  ASSERT_EQ(ov.value.as_sequence().size(), static_cast<size_t>(3));
  ASSERT_TRUE(ov.require_new);
}

TEST_CASE(assign_path_behaviour) {
  hydra::ConfigNode root = hydra::make_mapping();
  bool threw_missing     = false;
  try {
    hydra::assign_path(root, {"group", "value"}, hydra::make_int(12), false);
  } catch (const std::exception&) {
    threw_missing = true;
  }
  ASSERT_TRUE(threw_missing);

  hydra::assign_path(root, {"group", "value"}, hydra::make_int(12), true);
  const hydra::ConfigNode* found = hydra::find_path(root, {"group", "value"});
  ASSERT_TRUE(found != nullptr);
  ASSERT_TRUE(found->is_int());
  ASSERT_EQ(found->as_int(), static_cast<int64_t>(12));

  bool threw_duplicate = false;
  try {
    hydra::assign_path(root, {"group", "value"}, hydra::make_int(13), true);
  } catch (const std::exception&) {
    threw_duplicate = true;
  }
  ASSERT_TRUE(threw_duplicate);

  hydra::assign_path(root, {"group", "value"}, hydra::make_int(13), false);
  found = hydra::find_path(root, {"group", "value"});
  ASSERT_TRUE(found != nullptr);
  ASSERT_TRUE(found->is_int());
  ASSERT_EQ(found->as_int(), static_cast<int64_t>(13));
}

TEST_CASE(interpolation_resolves_paths) {
#ifdef _WIN32
  _putenv_s("HYDRA_CPP_TEST_ROOT", "");
#else
  unsetenv("HYDRA_CPP_TEST_ROOT");
#endif

  hydra::ConfigNode root = hydra::make_mapping();
  hydra::assign_path(root, {"paths", "root_dir"},
                     hydra::make_string("${oc.env:HYDRA_CPP_TEST_ROOT,.}"),
                     true);
  hydra::assign_path(root, {"paths", "base_output_dir"},
                     hydra::make_string("${paths.root_dir}/test_outputs"),
                     true);
  hydra::assign_path(root, {"hydra", "run", "dir"},
                     hydra::make_string("${paths.base_output_dir}/${now:%Y}"),
                     true);

  hydra::resolve_interpolations(root);

  const hydra::ConfigNode* root_dir =
      hydra::find_path(root, {"paths", "root_dir"});
  ASSERT_TRUE(root_dir != nullptr);
  ASSERT_TRUE(root_dir->is_string());
  ASSERT_EQ(root_dir->as_string(), std::string("."));

  const hydra::ConfigNode* base_output =
      hydra::find_path(root, {"paths", "base_output_dir"});
  ASSERT_TRUE(base_output != nullptr);
  ASSERT_TRUE(base_output->is_string());
  const std::string base_value = base_output->as_string();
  ASSERT_TRUE(base_value == "./test_outputs" ||
              base_value == ".//test_outputs");

  const hydra::ConfigNode* run_dir =
      hydra::find_path(root, {"hydra", "run", "dir"});
  ASSERT_TRUE(run_dir != nullptr);
  ASSERT_TRUE(run_dir->is_string());
  ASSERT_TRUE(run_dir->as_string().find("test_outputs") != std::string::npos);
}

TEST_CASE(interpolation_env_override) {
  const char* env_name = "HYDRA_CPP_TEST_OVERRIDE";
  const char* desired  = "/tmp/hydra_env_root";
  const char* previous = std::getenv(env_name);

#ifdef _WIN32
  if (_putenv_s(env_name, desired) != 0) {
    throw std::runtime_error("failed to set environment variable");
  }
#else
  if (setenv(env_name, desired, 1) != 0) {
    throw std::runtime_error("failed to set environment variable");
  }
#endif

  hydra::ConfigNode root = hydra::make_mapping();
  hydra::assign_path(
      root, {"paths", "root_dir"},
      hydra::make_string(std::string("${oc.env:") + env_name + ",.}"), true);

  hydra::resolve_interpolations(root);

  const hydra::ConfigNode* resolved =
      hydra::find_path(root, {"paths", "root_dir"});
  ASSERT_TRUE(resolved != nullptr);
  ASSERT_TRUE(resolved->is_string());
  ASSERT_EQ(resolved->as_string(), std::string(desired));

  if (previous) {
#ifdef _WIN32
    _putenv_s(env_name, previous);
#else
    setenv(env_name, previous, 1);
#endif
  } else {
#ifdef _WIN32
    _putenv_s(env_name, "");
#else
    unsetenv(env_name);
#endif
  }
}

TEST_CASE(defaults_include_merging) {
  fs::path dir = create_temp_directory("defaults");
  fs::create_directories(dir / "database");
  fs::create_directories(dir / "model");

  {
    std::ofstream out(dir / "main.yaml");
    out << "defaults:\n"
           "  - database: postgres\n"
           "  - model: resnet\n"
           "\n"
           "trainer:\n"
           "  batch_size: 16\n";
  }
  {
    std::ofstream out(dir / "database" / "postgres.yaml");
    out << "driver: postgres\n"
           "host: localhost\n"
           "port: 5432\n";
  }
  {
    std::ofstream out(dir / "model" / "resnet.yaml");
    out << "name: resnet\n"
           "depth: 50\n";
  }

  hydra::ConfigNode config    = hydra::load_yaml_file(dir / "main.yaml");
  const hydra::ConfigNode* db = hydra::find_path(config, {"database", "host"});
  ASSERT_TRUE(db != nullptr);
  ASSERT_TRUE(db->is_string());
  ASSERT_EQ(db->as_string(), std::string("localhost"));

  const hydra::ConfigNode* depth = hydra::find_path(config, {"model", "depth"});
  ASSERT_TRUE(depth != nullptr);
  ASSERT_TRUE(depth->is_int());
  ASSERT_EQ(depth->as_int(), static_cast<int64_t>(50));

  const hydra::ConfigNode* batch =
      hydra::find_path(config, {"trainer", "batch_size"});
  ASSERT_TRUE(batch != nullptr);
  ASSERT_TRUE(batch->is_int());
  ASSERT_EQ(batch->as_int(), static_cast<int64_t>(16));

  fs::remove_all(dir);
}

TEST_CASE(yaml_emission_round_trip) {
  hydra::ConfigNode root = hydra::make_mapping();
  hydra::assign_path(root, {"numbers"}, hydra::make_sequence(), true);
  auto& seq = root.as_mapping().at("numbers").as_sequence();
  seq.push_back(hydra::make_int(1));
  seq.push_back(hydra::make_int(2));

  std::string emitted      = hydra::to_yaml_string(root);
  hydra::ConfigNode reload = hydra::load_yaml_string(emitted, "<emitted>");
  const hydra::ConfigNode* numbers = hydra::find_path(reload, {"numbers"});
  ASSERT_TRUE(numbers != nullptr);
  ASSERT_TRUE(numbers->is_sequence());
  ASSERT_EQ(numbers->as_sequence().size(), static_cast<size_t>(2));
}

TEST_CASE(logging_level_debug) {
  fs::path config_path = "../../tests/configs/logging/level_debug.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  hydra::ConfigNode config = hydra::load_yaml_file(config_path);
  hydra::resolve_interpolations(config);

  const hydra::ConfigNode* level =
      hydra::find_path(config, {"hydra", "job_logging", "root", "level"});
  ASSERT_TRUE(level != nullptr);
  ASSERT_TRUE(level->is_string());
  ASSERT_EQ(level->as_string(), std::string("DEBUG"));

  const hydra::ConfigNode* handlers =
      hydra::find_path(config, {"hydra", "job_logging", "root", "handlers"});
  ASSERT_TRUE(handlers != nullptr);
  ASSERT_TRUE(handlers->is_sequence());
  ASSERT_EQ(handlers->as_sequence().size(), static_cast<size_t>(2));
}

TEST_CASE(logging_console_only) {
  fs::path config_path = "../../tests/configs/logging/console_only.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  hydra::ConfigNode config = hydra::load_yaml_file(config_path);
  hydra::resolve_interpolations(config);

  const hydra::ConfigNode* handlers =
      hydra::find_path(config, {"hydra", "job_logging", "root", "handlers"});
  ASSERT_TRUE(handlers != nullptr);
  ASSERT_TRUE(handlers->is_sequence());
  ASSERT_EQ(handlers->as_sequence().size(), static_cast<size_t>(1));
  ASSERT_EQ(handlers->as_sequence()[0].as_string(), std::string("console"));
}

TEST_CASE(logging_file_only) {
  fs::path config_path = "../../tests/configs/logging/file_only.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  hydra::ConfigNode config = hydra::load_yaml_file(config_path);
  hydra::resolve_interpolations(config);

  const hydra::ConfigNode* handlers =
      hydra::find_path(config, {"hydra", "job_logging", "root", "handlers"});
  ASSERT_TRUE(handlers != nullptr);
  ASSERT_TRUE(handlers->is_sequence());
  ASSERT_EQ(handlers->as_sequence().size(), static_cast<size_t>(1));
  ASSERT_EQ(handlers->as_sequence()[0].as_string(), std::string("file"));

  const hydra::ConfigNode* filename = hydra::find_path(
      config, {"hydra", "job_logging", "handlers", "file", "filename"});
  ASSERT_TRUE(filename != nullptr);
  ASSERT_TRUE(filename->is_string());
}

TEST_CASE(integration_simple_config) {
  fs::path config_path = "../../tests/configs/integration/simple.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  hydra::ConfigNode config = hydra::load_yaml_file(config_path);
  hydra::resolve_interpolations(config);

  const hydra::ConfigNode* model_name =
      hydra::find_path(config, {"model", "name"});
  ASSERT_TRUE(model_name != nullptr);
  ASSERT_TRUE(model_name->is_string());
  ASSERT_EQ(model_name->as_string(), std::string("resnet"));

  const hydra::ConfigNode* batch_size =
      hydra::find_path(config, {"trainer", "batch_size"});
  ASSERT_TRUE(batch_size != nullptr);
  ASSERT_TRUE(batch_size->is_int());
  ASSERT_EQ(batch_size->as_int(), static_cast<int64_t>(32));
}

TEST_CASE(integration_env_variables) {
  fs::path config_path = "../../tests/configs/integration/with_env.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

#ifdef _WIN32
  _putenv_s("TEST_OUTPUT_DIR", "/tmp/test_hydra");
  _putenv_s("DB_HOST", "testdb.example.com");
  _putenv_s("MODEL_NAME", "efficientnet");
  _putenv_s("BATCH_SIZE", "128");
#else
  setenv("TEST_OUTPUT_DIR", "/tmp/test_hydra", 1);
  setenv("DB_HOST", "testdb.example.com", 1);
  setenv("MODEL_NAME", "efficientnet", 1);
  setenv("BATCH_SIZE", "128", 1);
#endif

  hydra::ConfigNode config = hydra::load_yaml_file(config_path);
  hydra::resolve_interpolations(config);

  const hydra::ConfigNode* db_host =
      hydra::find_path(config, {"database", "host"});
  ASSERT_TRUE(db_host != nullptr);
  ASSERT_TRUE(db_host->is_string());
  ASSERT_EQ(db_host->as_string(), std::string("testdb.example.com"));

  const hydra::ConfigNode* model_name =
      hydra::find_path(config, {"model", "name"});
  ASSERT_TRUE(model_name != nullptr);
  ASSERT_TRUE(model_name->is_string());
  ASSERT_EQ(model_name->as_string(), std::string("efficientnet"));

  const hydra::ConfigNode* batch_size =
      hydra::find_path(config, {"trainer", "batch_size"});
  ASSERT_TRUE(batch_size != nullptr);
  // Environment variables are interpolated as strings
  ASSERT_TRUE(batch_size->is_string());
  ASSERT_EQ(batch_size->as_string(), std::string("128"));

#ifdef _WIN32
  _putenv_s("TEST_OUTPUT_DIR", "");
  _putenv_s("DB_HOST", "");
  _putenv_s("MODEL_NAME", "");
  _putenv_s("BATCH_SIZE", "");
#else
  unsetenv("TEST_OUTPUT_DIR");
  unsetenv("DB_HOST");
  unsetenv("MODEL_NAME");
  unsetenv("BATCH_SIZE");
#endif
}

TEST_CASE(utils_initialize_basic) {
  fs::path config_path = "../../tests/configs/integration/simple.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  const char* argv[]       = {"test_program", nullptr};
  hydra::ConfigNode config = hydra::utils::initialize(
      1, const_cast<char**>(argv), config_path.string());

  // Check job name was auto-derived
  const hydra::ConfigNode* job_name =
      hydra::find_path(config, {"hydra", "job", "name"});
  ASSERT_TRUE(job_name != nullptr);
  ASSERT_TRUE(job_name->is_string());
  ASSERT_EQ(job_name->as_string(), std::string("test_program"));

  // Check model config
  const hydra::ConfigNode* model_name =
      hydra::find_path(config, {"model", "name"});
  ASSERT_TRUE(model_name != nullptr);
  ASSERT_TRUE(model_name->is_string());
  ASSERT_EQ(model_name->as_string(), std::string("resnet"));
}

TEST_CASE(utils_initialize_with_overrides) {
  fs::path config_path = "../../tests/configs/integration/simple.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  const char* argv[]       = {"test_program", "trainer.batch_size=64",
                              "model.depth=101", nullptr};
  hydra::ConfigNode config = hydra::utils::initialize(
      3, const_cast<char**>(argv), config_path.string());

  const hydra::ConfigNode* batch_size =
      hydra::find_path(config, {"trainer", "batch_size"});
  ASSERT_TRUE(batch_size != nullptr);
  ASSERT_TRUE(batch_size->is_int());
  ASSERT_EQ(batch_size->as_int(), static_cast<int64_t>(64));

  const hydra::ConfigNode* depth = hydra::find_path(config, {"model", "depth"});
  ASSERT_TRUE(depth != nullptr);
  ASSERT_TRUE(depth->is_int());
  ASSERT_EQ(depth->as_int(), static_cast<int64_t>(101));
}

TEST_CASE(utils_write_hydra_outputs) {
  fs::path config_path = "../../tests/configs/integration/simple.yaml";
  if (!fs::exists(config_path)) {
    return;
  }

  const char* argv[]       = {"test_program", nullptr};
  hydra::ConfigNode config = hydra::utils::initialize(
      1, const_cast<char**>(argv), config_path.string());

  std::vector<std::string> overrides;
  fs::path run_dir = hydra::utils::write_hydra_outputs(config, overrides);

  // Check that run directory was created
  ASSERT_TRUE(fs::exists(run_dir));
  ASSERT_TRUE(fs::is_directory(run_dir));

  // Check that .hydra subdirectory exists
  fs::path hydra_dir = run_dir / ".hydra";
  ASSERT_TRUE(fs::exists(hydra_dir));
  ASSERT_TRUE(fs::is_directory(hydra_dir));

  // Check that config.yaml exists
  fs::path config_file = hydra_dir / "config.yaml";
  ASSERT_TRUE(fs::exists(config_file));
  ASSERT_TRUE(fs::is_regular_file(config_file));

  // Clean up
  fs::remove_all(run_dir);
}

int main() {
  int failures = 0;
  for (const auto& test : registry()) {
    try {
      test.fn();
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << "\n";
    }
  }
  if (failures == 0) {
    std::cout << "[OK] " << registry().size() << " tests passed\n";
    return 0;
  }
  std::cerr << failures << " test(s) failed\n";
  return 1;
}
