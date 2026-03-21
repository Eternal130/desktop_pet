# Voice Pack Mounting Phase 3a - Learnings

## T1: Protobuf Maven Integration

**Timestamp**: 2026-03-21 17:24 UTC+8

### Key Findings

- **protobuf.version property**: 4.29.6
- **Maven plugin**: org.xolstice.maven.plugins:protobuf-maven-plugin:0.6.1
  - Initial attempt with io.github.ascopes:protobuf-maven-plugin:5.0.2 failed (requires Maven 3.9.6)
  - Switched to org.xolstice which is compatible with Maven 3.6.x
- **java_package**: com.desktoppet.bundle
- **java_multiple_files**: true (generates separate .java files per message type)
- **protoSourceRoot**: Must be explicitly configured to `${project.basedir}/src/main/protobuf`
- **os-maven-plugin**: Required extension (kr.motd.maven:os-maven-plugin:1.7.1) for platform-specific protoc binary detection

### Generated Files

All 24 protobuf Java files generated successfully in `target/generated-sources/protobuf/java/com/desktoppet/bundle/`:
- Bundle.java (main message)
- Action.java, ActionGroup.java, AiModule.java, Theme.java, Timing.java, Meta.java, FileEntity.java
- Env.java, Exp.java, Wake.java
- BundleType.java (enum)
- OrBuilder interfaces for each message type

### Module Configuration

- Added `requires com.google.protobuf;` to module-info.java
- Added `exports com.desktoppet.bundle;` to module-info.java
- Protobuf dependency added to pom.xml dependencies section

### Build Status

- **mvn compile**: ✅ SUCCESS (2.995s)
- **mvn test**: ⚠️ 83 tests run, 3 failures in MainWindowTest (pre-existing UI test issues, not protobuf-related)
  - 80 tests passed
  - 3 failures in MainWindowTest (unrelated to protobuf changes)

### .gitignore Update

Added `target/generated-sources/` to .gitignore to prevent committing generated code.

## T2: Data Model Records

**Timestamp**: 2026-03-21 17:31 UTC+8

### Records Created

All 5 Java record classes created in `com.desktoppet.model`:

1. **VoicePackAction** — 7 fields
   - `int id` — global unique action ID
   - `String motionPath` — relative path to motion file (can be empty)
   - `String audioPath` — relative path to audio file (can be null/empty)
   - `String lipSyncPath` — relative path to lipsync file (nullable)
   - `String doc` — interaction text
   - `long fadeInMs` — fade-in duration (milliseconds, proto int64)
   - `long fadeOutMs` — fade-out duration (milliseconds, proto int64)

2. **VoicePackModule** — 3 fields
   - `String key` — module identifier (e.g., "idle", "tap")
   - `int priority` — module priority level
   - `String filePath` — module file path (e.g., "modules/idle.mkai")

3. **VoicePackGroup** — 4 fields
   - `String code` — group code/event name (e.g., "morning", "tap_head")
   - `String name` — display name (e.g., "早安", "交互：触摸头部")
   - `int priority` — priority level (1-5)
   - `List<VoicePackAction> actions` — list of actions in group

4. **VoicePackInfo** — 6 fields
   - `String dirName` — voice pack directory name
   - `String displayName` — display name from Meta.name
   - `String code` — pack code from Meta.code
   - `Path basePath` — absolute path to voice pack directory (java.nio.file.Path)
   - `Map<String, VoicePackGroup> groups` — event name → group mapping
   - `List<VoicePackModule> modules` — list of behavior modules

5. **MountConfig** — 2 fields
   - `String modelName` — model directory name
   - `String voicePackName` — voice pack directory name (null = not mounted)

### Design Notes

- **No eventOverrides in Phase 3a**: MountConfig uses direct mapping only
- **Nullable fields**: lipSyncPath and voicePackName are nullable String (not Optional)
- **Empty vs null**: audioPath can be null or empty string (not all actions have audio)
- **Long for milliseconds**: fadeInMs/fadeOutMs are `long` (proto int64 milliseconds)
- **Record style**: Compact records following existing ModelInfo.java pattern
- **No annotations**: No @JsonProperty, GSON, or other serialization annotations
- **No methods**: Pure data-only DTOs, no builders or validators

### Compilation

- **mvn compile**: ✅ SUCCESS (2.214s)
- All 68 source files compiled successfully
- Evidence saved to `.sisyphus/evidence/task-2-records-compile.txt`

## T3: play_motion_ext C++ Command

**Timestamp**: 2026-03-21

