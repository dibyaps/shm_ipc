#!/usr/bin/env python3
"""
Parses Google Test JUnit XML reports and generates:
1. A rich standalone HTML dashboard (reports/index.html)
2. A GitHub Actions Job Summary Markdown (if running inside GitHub CI)
"""

import os
import sys
import platform
import subprocess
import xml.etree.ElementTree as ET

def get_system_info():
    info = {
        "os": f"{platform.system()} {platform.release()}",
        "arch": platform.machine(),
        "cores": str(os.cpu_count() or "Unknown"),
        "distro": "Linux",
        "cpu_model": "Unknown CPU",
        "compiler": "GCC"
    }

    # Extract Distro Name from /etc/os-release
    if os.path.exists("/etc/os-release"):
        try:
            with open("/etc/os-release") as f:
                for line in f:
                    if line.startswith("PRETTY_NAME="):
                        info["distro"] = line.split("=", 1)[1].strip().strip('"')
                        break
        except Exception:
            pass

    # Extract CPU Model from /proc/cpuinfo
    if os.path.exists("/proc/cpuinfo"):
        try:
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if "model name" in line:
                        info["cpu_model"] = line.split(":", 1)[1].strip()
                        break
        except Exception:
            pass

    # Extract Compiler version
    try:
        res = subprocess.run(["g++", "--version"], capture_output=True, text=True, timeout=2)
        if res.returncode == 0:
            info["compiler"] = res.stdout.splitlines()[0]
    except Exception:
        pass

    return info

