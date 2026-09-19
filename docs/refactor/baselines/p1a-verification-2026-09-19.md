# P1a 验证报告（2026-09-19，Linux 侧）

> 对照设计文档 §D P1a 验收门: `cmake --workflow` 产物等价（DLL 清单级）+ 双平台 + binaryDir 沿用旧名。
> 本机为 Linux——Windows 侧（toolchain 实机、Git-MinGW 中毒路径、QT_MINGW_ROOT 覆盖）留待 Windows 机器/P1d CI 验证。

## 1. 交付物

| 文件 | 内容 |
|---|---|
| `controller_qt/CMakePresets.json` | schema v6; 4 configure（win/linux × release/debug）+ 4 build + 4 test + 3 workflow |
| `controller_qt/cmake/qt-mingw-qt.cmake` | Qt MinGW 13.1.0 钉死 + QT_MINGW_ROOT/QT_NINJA/QT_PREFIX_PATH env 覆盖 + configure 期 mingw1310_64 硬校验 |
| `renderer/CMakePresets.json` | schema v6; win-gl/win-vk/linux-gl/linux-vk + build/test/workflow; win 侧 generator=MinGW Makefiles（复刻 build.py L309，与 toolchain mingw32-make 钉死一致） |
| `renderer/cmake/toolchain-mingw-renderer.cmake` | 系统 MinGW 钉死 + RENDERER_MINGW_ROOT 覆盖 + "Git" 路径毒药硬校验 + 工具存在性校验 |

`.gitignore` 增补两处 `CMakeUserPresets.json`。

## 2. 验证结果（全部通过）

| 门 | 结果 |
|---|---|
| `cmake --workflow --preset linux-release`（controller_qt，全新 binaryDir） | exit 0，含 41/41 测试 |
| `cmake --workflow --preset linux-gl-release`（renderer，全新 binaryDir） | exit 0，含 21/21 测试 |
| binaryDir | 沿用 `build/controller_qt` / `build/renderer_build`（F7 硬约束 ✓） |
| 产物等价（字节级） | `desktop-pet-renderer`、`libLive2DCubismCore.so`、`desktop-pet-controller-qt-poc` **三件字节级一致** |
| 产物等价（量级） | `desktop-pet-controller-qt`（7,994,272B）与 `renderer_tests`（989,504B）尺寸一致、哈希不同——与 v2 预期一致（内嵌构建树路径/时间戳），非工具链差异；poc/renderer 的字节一致已排除该可能 |
| 依赖清单 | `ldd` 全解析（controller 87 项 / renderer 无缺库），等价性佐证 |
| 截图 | 3 页面全产出，尺寸带 ±3B 内（见 §3 方法论修正） |

对照参数: pgrep 捕获的 build.py 实际调用（`-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5`）与 preset cacheVariables 逐项一致。

## 3. 方法论修正（重要，回写基线约定）

**截图 sha256 不能作等价性判据。** 对照实验: 同一二进制连跑 3 次——monitor 出现 2 种哈希、settings 3 种、welcome 3 种（动画帧/图表时序的运行时非确定性）。P0 基线期记录的哈希仅在"字节恰好稳定"的页面有参照价值。

**替代判据**（P2/P3/M 系列沿用）:
1. PNG 尺寸带（±16B 内视为同构）
2. 页面数完整（3/3 产出）
3. 抽样目检（关键页面）

**附带发现的缺陷（记入 P1b 清单）**: `--screenshot --out <dir>` 目录不存在时，应用打印 `SCREENSHOT_SAVED=...` 但**实际未落盘**（ScreenshotRunner 未 mkpath / 未校验保存结果）——需修：mkpath + 保存失败改打印错误并不计入成功。

## 4. 遗留

- Windows 侧实机验证（用户操作或 P1d CI）: ① `cmake --workflow --preset win-release` 全链; ② Git-MinGW 中毒路径确认 configure 期 FATAL; ③ `QT_MINGW_ROOT`/`RENDERER_MINGW_ROOT` 覆盖真实安装路径
- Vulkan 变体 workflow（linux-vk-release）未在本轮运行（GL 已验证 preset 机制; vk 为同构 USE_VULKAN=ON 开关）
- build.py 仍保留（P1b 才删除）——当前新旧入口并存，`docs/refactor` 已声明过渡态

回滚保障: build.py 缓存备份于 `/tmp/opencode/p0/backup-{controller,renderer}-build`。