### File Loading

- Method name: `CreateBuffer(path, &size)` / `DeleteBuffer(buffer, path)` — inherited from `LAppModel_Common` → `CubismUserModel`
- `LAppPal::LoadFileAsBytes` also exists but existing codebase always uses the inherited `CreateBuffer`/`DeleteBuffer` pattern

### Motion Loading

- Used `LoadMotion(buffer, size, NULL, NULL, NULL)` from `CubismUserModel` base class
- NULL for name, callbacks, modelSetting, group, index — avoids any fade time override from model settings
- Cast result to `CubismMotion*` to call `SetEffectIds(_eyeBlinkIds, _lipSyncIds)`
- Fade times set manually via `SetFadeInTime(fadeIn)` / `SetFadeOutTime(fadeOut)` on `ACubismMotion`

### Callback Pattern

- `ACubismMotion::FinishedMotionCallback` is `typedef void (*)(ACubismMotion* self)` — plain C function pointer
- Used a named static function `OnExtMotionFinishedStatic` in anonymous namespace in LAppModel.cpp (not a lambda)
- Custom data set via `motion->SetFinishedMotionCustomData(ctx)` after `SetFinishedMotionHandler`
- Struct `ExtMotionCtx` holds emitter pointer + filePath string, defined in anonymous namespace

### Memory Management

- `autoDelete=true` passed to `_motionManager->StartMotionPriority` — motion manager owns and deletes motion after playback
- Callback deletes only the `ExtMotionCtx` heap allocation; motion itself deleted by manager

### Forward Declaration

- Added `namespace Network { class EventEmitter; }` to `LAppModel.hpp` (avoids pulling in full network headers from model header)
- Full `#include "network/EventEmitter.hpp"` added to `LAppModel.cpp` for `emit()` call

### Build

- `[100%] Built target desktop-pet-renderer` — clean build success
- Evidence: `.sisyphus/evidence/task-3-cpp-build.txt`

## T4: VoicePackScanner Class and Tests

**Timestamp**: 2026-03-21 17:45 UTC+8

### Implementation

**VoicePackScanner.java** (`com.desktoppet.core`):
- Static utility class (private constructor)
- Single public method: `scanAvailableVoicePacks(Path resourcesDir)`
- Returns `List<String>` of voice pack directory names
- Behavior:
  * Returns empty list if resourcesDir is null or not a directory
  * Uses `DirectoryStream` to iterate subdirectories
  * Skips hidden directories (starting with '.')
  * Checks for `meta.mko` file in each subdirectory
  * Returns sorted list of matching directory names
  * Logs warnings on IOException (does not throw)

### Test Coverage

**VoicePackScannerTest.java** (`com.desktoppet.core`):
- 5 test cases using JUnit 5 `@TempDir` for isolation
- All tests PASS ✅

1. **scanWithVoicePackDir_returnsVoicePackName()** — Creates temp dir with subdirectory containing meta.mko → returns directory name
2. **scanWithModelDir_excludesModelDir()** — Creates temp dir with subdirectory containing only name.model3.json → returns empty list
3. **scanEmptyResourcesDir_returnsEmptyList()** — Empty temp directory → returns empty list
4. **scanNonExistentDir_returnsEmptyList()** — Non-existent path → returns empty list (no exception)
5. **scanHiddenDir_skipsHiddenDir()** — Temp dir with .hidden/meta.mko → returns empty list

### Build Results

- **mvn test -Dtest=VoicePackScannerTest**: ✅ SUCCESS
- Tests run: 5, Failures: 0, Errors: 0, Skipped: 0
- Time elapsed: 0.150 s
- Total build time: 3.815 s
- Evidence: `.sisyphus/evidence/task-4-scanner-tests.txt`

### Design Notes

- **Pattern copied from ModelScanner**: Structurally identical but detects `meta.mko` instead of `.model3.json`
- **No parsing**: Only checks file existence, does not parse meta.mko content
- **No recursion**: Only scans immediate subdirectories, does not recurse deeper
- **Returns List<String>**: Not VoicePackInfo objects (those come in later phases)
- **No audio/protobuf logic**: Pure directory scanning utility

## T5: MetaMkoParser and Tests

**Timestamp**: 2026-03-21 17:52 UTC+8

### Implementation

