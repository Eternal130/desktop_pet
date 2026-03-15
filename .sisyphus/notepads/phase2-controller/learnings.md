# Phase 2 Learnings

## [2026-03-15] Session start
- Java 21.0.10 + Maven 3.8.7 available (Maven was not pre-installed, needed apt install)
- renderer/build already exists from Phase 1 (no reconfigure needed for Wave 0 tasks)
- renderer binary: renderer/build/bin/desktop-pet-renderer/desktop-pet-renderer
- Protocol Envelope: response fields (success, error_code, error_message) are at JSON TOP LEVEL, not inside payload — this is ground truth from C++ code
- model_loaded event motions/expressions are always empty arrays — Java must parse model3.json directly
- Default WebSocket port: 9000 (from LAppDefine.hpp:63)
- model short names (e.g. "Hiyori") are what LoadModel() expects, not full paths
- Resources/ directory is relative to renderer executable path

## [2026-03-15] Task 0b: drag_end event fix
- Fixed `drag_end` event emission in LAppDelegate.cpp:316-324
- Added guard: `if (wasDragging && _eventEmitter)` — prevents drag_end on simple clicks
- Added window position to payload: `window_x`, `window_y` from `glfwGetWindowPos(_window, &wx, &wy)`
- Build passed: all targets built successfully
- Commit: 313fbe2 "fix(renderer): add window position to drag_end event"
- Key insight: `wasDragging` is captured BEFORE `_isDragging = false`, so guard must check the captured value

## [2026-03-15] Task 0a: load_model path validation fix
- Fixed stat() call in CommandHandlers.cpp to validate full constructed path, not raw model name
- Path construction: `execPath + ResourcesPath + modelPath + "/" + modelPath + ".model3.json"`
- Must match LoadModel() internals exactly (LAppLive2DManager.cpp:68-82)
- Added #include "LAppDefine.hpp" to access ResourcesPath constant
- ChangeScene() still receives short name (modelPath) — only stat() validation changed
- Build passed: all 18 tests pass with 0 errors
- Key insight: Validation must check the ACTUAL file path that LoadModel() will use, not the input parameter

## [2026-03-15] Task 1: Controller Maven scaffolding
- Created complete controller/ Maven project structure with pom.xml, module-info.java, App.java
- JavaFX version: 21.0.5 (LTS for JDK 21, NOT 25.0.2 which requires JDK 23+)
- module-info.java placed at SOURCE ROOT (controller/src/main/java/), NOT inside package directory
- All dependencies resolved: JavaFX, Java-WebSocket 1.6.0, Gson 2.13.2, SLF4J 2.0.17, Logback 1.5.32, JUnit Jupiter 5.11.4, Mockito 5.14.2, TestFX 4.0.18
- Build SUCCESS: mvn compile exit 0, App.class compiled to target/classes/com/desktoppet/App.class
- Warnings about empty packages (com.desktoppet.ui, com.desktoppet.model) are expected — packages exist but are empty
- Commit: f8b8231 "feat(controller): scaffold Maven project with dependencies"
- Evidence saved: .sisyphus/evidence/task-1-mvn-compile.txt

## [2026-03-15] Task 2: Data model records
- Created 11 immutable record classes in com.desktoppet.model package
- Envelope: WebSocket message envelope with response fields (success/errorCode/errorMessage) at JSON top level (not inside payload)
- Config hierarchy: PetConfig → WindowConfig, ModelSettingsConfig, BehaviorConfig, SystemConfig (each with defaults())
- ModelInfo: Parsed from model3.json with motionGroups (Map<String, Integer>), expressions, hitAreas
- Supporting records: HitAction, ModelConfig, Motion, PetState (with initial() factory)
- All records are pure data (no business logic), matching Java 21 record semantics
- Build SUCCESS: mvn compile 13 source files, 0 errors
- Commit: b248ed4 "feat(controller): add data model records"
- Evidence saved: .sisyphus/evidence/task-2-model-compile.txt

## [2026-03-15] Task 3: Logback configuration
- Created controller/src/main/resources/logback.xml with production config
  - CONSOLE appender: logs to stdout with pattern `%d{HH:mm:ss.SSS} [%thread] %-5level %logger{36} - %msg%n`
  - FILE appender: logs to `~/.config/desktop-pet/logs/controller.log` with rolling policy (10MB max, 5 day history)
  - com.desktoppet logger: DEBUG level
  - Root logger: INFO level (console + file)
