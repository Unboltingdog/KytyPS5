import contextlib
import io
import tempfile
import unittest
from pathlib import Path

import diag_timeline


class DiagTimelineTest(unittest.TestCase):
    def test_gaps_stalls_and_preceding_gpu_event(self):
        log = "\n".join([
            "cp-counter-write time_us=1000000 queue=0 submit=1",
            "cp-counter-write time_us=5500000 queue=0 submit=2",
            "guest-wait name=KernelWaitSema duration_ms=6000 timeout_us=inf outcome=signalled result=0 id=0x00000000000000aa return=0x140000001 thread=7",
            "guest-wait name=KernelWaitSema duration_ms=6100 timeout_us=inf outcome=signalled result=0 id=0x00000000000000aa return=0x140000002 thread=8",
            "guest-wait name=KernelWaitSema duration_ms=5000 timeout_us=inf outcome=signalled result=0 id=0x00000000000000bb return=0x140000003 thread=9",
            "cp-counter-write time_us=16095000 queue=0 submit=3",
            "interrupt-trigger event=0x00000001 context=0x00000002 registrations=1",
            "semaphore-signal time_us=16100000 id=0x00000000000000aa count=1 result=0 return=0x140000abc thread=3",
            "guest-wait name=KernelWaitSema duration_ms=7000 timeout_us=inf outcome=signalled result=0 id=0x00000000000000cc return=0x140000004 thread=10",
        ]) + "\n"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "kyty-diag.txt"
            path.write_text(log, encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                diag_timeline.main([str(path)])

        lines = output.getvalue().splitlines()
        self.assertEqual(len(lines), 4)
        self.assertEqual(lines[0],
            "cp-gap start_us=1000000 duration_ms=4500 next_cp_counter_write_us=5500000")
        self.assertEqual(lines[1],
            "cp-gap start_us=5500000 duration_ms=10595 next_cp_counter_write_us=16095000")
        self.assertIn("start_us=10000000 duration_ms=6100 waiting_threads=2", lines[2])
        self.assertIn("semaphore=0x00000000000000aa signaller_return=0x140000abc", lines[2])
        self.assertIn("guest_returns=0x140000001,0x140000002", lines[2])
        self.assertIn("preceding_gpu_event=interrupt-trigger event=0x00000001", lines[2])
        self.assertIn("start_us=unknown duration_ms=7000 waiting_threads=1", lines[3])
        self.assertIn("signaller_return=unknown", lines[3])
        self.assertIn("preceding_gpu_event=none", lines[3])

    def test_signal_can_precede_awakened_wait_record(self):
        events = diag_timeline.parse([
            "semaphore-signal time_us=7000000 id=0xaa return=0xabc thread=3\n",
            "guest-wait name=KernelWaitSema duration_ms=6000 timeout_us=inf outcome=signalled result=0 id=0xaa return=0xdef thread=7\n",
        ])
        lines = diag_timeline.stalls(events)
        self.assertEqual(len(lines), 1)
        self.assertIn("start_us=1000000 duration_ms=6000", lines[0])
        self.assertIn("signaller_return=0xabc", lines[0])

    def test_timestamped_gpu_event_must_be_within_100_ms(self):
        events = diag_timeline.parse([
            "interrupt-trigger time_us=99000 event=1\n",
            "semaphore-signal time_us=200000 id=0xaa return=0x1 thread=1\n",
        ])
        self.assertIsNone(diag_timeline.preceding_gpu_event(events, events[1]))
        events[0].time_us = 100000
        self.assertEqual(diag_timeline.preceding_gpu_event(events, events[1]), events[0])


if __name__ == "__main__":
    unittest.main()