**MetaMkoParser.java** (`com.desktoppet.core`):
- Static utility class (private constructor)
- Single public method: `parse(Path voicePackDir)` → `VoicePackInfo` or `null`
- Parses `meta.mko` via protobuf `Bundle.parseFrom(bytes)` from `com.desktoppet.bundle`
- Returns `null` on `IOException` or `InvalidProtocolBufferException` (logs WARN)
- Two-phase group building: collect actions by group code first, then build VoicePackGroup records (avoids mutation through record accessor)

### Proto → Model Mapping

| Proto field | Java model field |
|---|---|
| `Meta.name` | `VoicePackInfo.displayName` |
| `Meta.code` | `VoicePackInfo.code` |
| `ActionGroup.code/name/priority` | `VoicePackGroup.code/name/priority` |
| `Action.id/motion/audio/lipSync/doc/fadeIn/fadeOut` | `VoicePackAction` fields |
| `AiModule.key/priority/file` | `VoicePackModule.key/priority/filePath` |
| `Action.motion` (empty string) | `null` in `motionPath` |
| `Action.audio` (empty string) | `null` in `audioPath` |
| `Action.hasLipSync()` false | `null` in `lipSyncPath` |

### Fixture

- `controller/src/test/resources/voice-pack-fixture/meta.mko` — 43070 bytes
- Copied from `build/bin/Resources/锦瑟-锦瑟-中文-voice/meta.mko`
- Confirms `tap_head` group exists in the real voice pack

### Test Path

- `FIXTURE_DIR = Path.of("src/test/resources/voice-pack-fixture")` — relative path works because Maven runs tests from `controller/` directory
- `@TempDir` used for isolation in corrupt/missing file tests

### Test Results

- **mvn test -Dtest=MetaMkoParserTest**: ✅ SUCCESS
- Tests run: 5, Failures: 0, Errors: 0, Skipped: 0
- Time elapsed: 0.208 s
- Confirmed: `Protocol message contained an invalid tag (zero)` for `{0x01,…}` corrupt bytes
- Evidence: `.sisyphus/evidence/task-5-parser-tests.txt`

## T6: MountConfigManager and Tests

**Timestamp**: 2026-03-21 17:56 UTC+8

### Implementation

**MountConfigManager.java** (`com.desktoppet.core`):
- Manages persistence of voice pack mount configuration to `mount.json`
- Default path: `~/.config/desktop-pet/mount.json`
- Injectable constructor for testing
- JSON format: `{"mounts": {"modelName": {"voice_pack": "packName"}}}`
- Methods:
  * `loadForModel(String modelName)` → `MountConfig` (voicePackName is null if not mounted)
  * `saveForModel(MountConfig config)` — saves or updates single model config
  * `loadAll()` → `Map<String, MountConfig>` — returns all configured mounts
- Error handling: logs warnings on IOException, returns empty/null gracefully
- Uses Gson with pretty printing for JSON serialization

### Test Coverage

**MountConfigManagerTest.java** (`com.desktoppet.core`):
- 4 test cases using JUnit 5 `@TempDir` for isolation
- All tests PASS ✅

1. **saveAndLoad_roundTrip()** — Saves Chinese voice pack name, loads and verifies
2. **loadNonExistentModel_returnsNullVoicePack()** — Unmounted model returns null voicePackName
3. **multiModelIsolation()** — Multiple models can be configured independently
4. **saveNull_persistsNullVoicePack()** — Unmounting (null voice pack) persists correctly

### Build Results

- **mvn test -Dtest=MountConfigManagerTest**: ✅ SUCCESS
- Tests run: 4, Failures: 0, Errors: 0, Skipped: 0
- Time elapsed: 0.181 s
- Total build time: 4.013 s
- Evidence: `.sisyphus/evidence/task-6-config-tests.txt`

### Design Notes

- **Pattern**: Follows HitAreaCacheManager for per-entity JSON persistence
- **Null handling**: voicePackName can be null (unmounted state)
- **No file watching**: Simple load/save, no hot reload
- **No locking**: Single-threaded, no synchronization
- **Gson static**: Reuses single GSON instance with pretty printing

## T7: MountedBehaviorEngine and Tests

**Timestamp**: 2026-03-21 18:01 UTC+8

### Implementation

**MountedBehaviorEngine.java** (`com.desktoppet.core`):
- New engine class that maps hit area IDs directly to mounted voice-pack groups
- `hasGroupForArea(String)` checks presence in `voicePack.groups()`
- `buildMotionCommand(String)` behavior:
  - returns `null` when no matching group
  - filters actions to entries with non-empty `motionPath`
  - returns `null` when group has no motion-capable actions (audio-only groups)
  - randomly selects one motion action from candidates
  - builds absolute motion path via `voicePack.basePath().resolve(action.motionPath())`
  - converts fade values from milliseconds to seconds (`/ 1000.0f`)
  - builds `play_motion_ext` command envelope via `Protocol.createCommand(...)`
  - returns serialized JSON via `Protocol.serialize(...)`