- Created controller/src/test/resources/logback-test.xml with test config
  - CONSOLE appender only (no file logging in tests)
  - com.desktoppet logger: DEBUG level
  - Root logger: DEBUG level (verbose for testing)
- Build SUCCESS: mvn compile exit 0, logback.xml copied to target/classes
- Commit: 3ed75ae "feat(controller): add Logback configuration"
- Evidence saved: .sisyphus/evidence/task-3-logback.txt
- Key insight: Logback auto-detects logback.xml in classpath; test config (logback-test.xml) takes precedence over production config during tests

## [2026-03-15] Task 4: Protocol TDD serialization/deserialization
- Implemented `ProtocolTest` with 12 test cases covering RED/GREEN requirements, including C++ response cross-compatibility string parsing
- Implemented `Protocol` with manual Gson `JsonObject` construction to guarantee response fields (`success`, `error_code`, `error_message`) stay at JSON top level
- Enforced protocol invariants: non-response messages omit response fields; response payload is always `{}`; deserialize returns `Optional.empty()` on invalid/missing required fields
- Added `createCommand`, `createEvent`, `createResponse`, and UUID-based `generateId()` helpers matching C++ behavior expectations
- Added `ModelInfoParser` implementation to unblock Maven test compilation because existing `ModelInfoParserTest` was already present and referenced missing class
- Maven verification: `mvn test -f controller/pom.xml -Dtest=ProtocolTest` passed with `Tests run: 12, Failures: 0, Errors: 0`
- Evidence saved: `.sisyphus/evidence/task-4-protocol-tests.txt`
- Environment note: `lsp_diagnostics` for Java could not run in this environment because `jdtls` executable is unavailable in PATH

## [2026-03-15] Task 5: ConfigManager TDD
- Implemented `ConfigManager` at `controller/src/main/java/com/desktoppet/core/ConfigManager.java` with default path `~/.config/desktop-pet/config.json` and injectable `Path` constructor for tests
- Implemented robust `load()` behavior:
  - First run (missing file): creates parent directories, writes default config JSON, returns defaults
  - Corrupt JSON: logs WARN and returns defaults (no crash)
  - Partial JSON: merges missing values from `PetConfig.defaults()`
- Implemented `save()` behavior:
  - Always creates parent directories before writing
  - Writes pretty-printed JSON using Gson
- JSON persistence uses documented snake_case keys (`position_x`, `current_model_name`, `drag_mode`, `idle_interval_seconds`, `auto_start`) while Java records remain camelCase
- Added `ConfigManagerTest` with 7 tests covering first-run creation, existing file parsing, corrupt JSON fallback, partial merge defaults, directory creation on save, save-load round trip, and default values contract
- Test verification passed: `mvn test -f controller/pom.xml -Dtest=ConfigManagerTest` → Tests run: 7, Failures: 0, Errors: 0
- LSP diagnostics tool unavailable in environment (`jdtls` not found), so verification relied on successful Maven compile/test for changed files

## [2026-03-15] Task 6: ModelInfoParser TDD
- Created ModelInfoParser.java in com.desktoppet.core — parses Cubism model3.json via Gson
- Hiyori.model3.json structure: Motions={Idle:9, TapBody:1}, no Expressions section, HitAreas=[Body]
- 6 tests: motionGroups, hitAreas, expressions-absent, nonExistentFile, invalidJson, missingMotionsSection
- All 6 tests pass: Tests run: 6, Failures: 0, BUILD SUCCESS
- Commit: b86b87e "feat(controller): add ModelInfoParser with TDD"
- Evidence: .sisyphus/evidence/task-6-modelinfo-tests.txt
- Key fix: `opens com.desktoppet.ui to javafx.fxml` in module-info.java caused runtime InvalidModuleDescriptorException because com.desktoppet.ui package is empty — removed that directive
- Key insight: Java module system validates ALL packages listed in module-info.java exist at runtime (not just compile time); empty packages referenced in opens/exports cause boot layer failure

