#!/usr/bin/env python3
import argparse
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

TIME_RE = re.compile(r"\btime_us=(\d+)")
DURATION_RE = re.compile(r"\bduration_ms=(\d+)")
ID_RE = re.compile(r"\bid=(0x[0-9a-fA-F]+)")
RETURN_RE = re.compile(r"\breturn=(0x[0-9a-fA-F]+|\S+)")
THREAD_RE = re.compile(r"\bthread=(\S+)")
GPU_EVENTS = ("interrupt-trigger", "release-mem-interrupt")


@dataclass
class Event:
    index: int
    kind: str
    text: str
    time_us: int | None = None
    duration_ms: int | None = None
    semaphore: str | None = None
    return_address: str | None = None
    thread: str | None = None


def parse(lines: Iterable[str]) -> list[Event]:
    events = []
    for index, raw in enumerate(lines):
        text = raw.strip()
        kind = next((name for name in (
            "cp-counter-write", "guest-wait", "semaphore-signal", *GPU_EVENTS
        ) if name in text), None)
        if kind is None:
            continue
        timestamp = TIME_RE.search(text)
        duration = DURATION_RE.search(text)
        semaphore = ID_RE.search(text)
        return_address = RETURN_RE.search(text)
        thread = THREAD_RE.search(text)
        events.append(Event(
            index=index,
            kind=kind,
            text=text,
            time_us=int(timestamp.group(1)) if timestamp else None,
            duration_ms=int(duration.group(1)) if duration else None,
            semaphore=semaphore.group(1) if semaphore else None,
            return_address=return_address.group(1) if return_address else None,
            thread=thread.group(1) if thread else None,
        ))
    return events


def preceding_gpu_event(events: list[Event], signal: Event) -> Event | None:
    if signal.time_us is None:
        return None
    previous_time = None
    for event in reversed(events[:signal.index]):
        if event.kind in GPU_EVENTS:
            if event.time_us is not None:
                age = signal.time_us - event.time_us
                return event if 0 <= age <= 100_000 else None
            if previous_time is None or signal.time_us - previous_time <= 100_000:
                return event
            return None
        if event.time_us is not None:
            previous_time = event.time_us
    return None


def stalls(events: list[Event]) -> list[str]:
    waits = [event for event in events if event.kind == "guest-wait" and
             event.duration_ms is not None and event.duration_ms > 5_000 and event.semaphore]
    signals = [event for event in events if event.kind == "semaphore-signal"]
    groups: dict[tuple[str, int | None], tuple[list[Event], Event | None]] = {}
    for wait in waits:
        candidates = [candidate for candidate in signals if candidate.semaphore == wait.semaphore]
        signal = min(candidates, key=lambda candidate: abs(candidate.index - wait.index),
                     default=None)
        key = (wait.semaphore, signal.index if signal else wait.index)
        if key in groups:
            groups[key][0].append(wait)
        else:
            groups[key] = ([wait], signal)

    output = []
    for (semaphore, _), (group, signal) in groups.items():
        duration = max(wait.duration_ms for wait in group)
        start = (f"{signal.time_us - duration * 1_000}"
                 if signal and signal.time_us is not None else "unknown")
        threads = sorted({wait.thread or "unknown" for wait in group})
        returns = sorted({wait.return_address or "unknown" for wait in group})
        gpu = preceding_gpu_event(events, signal) if signal else None
        signaller = signal.return_address if signal else None
        output.append(
            f"stall start_us={start} duration_ms={duration} waiting_threads={len(threads)} "
            f"semaphore={semaphore} signaller_return={signaller or 'unknown'} "
            f"guest_returns={','.join(returns)} preceding_gpu_event={gpu.text if gpu else 'none'}"
        )
    return output


def counter_gaps(events: list[Event]) -> list[str]:
    writes = [event for event in events if event.kind == "cp-counter-write" and
              event.time_us is not None]
    output = []
    for previous, current in zip(writes, writes[1:]):
        gap = current.time_us - previous.time_us
        if gap > 3_000_000:
            output.append(
                f"cp-gap start_us={previous.time_us} duration_ms={gap // 1_000} "
                f"next_cp_counter_write_us={current.time_us}"
            )
    return output


def main(paths: list[str]) -> None:
    for path in paths:
        events = parse(Path(path).read_text(encoding="utf-8", errors="replace").splitlines())
        for line in counter_gaps(events):
            print(line)
        for line in stalls(events):
            print(line)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Summarize stalls in kyty-diag.txt")
    parser.add_argument("paths", nargs="+", help="diagnostic log path")
    args = parser.parse_args()
    main(args.paths)