def generate_reports():
    xml_path = "reports/test_report.xml"
    html_path = "reports/index.html"
    bench_log_path = "reports/bench_run.log"
    sys_info = get_system_info()

    if not os.path.exists(xml_path):
        print(f"Error: {xml_path} not found.")
        sys.exit(1)

    tree = ET.parse(xml_path)
    root = tree.getroot()

    total_tests = root.get("tests", "0")
    total_failures = root.get("failures", "0")
    total_errors = root.get("errors", "0")
    total_time = root.get("time", "0.0")

    suites_data = []
    for suite in root.findall("testsuite"):
        s_name = suite.get("name")
        s_tests = suite.get("tests")
        s_time = suite.get("time")
        test_cases = []
        for case in suite.findall("testcase"):
            c_name = case.get("name")
            c_time = case.get("time", "0.0")
            c_status = "PASSED"
            failure = case.find("failure")
            if failure is not None:
                c_status = "FAILED"
            test_cases.append({"name": c_name, "time": c_time, "status": c_status})
        suites_data.append({"name": s_name, "tests": s_tests, "time": s_time, "cases": test_cases})

    # Read benchmark data if available
    bench_content = ""
    if os.path.exists(bench_log_path):
        with open(bench_log_path, "r") as f:
            bench_content = f.read()

    # =========================================================================
    # 1. Generate Standalone HTML Report
    # =========================================================================
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SHM-IPC Test Execution Dashboard</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background-color: #0d1117;
            color: #c9d1d9;
            margin: 0;
            padding: 30px;
        }}
        .container {{ max-width: 960px; margin: 0 auto; }}
        h1 {{ color: #58a6ff; border-bottom: 1px solid #30363d; padding-bottom: 10px; }}
        h2 {{ color: #79c0ff; margin-top: 30px; }}
        .cards {{ display: flex; gap: 20px; margin-bottom: 30px; }}
        .card {{
            background: #161b22;
            border: 1px solid #30363d;
            border-radius: 8px;
            padding: 20px;
            flex: 1;
            text-align: center;
        }}
        .card .num {{ font-size: 32px; font-weight: bold; margin-top: 5px; }}
        .card.success .num {{ color: #3fb950; }}
        .card.danger .num {{ color: #f85149; }}
        .card.info .num {{ color: #58a6ff; }}
        table {{ width: 100%; border-collapse: collapse; margin-top: 15px; background: #161b22; border-radius: 8px; overflow: hidden; }}
        th, td {{ padding: 12px 16px; text-align: left; border-bottom: 1px solid #21262d; }}
        th {{ background: #21262d; color: #8b949e; font-size: 14px; text-transform: uppercase; }}
        .badge {{ padding: 4px 8px; border-radius: 4px; font-weight: bold; font-size: 12px; }}
        .badge.passed {{ background: #238636; color: #ffffff; }}
        .badge.failed {{ background: #da3633; color: #ffffff; }}
        pre {{ background: #161b22; border: 1px solid #30363d; padding: 15px; border-radius: 6px; overflow-x: auto; color: #7ee787; font-family: monospace; }}
    </style>
</head>
<body>
<div class="container">
    <h1>🚀 SHM-IPC Test & Benchmark Report</h1>
    
    <div class="cards">
        <div class="card info"><div>Total Tests</div><div class="num">{total_tests}</div></div>
        <div class="card success"><div>Passed</div><div class="num">{int(total_tests) - int(total_failures)}</div></div>
        <div class="card danger"><div>Failures</div><div class="num">{total_failures}</div></div>
        <div class="card info"><div>Total Runtime</div><div class="num">{float(total_time):.3f}s</div></div>
    </div>

    <h2>🖥️ System & Hardware Environment</h2>
    <table>
        <tbody>
            <tr><td style="width: 25%;"><strong>Distribution</strong></td><td>{sys_info['distro']}</td></tr>
            <tr><td><strong>Kernel</strong></td><td>{sys_info['os']}</td></tr>
            <tr><td><strong>CPU Model</strong></td><td>{sys_info['cpu_model']}</td></tr>
            <tr><td><strong>Logical Cores / Arch</strong></td><td>{sys_info['cores']} cores ({sys_info['arch']})</td></tr>
            <tr><td><strong>Compiler</strong></td><td><code>{sys_info['compiler']}</code></td></tr>
        </tbody>
    </table>

    <h2>Test Suite Breakdown</h2>
    <table>
        <thead>
            <tr><th>Suite</th><th>Test Case</th><th>Runtime</th><th>Status</th></tr>
        </thead>
        <tbody>
    """

    for s in suites_data:
        for c in s["cases"]:
            badge_class = "passed" if c["status"] == "PASSED" else "failed"
            html += f"""<tr>
                <td><strong>{s['name']}</strong></td>
                <td>{c['name']}</td>
                <td>{c['time']}s</td>
                <td><span class="badge {badge_class}">{c['status']}</span></td>
            </tr>"""

    html += """
        </tbody>
    </table>
    """

    if bench_content:
        html += f"""
        <h2>⚡ Zero-Copy Performance Benchmark</h2>
        <pre>{bench_content}</pre>
        """

    html += """
</div>
</body>
</html>
    """

    try:
        with open(html_path, "w") as f:
            f.write(html)
        print(f"HTML Report generated at: {html_path}")
    except PermissionError:
        print(f"Note: {html_path} already exists with container permissions, skipping overwrite.")

    # =========================================================================
    # 2. Generate GitHub Actions Job Summary (if running in GitHub Actions)
    # =========================================================================
    distro = os.getenv("DISTRO_NAME", sys_info["distro"])
    summary_path = os.getenv("GITHUB_STEP_SUMMARY")
    if summary_path:
        passed_count = int(total_tests) - int(total_failures)
        md = f"""### Test Results — {distro}
| Total Tests | Passed | Failures | Runtime |
| :---: | :---: | :---: | :---: |
| **{total_tests}** | **{passed_count}** | **{total_failures}** | **{float(total_time):.3f}s** |

#### 🖥️ Hardware & OS Environment:
| Metric | Value |
| :--- | :--- |
| **Operating System** | {sys_info['distro']} |
| **Kernel & Arch** | {sys_info['os']} ({sys_info['arch']}) |
| **CPU Model** | {sys_info['cpu_model']} ({sys_info['cores']} logical cores) |
| **Compiler** | `{sys_info['compiler']}` |

#### Test Suite Breakdown:
| Suite | Test Case | Status | Time |
| :--- | :--- | :---: | :---: |
"""
        for s in suites_data:
            for c in s["cases"]:
                md += f"| `{s['name']}` | `{c['name']}` | {c['status']} | {c['time']}s |\n"

        if bench_content:
            md += f"\n#### Benchmark (Single-Copy vs. Zero-Copy):\n```text\n{bench_content}\n```\n"

        with open(summary_path, "a") as f:
            f.write(md)
        print("GitHub Actions Step Summary written successfully.")

if __name__ == "__main__":
    generate_reports()