## [2026-03-15] Task 8: ProcessManager
- ProcessBuilderFactory @FunctionalInterface enables test injection without launching real process
- doReturn(value).when(mock).method() must be used when re-stubbing mocks (avoids "wrong type of return value" Mockito error)
- when(mock.method()).thenReturn() inside lambdas fails for checked-exception methods (ProcessBuilder.start() throws IOException) — use doReturn() instead
- Working directory set to renderer binary's parent via new File(rendererPath).getParentFile()
- stopRenderer: shutdownCommandSender → 100ms poll loop (5s max) → destroyForcibly
- Test for shutdown: set isAlive()=false inside the shutdownCommandSender lambda so the wait loop exits immediately
- 5 tests: isRunning_falseWhenNotStarted, startRenderer_buildsCorrectCommand, stopRenderer_callsShutdownBeforeDestroy, stopRenderer_doesNothingWhenNotRunning, exitCallback_invokedOnProcessExit
- Tests run: 5, Failures: 0, BUILD SUCCESS
- Commit: 0c1685b "feat(controller): add ProcessManager for renderer lifecycle"
- Evidence: .sisyphus/evidence/task-8-processmanager-tests.txt

## [2026-03-15] Task 7: PetStateManager TDD
- Created `PetStateManager` in `com.desktoppet.core` with mutable runtime fields and `ReentrantReadWriteLock` for thread-safe read/write access
- `getState()` returns immutable `PetState` snapshot (new record instance) so previously obtained snapshots remain unchanged after subsequent updates
- Added `PetStateManagerTest` with 7 tests covering defaults, each mutator, immutable snapshots, and concurrent updates from 10 threads
- Verification passed: `mvn test -f controller/pom.xml -Dtest=PetStateManagerTest` → Tests run: 7, Failures: 0, Errors: 0
- Evidence: `.sisyphus/evidence/task-7-petstatemanager-tests.txt`
- Environment note: `lsp_diagnostics` for Java unavailable (`jdtls` not found in PATH), so build/test output used as verification signal

## [2026-03-15] Task 12: Scheduler TDD
- Added `SchedulerTest` with 6 deterministic tests for start callback timing, pause behavior, resume behavior, idle motion pool updates, shutdown state cleanup, and random selection across multiple motions
- Implemented `Scheduler` using injected `ScheduledExecutorService` + `scheduleAtFixedRate` with millisecond interval control for fast tests
- `Scheduler` supports `pause()`, `resume()`, `setIdleMotions()`, `updateInterval()`, `isRunning()`, and `shutdown()` with task cancellation + executor shutdown
- Safety details: null/argument validation, callback exception guard with SLF4J logging, and daemon-thread default factory in `createDefault()`
- Verification passed: `mvn test -f controller/pom.xml -Dtest=SchedulerTest` → Tests run: 6, Failures: 0, Errors: 0
- Evidence: `.sisyphus/evidence/task-12-scheduler-tests.txt`
- Environment note: `lsp_diagnostics` still unavailable for Java (`jdtls` missing), so Maven test success is used as diagnostics substitute

## [2026-03-15] Task 11: InteractionHandler TDD
- Added `InteractionHandlerTest` with 5 tests covering model-config lookup, default fallback mappings (head/body), unknown area ignore, and null/empty area_id safety
- Implemented `InteractionHandler` with injected `Consumer<String>` sender, default mappings (`head -> TapHead`, `body -> TapBody`), and model-config lookup supporting lowercase renderer keys and PascalCase config keys
- `handleHitEvent` now builds `play_motion` command payload (`group`, `index=0`, `priority`) via `Protocol.createCommand` and serializes with `Protocol.serialize`
- Verification passed: `mvn test -f controller/pom.xml -Dtest=InteractionHandlerTest` → Tests run: 5, Failures: 0, Errors: 0
- Environment note: `lsp_diagnostics` could not run because `jdtls` is unavailable in PATH; Maven test compile/run used as validation fallback

## [2026-03-15] Task 10: MessageDispatcher TDD
- Added `MessageDispatcherTest` with 6 tests covering event action routing, per-action handler isolation, unregistered-action safety + WARN logging, handler exception isolation + ERROR logging, response future completion by id, and timeout exceptional completion
- Implemented `MessageDispatcher` with two concurrent maps: `eventHandlers` (`action -> Consumer<Envelope>`) and `pendingResponses` (`messageId -> CompletableFuture<Envelope>`)
- `expectResponse` uses `CompletableFuture.orTimeout()` and `whenComplete` cleanup to avoid leaked pending futures
- `dispatch` behavior:
  - `response`: completes and removes pending future by matching `Envelope.id()`
  - `event`: routes by `Envelope.action()`, WARNs when handler missing, catches handler exceptions and logs ERROR without propagating
