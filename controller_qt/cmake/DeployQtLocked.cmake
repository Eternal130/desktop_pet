# -----------------------------------------------------------------------------
# DeployQtLocked.cmake — serialized windeployqt runner (script mode)
#
# Wraps a windeployqt invocation in a PROCESS-guarded file lock so the N
# per-test POST_BUILD deploy steps ninja runs in PARALLEL cannot race on
# the shared Qt DLL set. All test executables live (and deploy) in the
# SAME binary dir; two concurrent windeployqt instances there can both
# decide the same DLL (e.g. Qt6Quick.dll) needs "Updating" and the loser
# aborts with "Cannot copy ...: Destination file exists" — which kills
# the chained link command and the whole build (seen on windows-latest
# CI, 2026-10-16). Serializing removes the race with zero semantic
# change: windeployqt stays idempotent, incremental rebuilds still only
# re-run the step when the target relinks.
#
# Invoked by _pet_deploy_qt_impl (DeployQtTest.cmake):
#   cmake -DDEPLOY_TOOL=<windeployqt>
#         -DDEPLOY_LOCK_FILE=<binary-dir lock path>
#         -DDEPLOY_ARGS=<semicolon list: flags... exe-path>
#         -P DeployQtLocked.cmake
# -----------------------------------------------------------------------------

file(LOCK "${DEPLOY_LOCK_FILE}" GUARD PROCESS)

execute_process(
    COMMAND "${DEPLOY_TOOL}" ${DEPLOY_ARGS}
    RESULT_VARIABLE _pet_deploy_rc)

file(LOCK "${DEPLOY_LOCK_FILE}" RELEASE)

if(NOT _pet_deploy_rc EQUAL 0)
    message(FATAL_ERROR
        "windeployqt exited with ${_pet_deploy_rc} (args: ${DEPLOY_ARGS})")
endif()
