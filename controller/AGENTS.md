# Controller — JavaFX Control Panel

JavaFX 21 desktop app. Manages pet instances, config persistence, idle scheduling, and WebSocket communication with the C++ renderer.

## STRUCTURE

```
controller/
├── pom.xml                              # Maven (Java 21, JavaFX 21.0.5)
└── src/
    ├── main/
    │   ├── java/com/desktoppet/
    │   │   ├── App.java                  # JavaFX Application entry
    │   │   ├── Launcher.java             # Fat-JAR entry (JPMS workaround)
    │   │   ├── core/                     # Business logic (15 files)
    │   │   ├── model/                    # Data models — ALL Java Records (21 files)
    │   │   ├── network/                  # WebSocket server + protocol (3 files)
    │   │   ├── ui/                       # JavaFX controllers (8 files)
    │   │   └── util/                     # ProcessManager + AutoLaunchManager (2 files)
    │   ├── resources/
    │   │   ├── fxml/                     # 6 FXML layouts
    │   │   ├── css/                      # style.css + 6 theme CSS files
    │   │   └── logback.xml               # Production logging
    │   └── protobuf/                     # bundles.proto (voice pack schema)
    └── test/
        └── java/com/desktoppet/
            ├── core/                     # 10 unit tests
            ├── network/                  # 3 tests (incl. real WS server)
            ├── ui/                       # 3 TestFX headless tests
            ├── util/                     # 2 tests (ProcessManager, AutoLaunchManager)
            └── integration/              # 1 E2E smoke test
```

## WHERE TO LOOK

| Task | File(s) | Notes |
|------|---------|-------|
| Add tab/panel UI | `ui/` + `resources/fxml/` + `resources/css/` | Controller class, FXML layout, CSS theme |
| Add idle behavior logic | `core/Scheduler.java` | `ScheduledExecutorService`, random motion selection |
| Add interaction mapping | `core/InteractionHandler.java` | hit area → motion group mapping |
| Add config field | `model/` (Record) + corresponding `*ConfigManager.java` | Config at `~/.config/desktop-pet/` |
| Add voice pack feature | `core/VoicePackScanner.java`, `core/MetaMkoParser.java` | Protobuf `.mko` parsing |
| Add WS message type | `network/Protocol.java` + `network/MessageDispatcher.java` | Must match C++ `Protocol.hpp` |
| Add instance management | `core/InstanceConfigManager.java`, `core/PetStateManager.java` | Per-pet config in `instances/{uuid}.json` |
| Manage renderer process | `util/ProcessManager.java` | Start/stop/restart with crash recovery |
| Auto-launch on OS startup | `util/AutoLaunchManager.java` | Windows registry / Linux .desktop autostart |
| Switch graphics backend | `InstanceConfig.graphicsBackend` + `resolveRendererPath()` | Compile-time renderer variant (OpenGL/Vulkan) |

## KEY DEPENDENCIES

| Dep | Version | Purpose |
|-----|---------|---------|
| JavaFX (controls+fxml) | 21.0.5 | UI framework — do NOT use 25.x |
| Java-WebSocket | 1.6.0 | WS server (controller = server) |
| Gson | 2.13.2 | JSON ↔ Java Records |
| protobuf-java | 4.29.6 | Voice pack `.mko` parsing |
| SLF4J + Logback | 2.0.17 / 1.5.32 | Logging |

## CONVENTIONS

- **All data models are Java Records** — no POJOs, no Lombok, no getters/setters boilerplate
- **Gson `@SerializedName`** maps `snake_case` JSON config keys to `camelCase` Record fields
- **`AppOrchestrator`** is the lifecycle hub — startup/shutdown/crash recovery all flow through it
- **`PetStateManager`** uses `ReentrantReadWriteLock` for thread-safe state snapshots
- **`ProcessManager`** uses injectable `ProcessBuilderFactory` for testability
- **UI controllers** follow JavaFX FXML pattern — `@FXML` annotated methods, `initialize()` for setup
- **Tab layout**: `MainWindowController` is the tab container; each tab has its own controller + FXML
- **Themes**: `style.css` is base; 6 theme CSS files override colors. Theme selection in `PanelConfig`

## ANTI-PATTERNS

- **NO `System.out`/`System.err`** — use `log.info()`, `log.error()` etc.
- **NO `e.printStackTrace()`** — use `log.error("msg", e)`
- **NO `Thread.sleep()`** — use `ScheduledExecutorService` (see `Scheduler.java`)
- **NO `new Thread(...)`** — use managed executors
- **NO `@SuppressWarnings`** — fix root cause instead
- **NO committing `target/generated-sources/`** — protobuf output is gitignored

## TESTING

```bash
mvn test -f controller/pom.xml           # All tests (headless via Monocle)
mvn test -Dtest=ProtocolTest             # Single class
mvn test -pl . -Dtest="*Test"            # All via pattern
```

- **Framework**: JUnit 5.11.4 + Mockito 5.14.2 + TestFX 4.0.18 + Monocle
- **Naming**: `<ClassUnderTest>Test`, methods use `camelCase_describesBehavior`
- **File tests**: Use `@TempDir` (JUnit manages lifecycle)
- **WS tests**: Real server on random port, inner-class test clients
- **UI tests**: Extend `ApplicationTest`, headless via Monocle (`-Dglass.platform=Monocle`)
- **Log assertions**: `ListAppender` on logback for verifying log messages
- **Cubism resources**: Some tests reference `../third_party/CubismSdkForNative/Samples/Resources/`

## PITFALLS

- **Mockito re-stubbing**: Use `doReturn().when()`, not `when().thenReturn()` for checked-exception methods
- **Monocle dependency**: Must be `io.github.sebivenlo:openjfx-monocle:jdk-21.0.1`
- **module-info.java**: Empty packages in `opens`/`exports` cause runtime `InvalidModuleDescriptorException`
- **Surefire JVM args**: Requires extensive `--add-opens`/`--add-exports` for JavaFX internals
- **Fat JAR**: `maven-shade-plugin` excludes `module-info.class`; main class is `Launcher` not `App`