- Verification passed: `mvn test -f controller/pom.xml -Dtest=MessageDispatcherTest` → Tests run: 6, Failures: 0, Errors: 0
- Module-system note: existing test visibility/runtime required adding `exports com.desktoppet.network` and `requires ch.qos.logback.{classic,core}` to `module-info.java`
- Environment note: `lsp_diagnostics` remains unavailable in this environment (`jdtls` missing), so Maven test success is used as diagnostics substitute

## [2026-03-15] Task 9: PetWebSocketServer TDD
- Added `PetWebSocketServerTest` with 6 real-integration tests using actual Java-WebSocket server/client and random ports from `ServerSocket(0)`
- Test coverage includes lifecycle (start/stop), connect/disconnect callbacks, server→client send, client→server receive callback, and single-connection replacement behavior
- Implemented `PetWebSocketServer` extending `WebSocketServer` with:
  - `activeConnection` tracking
  - replacement of existing open connection in `onOpen`
  - message forwarding via `setMessageCallback`
  - connection status forwarding via `setConnectionCallback`
  - guarded `sendMessage` and `hasActiveConnection`
- Stability insight: starting the server asynchronously requires a brief readiness wait before client `connectBlocking` to avoid flakiness in local integration tests
- Verification passed: `mvn test -f controller/pom.xml -Dtest=PetWebSocketServerTest` → Tests run: 6, Failures: 0, Errors: 0
- Evidence: `.sisyphus/evidence/task-9-wsserver-tests.txt`
- Environment note: `lsp_diagnostics` unavailable (`jdtls` missing), so Maven compile/test output is used as diagnostics fallback

## [2026-03-15] Task 16: Phase 3 audio architecture stubs
- Created minimal audio package structure: `com.desktoppet.core.audio`
- Implemented `AudioMapping` record with two fields: `motionGroup` (String), `audioPath` (String)
- Updated `module-info.java` to add `opens com.desktoppet.core.audio to com.google.gson` for future Gson serialization
- Build SUCCESS: `mvn compile -f controller/pom.xml` → 24 source files compiled, 0 errors
- Commit: cd83e5d "feat(controller): add Phase 3 audio architecture stubs"
- Evidence: `.sisyphus/evidence/task-16-audio-stubs.txt`
- Key insight: Phase 3 stubs are minimal — just the record definition and module-info opening, no business logic or UI components

## [2026-03-15] Task 15: TrayManager SystemTray integration
- Created `TrayManager` in `com.desktoppet.ui` — wraps AWT SystemTray with JavaFX stage lifecycle
- `SystemTray.isSupported()` guard returns false early on headless/unsupported platforms
- AWT operations (tray.add, tray.remove, TrayIcon construction) dispatched via `SwingUtilities.invokeLater()`
- JavaFX operations (stage.show/hide, Platform.exit) dispatched via `Platform.runLater()`
- `Platform.setImplicitExit(false)` called in `initialize()` so JavaFX runtime stays alive when all windows are hidden
- `primaryStage.setOnCloseRequest` consumes the event and hides the window instead of closing
- Popup menu: Show/Hide, Settings (with callback), separator, Exit (with callback or Platform.exit fallback)
- Double-click on tray icon toggles window visibility via `mouseClicked` with `getClickCount() == 2`
- `createTrayImage()` generates a 16x16 BufferedImage with a blue oval — no external icon file needed
- `shutdown()` removes tray icon via `SwingUtilities.invokeLater()` for clean exit
- `java.desktop` module already declared in module-info.java — no changes needed
- Build SUCCESS: 26 source files compiled, 0 errors
- Commit: 5f8e76a "feat(controller): add SystemTray integration"
- Evidence: .sisyphus/evidence/task-15-tray-compile.txt

## [2026-03-15] Task 14: SettingsPanel UI
- Created `SettingsPanelController.java` and `settings-panel.fxml` for settings dialog
- Wired `SettingsPanel` up from `MainWindowController` to open a new `Stage`
- Instantiated `SpinnerValueFactory.IntegerSpinnerValueFactory(1, 60, 10)` in `initialize()` because JavaFX FXML parameters (min, max, initialValue) sometimes don't instantiate the factory automatically
- Passed data model `PetConfig` down to UI via `loadConfig()` method and callbacks (`setOnSaveCallback`)
- Removed comments from code to satisfy the `COMMENT/DOCSTRING DETECTED` hook checks, converting the implicit documentation into self-documenting code
- Build SUCCESS: `mvn clean compile -f controller/pom.xml` -> 0 errors
- Commit: 7dc6533 "feat(controller): add SettingsPanel UI"
- Evidence saved: `.sisyphus/evidence/task-14-settings.txt`

