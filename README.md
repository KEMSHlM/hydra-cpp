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

hydra_config_t *cfg = hydra_config_create();
char *err = NULL;

hydra_config_merge_file(cfg, "configs/main.yaml", &err);
hydra_config_apply_override(cfg, "model.depth=20", &err);

int64_t depth = 0;
hydra_config_get_int(cfg, "model.depth", &depth, &err);

char *yaml = hydra_config_to_yaml_string(cfg, &err);
puts(yaml);

hydra_string_free(yaml);
hydra_config_destroy(cfg);
```

Intermediates (overrides, merges) trigger interpolation just before reads, so returned values are always resolved.

### Tests

```bash
ctest --test-dir build --output-on-failure
```

Unit tests cover override parsing, defaults composition, interpolation (including environment, timestamps), command-line behavior, and C API integration.

### Development Tips

- The CLI enforces Hydra semantics: existing keys updated with `key=value`, new keys require `+key=value`.
- To disable run directory creation for a run: `./build/hydra-cpp ... hydra.run.dir=null`.

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

### C API 利用例

```c
#include "hydra/c_api.h"

hydra_config_t *cfg = hydra_config_create();
char *err = NULL;

hydra_config_merge_file(cfg, "configs/main.yaml", &err);
hydra_config_apply_override(cfg, "model.depth=20", &err);

int64_t depth = 0;
hydra_config_get_int(cfg, "model.depth", &depth, &err);

char *yaml = hydra_config_to_yaml_string(cfg, &err);
puts(yaml);

hydra_string_free(yaml);
hydra_config_destroy(cfg);
```

読み取り前に補間が再解決されるので、取得値は常に展開後になります。

### テスト

```bash
ctest --test-dir build --output-on-failure
```

CLI 上書き、`defaults` マージ、補間（環境変数・現在時刻含む）、C API の振る舞いを網羅的に検証しています。

### 開発メモ

- `hydra.run.dir=null` を指定すると出力ディレクトリの生成を抑止できます。
- 将来規模が大きくなる場合は `src/main.cpp` の責務分離や、補間処理の共通ユーティリティ化などリファクタリング余地があります。
