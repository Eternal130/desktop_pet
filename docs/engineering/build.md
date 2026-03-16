# 构建与分发

> 工程化概述参见 [工程化](./README.md)，整体架构参见 [架构总览](../README.md)。

---

## 一、目标平台

| 阶段 | 平台 | 窗口系统 |
|:---|:---|:---|
| MVP | Ubuntu 22.04 LTS 桌面版 | X11 |
| 后期 | Windows 10/11 | Win32 / DWM |

---

## 二、构建方案

**C++ 渲染引擎**：

```bash
# 安装构建依赖（Ubuntu 22.04）
sudo apt install build-essential cmake ninja-build \
    libgl-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev

# 配置（在 renderer/ 子目录下构建）
cmake -S renderer -B renderer/build -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build renderer/build -j$(nproc)

# 运行测试（Google Test + CTest）
ctest --test-dir renderer/build --output-on-failure

# 可执行文件输出路径
# renderer/build/bin/desktop-pet-renderer/desktop-pet-renderer
```

> **说明**：GLFW 和 GLEW 从 Cubism SDK 内置源码编译，无需安装系统包（`libglfw3-dev`、`libglew-dev`）。WebSocket 通信使用本地连接，未启用 TLS（`USE_TLS=OFF`），无需 `libssl-dev`。
> **Phase 3 增加依赖**：`libopenal-dev`（音频）。

**Java 控制面板（Phase 2）**：

```bash
# 编译 + 测试
mvn clean verify

# 打包（含依赖的 fat-jar）
mvn clean package

# 生成最小化 JRE
jlink --module-path $JAVA_HOME/jmods:target/modules \
      --add-modules java.base,java.desktop,javafx.controls,javafx.fxml \
      --output target/runtime \
      --strip-debug --compress zip-6

# 生成平台原生安装包
jpackage --input target/ \
         --main-jar desktop-pet-controller.jar \
         --runtime-image target/runtime \
         --name desktop-pet-controller \
         --type deb \
         --app-version 1.0.0
```

---

## 三、分发形式

**MVP**：仅 C++ 渲染引擎可执行文件。

**后期**：同时提供两种分发形式：

| 形式 | Ubuntu | Windows（后期） |
|:---|:---|:---|
| 安装包 | .deb（jpackage 生成） | .msi / .exe |
| 免安装压缩包 | .tar.gz | .zip |

---

## 四、打包结构

**MVP**：

```plain
desktop-pet/
├── desktop-pet-renderer          # C++ 渲染引擎可执行文件
└── models/                       # 默认模型（可与可执行文件同目录或使用硬编码路径）
    └── Haru/
```

**完整版（Phase 1-3）**：

```plain
desktop-pet/
├── bin/
│   ├── desktop-pet-controller    # Java 控制面板启动脚本
│   └── desktop-pet-renderer      # C++ 渲染引擎可执行文件
├── lib/
│   ├── *.so                      # C++ 运行时动态库
│   ├── app/                      # Java 应用 JAR
│   └── runtime/                  # 内嵌 JRE（jlink 生成的最小化运行时）
├── models/                       # 默认模型（不含音频文件）
│   └── default/
├── audio/                        # 音频文件独立目录（Phase 3，与模型分离存储）
│   └── default/                  # 默认音频文件
└── config/
    ├── config.json               # 默认配置（首次启动复制到 ~/.config/desktop-pet/）
    └── audio_mapping.json        # 音频映射配置（Phase 3）
```
