"""Run component Unity suites on the PC, using the real Unity from ESP-IDF.

    python tests/host_unity/run_host_tests.py              # every suite in SUITES
    python tests/host_unity/run_host_tests.py e32p_uds     # just one

Only pure code can run here. Each suite lists the sources that need no hardware,
and the headers in shim/ stand in for the two platform headers they include.
Anything that touches a peripheral stays on the target's unit runner
(tests/unit_runner), which remains the source of truth.

Needs gcc on PATH (MinGW-w64 on Windows) and an ESP-IDF install for Unity's
sources (IDF_PATH, or DEFAULT_IDF below). Builds under build/. Exits non-zero if
anything fails to build, a suite crashes, or any case fails.
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
COMPONENTS = HERE.parent.parent / "components"
DEFAULT_IDF = Path("C:/Espressif/frameworks/esp-idf-v5.3.1")

# component -> the sources pure enough to build on the PC. The transport glue
# (e32p_*_isotp.c) and anything else that needs ESP-IDF stays out.
SUITES = {
    "e32p_obd2": ["src/e32p_obd2.c", "src/e32p_obd2_dtc.c"],
    "e32p_uds": ["src/e32p_uds.c", "src/e32p_uds_security.c", "src/e32p_uds_transfer.c",
                 "src/e32p_uds_dtc.c", "src/e32p_uds_routine.c"],
}

CFLAGS = ["-std=gnu11", "-O0", "-g", "-Wall", "-Wextra"]
SUMMARY = re.compile(r"(\d+) Tests (\d+) Failures (\d+) Ignored")


def run(cmd):
    """Run a compiler or linker command; stop on failure, show any warnings."""
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.stdout.strip() or r.stderr.strip():
        sys.stdout.write(r.stdout + r.stderr)
    if r.returncode != 0:
        raise SystemExit("build step failed: %s" % " ".join(str(c) for c in cmd[-3:]))


def build_suite(name, sources, gcc, unity_src):
    comp = COMPONENTS / name
    out = HERE / "build" / name
    out.mkdir(parents=True, exist_ok=True)
    ours = [gcc, *CFLAGS, "-I", HERE / "shim", "-I", unity_src,
            "-I", comp / "include", "-I", comp / "src"]
    objs = []

    # Unity is third-party: built as it is, without our warning flags.
    run([gcc, "-std=gnu11", "-O0", "-g", "-I", unity_src,
         "-c", unity_src / "unity.c", "-o", out / "unity.o"])
    objs.append(out / "unity.o")
    run([*ours, "-I", HERE, "-c", HERE / "host_main.c", "-o", out / "host_main.o"])
    objs.append(out / "host_main.o")
    for src in sources:
        obj = out / (Path(src).stem + ".o")
        run([*ours, "-c", comp / src, "-o", obj])
        objs.append(obj)
    test_obj = out / "test.o"
    run([*ours, "-I", HERE, "-include", "host_testcase.h",
         "-c", comp / "test" / ("test_%s.c" % name), "-o", test_obj])
    objs.append(test_obj)

    exe = out / ("%s_tests.exe" % name)
    run([gcc, *objs, "-o", exe])
    return exe


def main(argv):
    names = argv[1:] or list(SUITES)
    unknown = [n for n in names if n not in SUITES]
    if unknown:
        raise SystemExit("no host suite for %s (known: %s)" % (", ".join(unknown), ", ".join(SUITES)))
    gcc = shutil.which("gcc")
    if gcc is None:
        raise SystemExit("gcc not found on PATH (install MinGW-w64)")
    unity_src = Path(os.environ.get("IDF_PATH", DEFAULT_IDF)) / "components" / "unity" / "unity" / "src"
    if not (unity_src / "unity.c").exists():
        raise SystemExit("Unity sources not found at %s - set IDF_PATH" % unity_src)

    results, failed = [], 0
    for name in names:
        exe = build_suite(name, SUITES[name], gcc, unity_src)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        m = SUMMARY.search(r.stdout)
        if m is None:
            results.append("%-10s no summary line - the suite crashed (exit %d)" % (name, r.returncode))
            failed += 1
            continue
        tests, failures = int(m.group(1)), int(m.group(2))
        failed += failures
        results.append("%-10s %3d tests, %d failures" % (name, tests, failures))

    print("\n=== host suites ===")
    for line in results:
        print(line)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