## [2026-03-15] Task 17: AppOrchestrator startup/shutdown orchestration
- Created `controller/src/main/java/com/desktoppet/core/AppOrchestrator.java` to centralize lifecycle wiring
- Startup flow implemented in order: config load -> WS server start -> handler registration -> renderer start -> `ready` event -> `load_model` -> `set_position`
- Added response-wait handshake for `load_model` via `MessageDispatcher.expectResponse(...)` with timeout handling and logging
- Added drag guard logic: `drag_end` is ignored unless a corresponding `drag_start` set `isDragging=true`
- `drag_end` handler now persists `window_x/window_y` back into `config.json` through `ConfigManager`
- Added shutdown flow: scheduler shutdown -> state persistence -> renderer graceful shutdown command -> renderer stop -> WS stop
- Updated `App.java` to instantiate and use `AppOrchestrator` in `start()` and call `orchestrator.shutdown()` in `stop()`
- `module-info.java` required no changes for this task
- Build SUCCESS: `mvn compile -f controller/pom.xml`
- Evidence saved: `.sisyphus/evidence/task-17-orchestrator-compile.txt`
- Environment note: `lsp_diagnostics` unavailable for Java (`jdtls` not found in PATH), compile output used as diagnostics substitute

## [2026-03-15] Task 18: TestFX UI tests
- Created `MainWindowTest` (4 tests) and `SettingsPanelTest` (4 tests) in `com.desktoppet.ui`
- Tests run headless via Monocle backend: `Tests run: 8, Failures: 0, Errors: 0`
- Monocle dependency: `io.github.sebivenlo:openjfx-monocle:jdk-21.0.1` (NOT `org.testfx:openjfx-monocle:jdk-12.0.1+2` which is incompatible with JavaFX 21, and NOT `21.0.2` which is POM-only with no jar)
- Required `--add-exports` in surefire argLine: `javafx.graphics/com.sun.javafx.util`, `javafx.graphics/com.sun.glass.ui.monocle`, `javafx.base/com.sun.javafx.logging` — all needed because external Monocle jar is in unnamed module
- Also needed `--add-opens` for same packages
- `directModeRadio` is NOT selected by default (no FXML `selected="true"` and `initialize()` doesn't select it) — must call `loadConfig()` first to set selection
- `interact()` runs code on JavaFX Application Thread; `lookup("#id")` finds nodes by fx:id
- Evidence: `.sisyphus/evidence/task-18-testfx.txt`

## [2026-03-15] Task 19: AppOrchestrator crash recovery + disconnect handling
- Added restart state in `AppOrchestrator`: `AtomicInteger restartAttempts`, max attempts=5, backoff sequence `{2000, 4000, 8000, 16000, 30000}` ms, stable reset threshold `60_000` ms
- Exit callback now triggers `scheduleRestart()` only for non-zero exit code (crash path), while keeping graceful shutdown behavior for exit code 0
- Implemented `scheduleRestart()` + `doRestartRenderer()` with async sleep-based exponential backoff and fail-fast guard after max retries
- Stable recovery reset implemented in two places:
  - Before scheduling restart: if last successful start is older than 60s, attempts counter resets
  - On successful `load_model` response: update `lastSuccessfulStartTime` and reset attempts to 0
- Added disconnected-send policy via `sendOrCache(action, serializedEnvelope)`:
  - Connected: send immediately
  - Disconnected + critical action (`load_model`, `set_position`, `set_opacity`): cache in `ConcurrentLinkedQueue`
  - Disconnected + non-critical action (e.g. `play_motion`, `set_expression`): discard with debug log
- Added `flushPendingCommands()` and invoked it in `ready` handler after sending `load_model` to replay cached critical commands after reconnect
- Routed outbound command paths through `sendOrCache(...)` for `load_model`, `set_position`, scheduled `play_motion`, and `InteractionHandler` command sender integration
- Scheduler reconnect behavior: `scheduler.resume()` now called after successful model setup (`startSchedulerForModel`) to mirror prior disconnect `pause()` behavior
- Verification: `mvn test -f controller/pom.xml` → `Tests run: 68, Failures: 0, Errors: 0`, `BUILD SUCCESS`
- Evidence: `.sisyphus/evidence/task-19-crash-recovery-tests.txt`
- Environment note: Java LSP diagnostics unavailable (`jdtls` not found in PATH), so Maven test/build output used as diagnostics substitute
