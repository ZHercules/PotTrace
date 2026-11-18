"""Observe an owned process before accepting its PotPlayer window.

Exit evidence is persisted before callers start cleanup. An exit code, including
zero, is evidence of termination, not proof of its cause. Never retry or adopt a
different process silently: either can hide a failed startup or control a user's
unrelated player.
"""
import time


class PlayerStartupError(RuntimeError):
    pass


def wait_for_player_window(process, inspect, evidence, checkpoint, *,
                           now=lambda: int(time.monotonic() * 1000),
                           sleep=time.sleep, timeout_ms=20000):
    if timeout_ms <= 0:
        raise ValueError("Startup timeout must be positive")
    evidence.update(pid=process.pid, started_at=now(), outcome="waiting",
                    exit_code=None, snapshots=[])
    deadline = evidence["started_at"] + timeout_ms
    checkpoint(evidence)

    def finish(outcome, code=None):
        evidence.update(outcome=outcome, ended_at=now(), exit_code=code)
        if code is not None:
            evidence["exit_code_hex"] = f"0x{code & 0xffffffff:08x}"
        checkpoint(evidence)

    while now() < deadline:
        code = process.poll()
        if code is not None:
            finish("exited_before_window", code)
            raise PlayerStartupError(f"Owned player exited before window: pid={process.pid}, code={code}, hex={evidence['exit_code_hex']}")
        began = now()
        try:
            observed = inspect()
        except Exception as error:
            observed = {"inspection_error": {"type": type(error).__name__, "message": str(error)}}
        snapshot = dict(observed, begin=began, end=now())
        # Keep bounded diagnostics, preserving the first observation and latest
        # state changes. Routine identical polls do not write to disk.
        previous = evidence["snapshots"][-1] if evidence["snapshots"] else None
        if previous is None or observed != {k: v for k, v in previous.items() if k not in ("begin", "end")}:
            if len(evidence["snapshots"]) >= 128:
                del evidence["snapshots"][1]
            evidence["snapshots"].append(snapshot)
            checkpoint(evidence)
        code = process.poll()
        if code is not None:
            finish("exited_before_window", code)
            raise PlayerStartupError(f"Owned player exited during window inspection: pid={process.pid}, code={code}, hex={evidence['exit_code_hex']}")
        window = next((w for w in observed.get("windows", [])
                       if w.get("pid") == process.pid and w.get("cls") == "PotPlayer64"), None)
        if window:
            evidence["hwnd"] = window["hwnd"]
            finish("window_ready")
            return window["hwnd"]
        sleep(.03)
    code = process.poll()
    if code is not None:
        finish("exited_before_window", code)
        raise PlayerStartupError(f"Owned player exited at startup deadline: pid={process.pid}, code={code}, hex={evidence['exit_code_hex']}")
    finish("window_timeout")
    raise PlayerStartupError(f"Owned player window timeout: pid={process.pid}")