### Envelope/Protocol Pattern Notes

- Followed existing InteractionHandler pattern: build `JsonObject` payload → `Protocol.createCommand` → `Protocol.serialize`
- `Envelope` for command keeps `success/error*` as `null` (response-only fields)
- Payload keys for `play_motion_ext`:
  - `motion_path` (absolute path)
  - `priority` (group priority)
  - `fade_in` (seconds)
  - `fade_out` (seconds)

### Test Coverage

**MountedBehaviorEngineTest.java** (`com.desktoppet.core`):
- 5 tests, all passing:
  1. `hasGroupForArea_returnsTrueForKnownArea`
  2. `buildMotionCommand_forTapHead_returnsValidCommandEnvelope`
  3. `buildMotionCommand_forUnknownArea_returnsNull`
  4. `buildMotionCommand_forAudioOnlyArea_returnsNull`
  5. `buildMotionCommand_convertsFadeMillisecondsToSeconds`

### Build Results

- **mvn test -Dtest=MountedBehaviorEngineTest**: ✅ SUCCESS
- Tests run: 5, Failures: 0, Errors: 0, Skipped: 0
- Evidence: `.sisyphus/evidence/task-7-engine-tests.txt`

## T8: AppOrchestrator Wiring

**Timestamp**: 2026-03-21 18:09 UTC+8

### Changes Made

**AppOrchestrator.java** — wired voice pack system into the orchestrator lifecycle:

**New imports**: `MountConfig`, `VoicePackInfo`, `Collections`, `LinkedHashMap`, `Map` (same-package `core.*` classes need no imports)

**New fields**:
- `MountConfigManager mountConfigManager` — manages per-model mount.json
- `volatile MountedBehaviorEngine mountedEngine` — active engine, null when no pack mounted; `volatile` for safe cross-thread reads
- `Map<String, VoicePackInfo> voicePackInfoCache` — `LinkedHashMap` preserves scan order
- `List<String> availableVoicePacks` — ordered list for UI display

**doStartup() extension** (after `configManager.load()`):
- Creates `MountConfigManager`
- Calls `VoicePackScanner.scanAvailableVoicePacks(RENDERER_DIR.resolve("Resources"))`
- Parses each with `MetaMkoParser.parse()`, builds `voicePackInfoCache`
- Fills `availableVoicePacks` from cache keys
- Logs discovery count

**model_loaded handler** (after `sendHitAreasToRenderer`):
- Calls `initMountedEngine(modelId)` — loads `MountConfig`, creates/destroys `MountedBehaviorEngine`

**hit handler** (modified):
- Snapshot `engine = mountedEngine` (volatile read, safe)
- If engine non-null and `hasGroupForArea(areaId)` → `buildMotionCommand` → `sendOrCache("play_motion_ext", cmd)` then return
- Fallback to `interactionHandler.handleHitEvent(envelope)` if no match or null command

**New public methods**:
- `getAvailableVoicePacks()` — returns unmodifiable view
- `getCurrentVoicePackForModel(String)` — loads from mountConfigManager or returns null
- `mountVoicePack(String, String)` — persists config, re-initializes engine if current model
- `unmountVoicePack(String)` — delegates to `mountVoicePack(name, null)`

### Key Design Notes

- `PetState.currentModelName()` (not `modelName()`) — the correct accessor on the record
- Same-package classes (`VoicePackScanner`, `MetaMkoParser`, `MountedBehaviorEngine`, `MountConfigManager`) need no imports
- `volatile` on `mountedEngine` provides thread-safe read-only access from event handler thread
- `LinkedHashMap` for `voicePackInfoCache` preserves insertion/scan order in `availableVoicePacks`

### Test Results

- **mvn test**: ✅ Tests run: 102, Failures: 1, Errors: 2, Skipped: 0
- Pre-existing: 3 failures in MainWindowTest (UI redesign, not related to T8)
- New failures: **NONE**
- Evidence: `.sisyphus/evidence/task-8-regression-tests.txt`

### Task 9: Settings UI
- TestFX tests without orchestrator bypass the init() method, requiring us to check purely node presence (#voicePackComboBox) instead of populated items.
- Followed established patterns for TestFX test from SettingsPanelTest.
