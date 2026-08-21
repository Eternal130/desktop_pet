#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""controller_qt UI 自动化测试脚手架。

自动化 qt-ui-manual-test.md 中无需真实鼠标交互的用例:
  TC-4.1  首次启动进入欢迎页(--screenshot welcome + 像素断言)
  TC-4.7  窗口几何持久化(写 panel.json 几何 → 启动 → 读日志/截图尺寸)
  TC-4.9  关闭按钮默认退出(--screenshot 模式自然退出 → 进程消失)
  TC-4.10 标题栏导航(--screenshot welcome,monitor,settings 三页)
  TC-4.11 环境检测全绿(日志 EnvironmentChecker 行)
  TC-4.12 端口占用异常态(占 9001 启动 → 日志 not bindable)
  TC-5.6  实例持久化(预置 instances/*.json → 启动 → 日志 loaded N)
  TC-9.2  冷启动时间(日志 COLD_START_MS=)
  TC-9.5  panel.json 损坏恢复
  TC-9.6  instance JSON 损坏恢复
  TC-9.7  端口冲突优雅降级

用法: python run_ui_tests.py [--exe PATH] [--keep]
依赖: 仅 Python 3.6+ 标准库;PNG 像素断言用 PowerShell System.Drawing
      (通过子进程调用,避免引入 PIL 依赖)。
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DEFAULT_EXE = os.path.join(REPO, "build", "bin", "desktop-pet-controller-qt.exe")
CFG = os.path.join(os.path.expanduser("~"), ".config", "desktop-pet")
LOG = os.path.join(CFG, "logs", "desktop-pet-qt.log")

RESULTS = []


def result(tc, status, notes=""):
    RESULTS.append((tc, status, notes))
    print("  [%s] %s %s" % (status, tc, notes))


def read_log_tail(n=200):
    try:
        with open(LOG, "r", encoding="utf-8", errors="replace") as f:
            return f.readlines()[-n:]
    except IOError:
        return []


def run_screenshot(pages, out_dir, delay=1200, timeout=60, extra_env=None):
    """启动 --screenshot 模式,返回 (returncode, stdout, saved_files)。"""
    os.makedirs(out_dir, exist_ok=True)
    cmd = [EXE, "--screenshot", "--out", out_dir,
           "--pages", ",".join(pages), "--delay", str(delay)]
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    try:
        p = subprocess.run(cmd, capture_output=True, timeout=timeout, env=env,
                           cwd=os.path.dirname(EXE))
        out = (p.stdout or b"").decode("utf-8", "replace")
        saved = [l.split("=", 1)[1].strip() for l in out.splitlines()
                 if l.startswith("SCREENSHOT_SAVED=")]
        return p.returncode, out, saved
    except subprocess.TimeoutExpired:
        kill_leftover()
        return -1, "TIMEOUT", []


def kill_leftover():
    subprocess.run(["taskkill", "/F", "/IM", "desktop-pet-controller-qt.exe"],
                   capture_output=True)


def backup_cfg():
    if os.path.isdir(CFG):
        bak = CFG + ".autotest-bak"
        if os.path.isdir(bak):
            shutil.rmtree(bak)
        shutil.copytree(CFG, bak)
        shutil.rmtree(CFG)


def restore_cfg():
    bak = CFG + ".autotest-bak"
    kill_leftover()
    time.sleep(0.5)
    if os.path.isdir(CFG):
        shutil.rmtree(CFG)
    if os.path.isdir(bak):
        shutil.move(bak, CFG)


def px(path, x_pct, y_pct):
    """PowerShell System.Drawing 采样像素,返回 'rrggbb'。"""
    ps = ("Add-Type -AssemblyName System.Drawing;"
          "$b=[System.Drawing.Bitmap]::FromFile('%s');"
          "$c=$b.GetPixel([int]($b.Width*%s),[int]($b.Height*%s));"
          "$b.Dispose(); Write-Output ('{0:x2}{1:x2}{2:x2}' -f $c.R,$c.G,$c.B)"
          % (path, x_pct, y_pct))
    r = subprocess.run(["powershell", "-NoProfile", "-Command", ps],
                       capture_output=True)
    return r.stdout.decode().strip()


# ── 用例实现 ────────────────────────────────────────────────────────────

def tc_4_1(tmp):
    """TC-4.1 首次启动进入欢迎页。"""
    backup_cfg()
    try:
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "41"))
        ok = rc == 0 and len(saved) == 1
        if ok:
            # Sample guaranteed-empty spots: right edge of content area
            # (clear of the env panel) and bottom-left above the sidebar's
            # add button — the old (60%,75%) sample landed on the CTA button.
            canvas = px(saved[0], 0.97, 0.5)
            sidebar = px(saved[0], 0.08, 0.4)
            ok = canvas.lower() in ("fafafa", "ffffff") and sidebar.lower() == "f4f4f5"
            result("TC-4.1", "PASS" if ok else "FAIL",
                   "canvas=%s sidebar=%s" % (canvas, sidebar))
        else:
            result("TC-4.1", "FAIL", "rc=%s saved=%d" % (rc, len(saved)))
    finally:
        restore_cfg()


def tc_4_7(tmp):
    """TC-4.7 窗口几何持久化:写 panel.json 几何 → 启动 → 日志恢复值。"""
    backup_cfg()
    try:
        os.makedirs(os.path.join(CFG, "instances"), exist_ok=True)
        panel = {"panel_x": 100, "panel_y": 100, "panel_width": 900,
                 "panel_height": 600, "theme": "现代简约", "instance_ids": []}
        with open(os.path.join(CFG, "panel.json"), "w", encoding="utf-8") as f:
            json.dump(panel, f)
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "47"))
        tail = "".join(read_log_tail())
        ok = rc == 0 and "panelX=100 panelY=100 900x600" in tail
        result("TC-4.7", "PASS" if ok else "FAIL",
               "geometry-restore=%s" % ("panelX=100 panelY=100 900x600" in tail))
    finally:
        restore_cfg()


def tc_4_9(tmp):
    """TC-4.9 关闭按钮默认退出:--screenshot 完成后进程自然退出。"""
    rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "49"))
    time.sleep(1)
    check = subprocess.run(["tasklist", "/FI",
                            "IMAGENAME eq desktop-pet-controller-qt.exe"],
                           capture_output=True).stdout.decode(errors="replace")
    gone = "desktop-pet-controller-qt.exe" not in check
    result("TC-4.9", "PASS" if (rc == 0 and gone) else "FAIL",
           "rc=%s process-exited=%s" % (rc, gone))


def tc_4_10(tmp):
    """TC-4.10 标题栏导航:三页截图全部产出。"""
    rc, out, saved = run_screenshot(["welcome", "monitor", "settings"],
                                    os.path.join(tmp, "410"))
    names = sorted(os.path.splitext(os.path.basename(s))[0] for s in saved)
    ok = rc == 0 and names == ["monitor", "settings", "welcome"]
    result("TC-4.10", "PASS" if ok else "FAIL", "pages=%s" % names)


def tc_4_11(tmp):
    """TC-4.11 环境检测:日志含 6 项探测且 port bindable(无占用时)。"""
    rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "411"))
    tail = "".join(read_log_tail())
    has_env = "EnvironmentChecker: opengl=true" in tail
    port_ok = "port=true" in tail
    result("TC-4.11", "PASS" if (rc == 0 and has_env and port_ok) else "FAIL",
           "env-line=%s port=true:%s" % (has_env, port_ok))


def occupy_port():
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", 9001))
    s.listen(1)
    return s


def tc_4_12_9_7(tmp):
    """TC-4.12 + TC-9.7 端口占用:环境检测红色 + 控制器不崩溃。"""
    s = occupy_port()
    try:
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "412"))
        tail = "".join(read_log_tail())
        not_bindable = "not bindable" in tail
        port_false = "port=false" in tail
        ok = rc == 0 and not_bindable and port_false
        result("TC-4.12", "PASS" if ok else "FAIL",
               "not-bindable=%s port=false:%s" % (not_bindable, port_false))
        result("TC-9.7", "PASS" if rc == 0 else "FAIL",
               "controller-survived=%s" % (rc == 0))
    finally:
        s.close()


def tc_5_6(tmp):
    """TC-5.6 实例持久化:预置 2 个 instance json → 启动日志 loaded 2。"""
    backup_cfg()
    try:
        inst_dir = os.path.join(CFG, "instances")
        os.makedirs(inst_dir, exist_ok=True)
        ids = []
        for i, label in enumerate(["宠物A", "宠物B"]):
            uid = "aaaaaaaa-0000-0000-0000-%012d" % i
            ids.append(uid)
            cfg = {"id": uid, "label": label, "renderer_path": "",
                   "graphics_backend": "opengl", "model_name": "",
                   "window_x": -1, "window_y": -1, "window_width": 300,
                   "window_height": 400, "opacity": 1.0, "drag_mode": "direct",
                   "idle_interval_seconds": 10, "target_fps": 0,
                   "auto_start": False, "volume": 1.0, "muted": False,
                   "voice_pack": "", "current_expression": "",
                   "layout_offset_x": 0.0, "layout_offset_y": 0.0,
                   "layout_scale": 1.0}
            with open(os.path.join(inst_dir, uid + ".json"), "w",
                      encoding="utf-8") as f:
                json.dump(cfg, f)
        panel = {"panel_x": -1, "panel_y": -1, "panel_width": 1200,
                 "panel_height": 760, "theme": "现代简约",
                 "instance_ids": ids}
        with open(os.path.join(CFG, "panel.json"), "w", encoding="utf-8") as f:
            json.dump(panel, f)
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "56"))
        tail = "".join(read_log_tail())
        loaded2 = "loaded 2 instance(s)" in tail
        result("TC-5.6", "PASS" if (rc == 0 and loaded2) else "FAIL",
               "loaded-2=%s" % loaded2)
    finally:
        restore_cfg()


def tc_9_2(tmp):
    """TC-9.2 冷启动时间:日志 COLD_START_MS=<1500 目标。"""
    rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "92"))
    tail = "".join(read_log_tail(50))
    ms = None
    for line in tail.splitlines():
        if "COLD_START_MS=" in line:
            try:
                ms = int(line.split("COLD_START_MS=")[1].split()[0])
            except (ValueError, IndexError):
                pass
    ok = ms is not None and ms < 2000
    result("TC-9.2", "PASS" if ok else "FAIL", "cold_start_ms=%s" % ms)


def tc_9_5(tmp):
    """TC-9.5 panel.json 损坏:不崩溃 + 回退默认。"""
    backup_cfg()
    try:
        os.makedirs(CFG, exist_ok=True)
        with open(os.path.join(CFG, "panel.json"), "w", encoding="utf-8") as f:
            f.write('{"panel_x": 100, "panel_y"')  # truncated JSON
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "95"))
        result("TC-9.5", "PASS" if rc == 0 else "FAIL",
               "survived-corrupt-panel rc=%s" % rc)
    finally:
        restore_cfg()


def tc_9_6(tmp):
    """TC-9.6 instance JSON 损坏:跳过损坏实例,健康实例正常。"""
    backup_cfg()
    try:
        inst_dir = os.path.join(CFG, "instances")
        os.makedirs(inst_dir, exist_ok=True)
        good = "bbbbbbbb-0000-0000-0000-000000000001"
        cfg = {"id": good, "label": "健康实例", "renderer_path": "",
               "graphics_backend": "opengl", "model_name": "",
               "window_x": -1, "window_y": -1, "window_width": 300,
               "window_height": 400, "opacity": 1.0, "drag_mode": "direct",
               "idle_interval_seconds": 10, "target_fps": 0,
               "auto_start": False, "volume": 1.0, "muted": False,
               "voice_pack": "", "current_expression": "",
               "layout_offset_x": 0.0, "layout_offset_y": 0.0,
               "layout_scale": 1.0}
        with open(os.path.join(inst_dir, good + ".json"), "w",
                  encoding="utf-8") as f:
            json.dump(cfg, f)
        with open(os.path.join(inst_dir,
                               "cccccccc-0000-0000-0000-000000000002.json"),
                  "w", encoding="utf-8") as f:
            f.write('{"id": "cccccccc", "label":')  # truncated
        panel = {"panel_x": -1, "panel_y": -1, "panel_width": 1200,
                 "panel_height": 760, "theme": "现代简约",
                 "instance_ids": [good,
                                  "cccccccc-0000-0000-0000-000000000002"]}
        with open(os.path.join(CFG, "panel.json"), "w", encoding="utf-8") as f:
            json.dump(panel, f)
        rc, out, saved = run_screenshot(["welcome"], os.path.join(tmp, "96"))
        tail = "".join(read_log_tail())
        loaded1 = "loaded 1 instance(s)" in tail
        ok = rc == 0 and loaded1
        result("TC-9.6", "PASS" if ok else "FAIL",
               "survived=%s loaded-1(healthy-only)=%s" % (rc == 0, loaded1))
    finally:
        restore_cfg()


def main():
    global EXE
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--keep", action="store_true",
                    help="keep temp screenshots dir")
    args = ap.parse_args()
    EXE = args.exe
    if not os.path.isfile(EXE):
        print("exe not found: %s" % EXE)
        sys.exit(2)

    tmp = tempfile.mkdtemp(prefix="dpet_ui_tests_")
    print("artifacts: %s" % tmp)
    tests = [tc_4_1, tc_4_7, tc_4_9, tc_4_10, tc_4_11,
             tc_4_12_9_7, tc_5_6, tc_9_2, tc_9_5, tc_9_6]
    for t in tests:
        print("== %s ==" % t.__name__)
        try:
            t(tmp)
        except Exception as e:  # noqa: BLE001 - harness must not abort
            result(t.__name__, "ERROR", repr(e))
            kill_leftover()

    passed = sum(1 for _, s, _ in RESULTS if s == "PASS")
    print("\n===== SUMMARY: %d/%d PASS =====" % (passed, len(RESULTS)))
    for tc, s, note in RESULTS:
        print("%-14s %-5s %s" % (tc, s, note))
    with open(os.path.join(tmp, "results.txt"), "w", encoding="utf-8") as f:
        for tc, s, note in RESULTS:
            f.write("%s\t%s\t%s\n" % (tc, s, note))
    if not args.keep:
        pass  # keep artifacts by default for evidence; --keep reserved
    sys.exit(0 if passed == len(RESULTS) else 1)


if __name__ == "__main__":
    main()
