import os
import pty
import re
import select
import signal
import sys
import tempfile
import time


def child_exit_code(status):
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    return 128 + os.WTERMSIG(status)


def main():
    ash = sys.argv[1]
    want_strace = len(sys.argv) > 2 and sys.argv[2] == "strace"

    strace_log = None
    argv = [ash]
    if want_strace:
        fd, strace_log = tempfile.mkstemp(prefix="ash-nocfg-")
        os.close(fd)
        argv = ["strace", "-f", "-e", "trace=openat,open", "-o", strace_log, ash]

    pid, master = pty.fork()
    if pid == 0:
        os.environ["ANTHROPIC_API_KEY"] = "x"
        os.environ["ASH_URL"] = "http://127.0.0.1:1/"
        os.environ["ASAN_OPTIONS"] = os.environ.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        try:
            os.execvp(argv[0], argv)
        except OSError:
            os._exit(127)

    sent = False
    reads = 0
    while reads < 500:
        r, _, _ = select.select([master], [], [], 5)
        if not r:
            break
        try:
            data = os.read(master, 4096)
        except OSError:
            break
        if not data:
            break
        if not sent:
            try:
                os.write(master, b"\x04")
            except OSError:
                pass
            sent = True
        reads += 1

    code = None
    for _ in range(30):
        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            code = child_exit_code(status)
            break
        time.sleep(0.1)
    if code is None:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        os.waitpid(pid, 0)
        if sent:
            print("FAIL: ash ignored the EOT and did not exit under a pty (killed)")
        else:
            print("FAIL: ash produced no output under a pty within 5s (killed)")
        _cleanup(strace_log)
        sys.exit(1)

    trace = None
    if strace_log is not None:
        try:
            with open(strace_log, "r", errors="replace") as f:
                trace = f.read()
        except OSError:
            trace = ""
        _cleanup(strace_log)

    traced = trace is not None and trace.strip() != ""

    if code != 0:
        if want_strace and not traced:
            print("SKIP: strace produced no trace (ptrace blocked or attach failed)")
            sys.exit(0)
        print("FAIL: ash under a pty did not start up and exit 0 (code %d)" % code)
        sys.exit(1)

    if not want_strace:
        print("PASS: pty startup exits 0 (no strace)")
        sys.exit(0)
    if not traced:
        print("PASS: pty startup exits 0 (strace produced no trace, ptrace blocked)")
        sys.exit(0)

    pat = re.compile(
        r"open(at)?\([^)]*"
        r"(\.config/ash|\.ashrc|\.ash/(?!settings\.json)|/etc/ash|"
        r"ash\.(conf|toml|json|ya?ml|ini))",
        re.IGNORECASE,
    )
    for line in trace.splitlines():
        if pat.search(line):
            print("FAIL: startup opened a ash config path: %s" % line.strip())
            sys.exit(1)
    print("PASS: real startup under a pty opened no ash config path")
    sys.exit(0)


def _cleanup(path):
    if path is not None:
        try:
            os.remove(path)
        except OSError:
            pass


if __name__ == "__main__":
    main()
