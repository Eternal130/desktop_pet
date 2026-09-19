# P4/P5 验证报告（2026-09-19，Linux 侧）

> 对照设计文档 §D P4/P5 验收门。
> P4 提交: 98e1825（SDK 接口头）、0b03914（Registry/Host/Manager/PageModel + 静态插件管线）、b496f95（插件管理 UI + sdk-template dummy）。
> P5 提交: 4a12215（DownloadService 下载管线）、ef2f2dd（安装管线 + 真实 API 接线）。

## 1. P4 插件框架

| 门 | 结果 |
|---|---|
| 测试 | 41 → **45**（Registry 12 槽/Host 4/Manager 6/PageModel 8） |
| dummy 端到端 | sample 插件 registered→Started 完整链; 截图轮换零代码扩展（`--pages` 直收插件 id）→ **4 PNG**（含 org.desktop-pet.sample.png） |
| 禁用语义 | 双测试覆盖（enabled=false → 零 create 调用; kv 位 → 启动跳过） |
| manifest 损坏 | 缺字段 / 非数值 api_version 均 configure 期 FATAL_ERROR（实测） |
| 退出 QA | --self-quit EXIT=0, Started→Stopped 在退出链内 |
| context property | 17 → 19（pluginPages/pluginManager 追加尾部），零 ReferenceError |
| 视觉（obs-1） | 锚点项/侧栏插件导航/示例页面全就位、中文零方框; 遗留: ①插件管理卡片需滚动后核验 ②插件页标题重复 bug——两项已派修复 |

## 2. P5 DownloadService 宿主管线

| 门 | 结果 |
|---|---|
| 测试 | 45 → **47**（DownloadService 8 槽/InstallPipeline 8 槽） |
| e2e | file:// 夹具 → download（sha256 过）→ installArchive → VoicePackScanner **发现 FixturePack**（哨兵落位断言） |
| 退出 QA + 截图 | EXIT=0; 4 PNG; 严格 grep 零命中 |
| **QNAM 咽喉点审计** | grep 全 src/ 唯一实例化点 DownloadService 内部 ✓ |
| zip-slip | 四类恶意名全拒 + **先验证全部 entry 再写任何字节**（恶意包零落盘） |
| 断网/错误路径 | IReplyFactory 注入 seam + FakeReply（设计 v2 要求落地） |

## 3. 偏差记录（已按"推翻需回改"纪律收录）

1. **QZipReader → miniz**: 设计/任务书指定 Qt 私有 QZipReader，但本 Qt 发行版无私有头（qzipreader_p.h 缺席、qt6-base-private-dev 未装）→ 换 miniz 3.0.2（FetchContent，spdlog 先例），同一隔离 TU（ZipArchive.cpp 头部可替换性注记）。**Windows/CI 侧若装有 Qt private headers 不受影响**（隔离边界保证）。
2. **miniz CMake 兼容垫片**: 其 CMake 早于 CMake 4 的 3.5 兼容移除 → 官方 `CMAKE_POLICY_VERSION_MINIMUM` 垫片（上游修复后可去）。
3. **VoicePackScanner 契约注记**: `scanAvailableVoicePacks` 返回**绝对路径**而非目录名——P4 的 listPacks 曾按裸目录名假设实现，被 P5 e2e 门抓出修正。已补进 AGENTS.md。
4. 夹具工程技巧: miniz writer 拒写绝对路径 entry 名 → 恶意名夹具以等长占位符写入 + finalize 后字节补丁（STORED 无压缩、长度不变偏移不动）。

## 4. 工程教训（P4 过程记录）

- 生成的 C++ 代码绝不可经 CMake list 变量传递（foreach 非引用展开按 `;` 撕裂元素——实测复现，改 file(APPEND) 单引号循环）
- 本仓命名空间混用（DatabaseManager 等全局类 vs core:: 自由函数）——前置声明逐类核对
- e2e 日志门抓真 bug: 生成行参数签名误标（qmlUrl 误作 error），编译期零症状——日志断言门的价值实证

## 5. 状态与下一步

- P6a ★冻结点: 打磨项落地后 @oracle 独立接口 review（对照 §B.3 合同 + ABI 纪律）→ api/ 版本化受控声明
- P6b: 平台下载器插件（plugins/downloader/）——P5 管线已就绪
- CI 首跑与 Windows 侧验证仍待 push
