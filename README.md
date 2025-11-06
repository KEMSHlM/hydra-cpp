# Hydra C++ (hydra-cpp)

[English](#english) | [日本語](#日本語)

---

## English

### Overview

`hydra-cpp` is a lightweight configuration orchestration tool for C/C++ inspired by [Hydra](https://github.com/facebookresearch/hydra). It loads hierarchical YAML configurations, supports Hydra-style `defaults` composition, command-line overrides, interpolation (`${path.to.value}`, `${oc.env:VAR,default}`, `${now:%Y-%m-%d}`), and writes resolved configs into timestamped run directories. A plain-C API is available for embedding into C projects.

### Features

- YAML configuration loading and deep merge with libyaml (`yaml-0.1`)
- Hydra `defaults` resolution with group placement (e.g., `paths: default`)
- CLI overrides with strict `+new.key=value` semantics for new parameters
- Runtime interpolation for config references, environment variables, and timestamps
- Run directory management (`hydra.run.dir`) mirroring Hydra output layout (`.hydra/config.yaml`, `hydra.yaml`, `overrides.yaml`)
- C API (`include/hydra/c_api.h`) for non-C++ consumers
- CLI helper (`hydra_config_apply_cli`) to mirror Hydra-style `--config/-c` and override parsing in C
- Convenience binding helpers (`hydra/c_api_utils.h`, `hydra/config_utils.hpp`) to extract strongly-typed values easily
- Integrated logging system powered by [rxi/log.c](https://github.com/rxi/log.c) with automatic configuration from `hydra.job_logging.root.level`

### Quick Start

```bash
cmake -S . -B build
cmake --build build
./build/hydra-cpp --config configs/main.yaml model.depth=30
./build/hydra-cpp --config configs/main.yaml +experiment.name=demo
```

Resolved configuration is printed to stdout and saved under the run directory
(`hydra.run.dir`, default: `outputs/${now:%Y-%m-%d_%H-%M-%S}`).
Override new keys with `+` (e.g., `+new.group.value=1`).

### Configuration Layout

- `configs/main.yaml` – entry point with `defaults` specifying component configs
- `configs/{group}/{name}.yaml` – group configs merged into the corresponding namespace
- `configs/hydra/default.yaml`, `configs/paths/default.yaml` – Hydra runtime settings that remain editable via overrides (`hydra.run.dir=.` etc.)

Interpolation fallback order:

1. `${path.to.node}` – other config values
2. `${oc.env:VAR,default}` – environment variable with optional fallback
3. `${now:%...}` – timestamp (strftime format)

### C API Usage

```c
#include "hydra/c_api.h"
#include "hydra/c_api_utils.h"

hydra_config_t *cfg = hydra_config_create();
char *err = NULL;
hydra_cli_overrides_t overrides = {0};

hydra_config_apply_cli(cfg, argc, argv, "configs/main.yaml", &overrides, &err);

int64_t depth = hydra_config_expect_int(cfg, "model.depth");

hydra_config_finalize_run(cfg,
                          (const char *const *)overrides.items,
                          overrides.count,
                          NULL,
                          &err);
hydra_cli_overrides_free(&overrides);

hydra_config_destroy(cfg);
```

### C++ API Usage

```cpp
#include "hydra/config_utils.hpp"
#include "hydra/interpolation.hpp"
#include "hydra/overrides.hpp"
#include "hydra/yaml_loader.hpp"

int main(int argc, char** argv) {
    hydra::ConfigNode config = hydra::make_mapping();
    hydra::ConfigNode file_cfg = hydra::load_yaml_file("configs/main.yaml");
    hydra::merge(config, file_cfg);

    for (int i = 1; i < argc; ++i) {
        hydra::Override ov = hydra::parse_override(argv[i]);
        hydra::assign_path(config, ov.path, std::move(ov.value), ov.require_new);
    }

    hydra::resolve_interpolations(config);

    std::vector<std::string> overrides(argv + 1, argv + argc);
    std::filesystem::path run_dir = hydra::utils::write_hydra_outputs(config, overrides);

    std::string model = hydra::utils::expect_string(config, {"model", "name"});
    int64_t batch = hydra::utils::expect_int(config, {"trainer", "batch_size"});

    std::cout << "Model: " << model << " (batch=" << batch << ")\n";
    std::cout << "(Hydra outputs under " << run_dir << "/.hydra)\n";
}
```

#### Build the C Example

```bash
cmake --build build --target hydra-c-example
./build/hydra-c-example --config configs/main.yaml trainer.batch_size=5

cmake --build build --target hydra-cpp-example
./build/hydra-cpp-example --config configs/main.yaml trainer.batch_size=12
```

Intermediates (overrides, merges) trigger interpolation just before reads, so returned values are always resolved. The bundled examples (`hydra-c-example`, `hydra-cpp-example`) demonstrate realistic usage by binding configuration data into domain structs, validating required keys via `hydra_config_expect_*` / `hydra::utils::expect_*`, and simulating a small workload.

### Tests

```bash
ctest --test-dir build --output-on-failure
```

Unit tests cover override parsing, defaults composition, interpolation (including environment, timestamps), command-line behavior, and C API integration.

### Development Tips

- The CLI enforces Hydra semantics: existing keys updated with `key=value`, new keys require `+key=value`.
- To disable run directory creation for a run: `./build/hydra-cpp ... hydra.run.dir=null`.

### Acknowledgments

This project uses [rxi/log.c](https://github.com/rxi/log.c) for logging (MIT License).

### Contributing

If you have suggestions or improvements, please open a pull request.

---

## 日本語

### 概要

`hydra-cpp` は Hydra から着想を得た C/C++ 向けの軽量設定管理ツールです。階層的 YAML 設定のロードと `defaults` 連結、コマンドライン上書き、補間 (`${path.to.value}`, `${oc.env:VAR,default}`, `${now:%Y-%m-%d}`) を提供し、展開済み設定を実行ディレクトリへ出力します。C 言語から呼び出せる API も同梱しています。

### 主な機能

- libyaml (`yaml-0.1`) を利用した YAML ロードとマージ
- Hydra 互換の `defaults` 解決（グループ配置に対応）
- CLI 上書き。新規キーは `+new.key=value` を必須とし、既存キーのみ `key=value` を許可
- 設定値リファレンス・環境変数・時刻フォーマットを含む補間処理
- Hydra と同じランタイム出力 (`hydra.run.dir` 以下に `.hydra/config.yaml` などを保存)
- C API (`include/hydra/c_api.h`) による他言語連携
- C API には Hydra 互換の CLI 解析ヘルパー `hydra_config_apply_cli` を用意
- 設定値を扱いやすくするヘルパ (`hydra/c_api_utils.h`, `hydra/config_utils.hpp`) を同梱
- [rxi/log.c](https://github.com/rxi/log.c) を統合したロギングシステム。`hydra.job_logging.root.level` から自動設定

### 使い方

```bash
cmake -S . -B build
cmake --build build
./build/hydra-cpp --config configs/main.yaml model.depth=30
./build/hydra-cpp --config configs/main.yaml +experiment.name=demo
```

標準出力に展開済み設定を表示し、同時に `hydra.run.dir`
（既定値: `outputs/${now:%Y-%m-%d_%H-%M-%S}`）
配下へ保存します。新規キーの追加は `+` を先頭につけてください。

### 設定ファイル構成

- `configs/main.yaml` – エントリポイント (`defaults` で他設定を指定)
- `configs/{group}/{name}.yaml` – グループ単位の設定ファイル
- `configs/hydra/default.yaml`, `configs/paths/default.yaml` – Hydra 実行系設定

補間の解決順序:

1. `${path.to.node}` – 他設定値の参照
2. `${oc.env:VAR,default}` – 環境変数 + フォールバック
3. `${now:%...}` – `strftime` フォーマットでの時刻文字列

#### C サンプルのビルド

```bash
cmake --build build --target hydra-c-example
./build/hydra-c-example --config configs/main.yaml trainer.batch_size=5

cmake --build build --target hydra-cpp-example
./build/hydra-cpp-example --config configs/main.yaml trainer.batch_size=12
```

読み取り前に補間が再解決されるので、取得値は常に展開後になります。付属の `hydra-c-example` / `hydra-cpp-example` では `hydra_config_expect_*` や `hydra::utils::expect_*` を利用して構造体へ紐付ける実例を確認できます。

### テスト

```bash
ctest --test-dir build --output-on-failure
```

CLI 上書き、`defaults` マージ、補間（環境変数・現在時刻含む）、C API の振る舞いを網羅的に検証しています。

### 開発メモ

- `hydra.run.dir=null` を指定すると出力ディレクトリの生成を抑止できます。
- 将来規模が大きくなる場合は `src/main.cpp` の責務分離や、補間処理の共通ユーティリティ化などリファクタリング余地があります。

### 謝辞

このプロジェクトは [rxi/log.c](https://github.com/rxi/log.c) をロギングに使用しています（MIT ライセンス）。

### 貢献

修正案や改善案がある場合は、プルリクエストをお送りください。
