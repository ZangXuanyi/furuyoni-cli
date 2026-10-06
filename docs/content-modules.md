# 内容模块与预设规则包

评测机的内容（女神卡组）以 **Lua 模块** 形式动态加载，每个模块带一个 **pack（规则包）标签**，
再由 **preset（预设）** 决定“启用哪些 pack、是否允许异相”。这样无需改代码即可切换环境。

## 1. 模块

* 一个模块 = 一个 `content/*.lua` 文件，返回卡牌表（见 README §4）。
* 模块自身不声明 pack；pack 由**加载方式**决定：
  * `Engine::load_content(file, pack)`
  * `Engine::load_content_dir(dir, pack)`（按文件名排序加载目录下所有 `*.lua`）
  * `Engine::load_manifest("content/packs.json")`（按清单加载，最常用）
* 引擎记录每个 `CardDef` 的来源 pack（`defPack_`）与模块清单（`modules()`）。

`content/packs.json`（默认清单）格式：

```json
{
  "tatsujin": ["yurina", "saine", "...", "raira"],
  "official": [],
  "custom": []
}
```

* `tatsujin` —— 达人包 01–12（仓库自带）。
* `official` —— 后续官方女神（全扩）。新增官方女神时把模块名加进这个数组即可
  （当前已登记 `utsuro`(13-虚路)、`honoka`(14-仄佳)、`konuru`(15-凝努)、`yatsuha`(16-八叶)、`kamuwi`(21-神居)）。
* `custom` —— 自定义女神。

## 2. 预设

| preset（别名） | 说明 | pack | 异相 |
|---|---|---|---|
| `kigen-tatsujin`（起源战达人） | 01–12 本格 | tatsujin | 禁用 |
| `kigen-full`（起源战全扩） | + 后续官方本格 | tatsujin + official | 禁用 |
| `gachi-tatsujin`（完全战达人） | 01–12 本格 + 异相 | tatsujin | 允许 |
| `gachi-full`（完全战全扩） | 全部官方 + 异相 | tatsujin + official | 允许 |

* **默认 preset = `kigen-full`（起源战全扩）**，即需求方的主要评测环境。
* 任意 preset 加 `allowCustom = true` 即得到“含自定义女神的完全战/起源战”。
* 还可用 `allowedPacks`（直接指定 pack 列表）与 `variantsOverride`（覆盖异相开关）做更细的控制，
  以便将来扩展新的规则包。

## 3. 决策规则

* **女神池**（`goddess_pool()`）：所有来源 pack 被允许、且 `enabledGoddesses` 允许的女神，按加载顺序去重。
  三拾一舍从该池中抽取。
* **可选形态**（`available_forms(g)`）：本格 O 一定可用；异相 A1/A2 仅当 preset 允许异相时可选。
  因此 `起源战` 下 `deck_def_ids(g, "O")` 只会用到本格牌，`完全战` 下才按同编号替换。
* `Config.enabledGoddesses` 非空时作为白名单（可用于“只跑指定几柱”的定向测试/评测）。
* `Config.draftPool` 非空时直接覆盖女神池（兼容旧用法）。

## 4. 命令行

```bash
# 默认：起源战全扩
./build/furuyoni-cli --standard --random --seed 42

# 指定预设（中英文别名均可）
./build/furuyoni-cli --standard --random --seed 42 --preset gachi-tatsujin
./build/furuyoni-cli --standard --random --seed 42 --preset 起源战达人

# 细粒度覆盖
./build/furuyoni-cli --standard --random --variants on --packs tatsujin,official
./build/furuyoni-cli --standard --random --goddesses yurina,saine,himika

# 含自定义女神（把模块目录按 "dir:pack" 挂进来）
./build/furuyoni-cli --standard --random --allow-custom \
  --content-dir ./my_goddesses:custom --goddesses mygoddess,yurina
```

运行时第一行会打印解析后的规则包：

```
ruleset: preset=kigen-full variants=off packs=tatsujin+official goddesses=12 modules=12
```

## 4.5 禁用组合表（村规/官方禁卡）

`content/combo_bans.json` 是一个数组，每条表示“当某玩家同时选了 a 与 b 两柱时，`card` 不能加入其构筑”：

```json
[ { "a": "himika", "b": "utsuro", "card": "真红凶弹" } ]
```

* 这是全《散樱》唯一的官方禁卡（铳镰的凶弹快攻），也已按数据驱动实现，便于之后加村规。
* 用 `--bans FILE` 可换用别的表；`Config.comboBans` 也可在代码里直接追加。
* 只在构筑（`build_from_pool`）阶段生效：被禁的牌不会出现在可选池里。

## 5. 编写自定义女神

1. 新建一个 Lua 模块（可放在任意目录，例如 `my_goddesses/foo.lua`），格式与内置模块相同。
2. 必须提供 7 张常规（`kind="normal"`）+ 4 张切札（`kind="special"`）；`num` 为组内编号；
   异相用 `form="A1"/"A2"` 并让 `num` 与本格对应（同编号替换）。
3. 用 `--content-dir <dir>:custom --allow-custom`（或 `load_content(file, "custom")`）接入；
   若希望它出现在非 custom 的规则包里，把它登记进 `allowedPacks` / `packs.json` 的自定义 pack。
4. 参考夹具 `src/tests/fixtures/custom_goddess.lua`（最小可用的自定义女神，含 1 个异相）。

## 5.5 追加牌（EX）的区位约定

* 追加牌一律 `extra = true`，不进入构筑与 `deck_def_ids`。
* `ctx:gain_extra(name)` 把该 EX 放进**切牌区未使用态**（引擎的“追加区”约定）。
  文本写“以未使用状态获得 X 到切牌区”的（`双掌生花`/`新幕来临`/`夙愿`/`熠熠见繁樱`…）直接用。
* 普通（非切札）EX 若被 `gain_extra` 获得，必须由卡面把它移出切牌区（抽牌堆底/弃牌堆/牌库），
  否则它既不能当切札打出、也不在正常区域。`绽放` 的 `bloom()` 已按文本强制二选一。
* 引擎只在 `Zone::Special` 里把 `kind == "special"` 的牌当切札提供／当对应（普通 EX 不会被误当切札）。

## 6. 动态加载 / 卸载

* **加载**是增量的：可以在任意时刻加载更多模块；`defs` / `defPack_` 会同步增长。
* **卸载**通过“不再启用”实现：`enabledGoddesses` 白名单或 `allowedPacks` 选择，
  而不是从 `defs` 中物理移除（已加载的 `CardDef` 保持不变，避免索引失效）。
* 每个模块的装载范围记录在 `modules()` 中，便于审计“这一局的牌来自哪些模块”。

## 7. 测试覆盖

* `ruleset presets select packs and whether 异相 is allowed`：4 个预设 + 中文别名。
* `dynamic modules: custom pack, manifest and 异相 control`：动态加载自定义模块、pack 开关、异相控制。
* `a match can be played with a restricted (custom) goddess pool`：只用 2 柱（含自定义）跑完整对局。
* `the bundled packs manifest loads the 达人 modules`：清单加载。
