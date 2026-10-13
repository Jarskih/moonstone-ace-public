"""Tests for tools/emulock.py (the machine-wide emulator lock): exclusion between processes, release on exit, stale info, nesting."""
import os
import subprocess
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import emulock  # noqa: E402

HOLDER = r'''
import sys, time
sys.path.insert(0, %r)
import emulock
with emulock.EmuLock("holder", quiet=True):
    print("locked", flush=True)
    time.sleep(float(sys.argv[1]))
'''


class EmuLockTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='emulock_test_')
        self.path = os.path.join(self.tmp, '.emu.lock')
        self.env = dict(os.environ, MS_EMU_LOCK=self.path)
        self.env.pop('MS_EMU_LOCK_HELD', None)
        self.saved = {k: os.environ.get(k) for k in ('MS_EMU_LOCK', 'MS_EMU_LOCK_HELD')}
        os.environ['MS_EMU_LOCK'] = self.path
        os.environ.pop('MS_EMU_LOCK_HELD', None)
        self.addCleanup(self.restore)

    def restore(self):
        for k, v in self.saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    @staticmethod
    def reap(p):
        p.kill()
        p.wait()
        p.stdout.close()

    def holder(self, seconds):
        p = subprocess.Popen([sys.executable, '-c', HOLDER % os.path.join(ROOT, 'tools'), str(seconds)], env=self.env,
                             stdout=subprocess.PIPE, text=True)
        self.addCleanup(self.reap, p)
        self.assertEqual(p.stdout.readline().strip(), 'locked')
        return p

    def test_second_taker_times_out_while_the_first_holds(self):
        p = self.holder(30)
        with self.assertRaises(TimeoutError) as cm:
            emulock.EmuLock('second', timeout=1, quiet=True).acquire()
        self.assertIn('holder', str(cm.exception))
        self.assertIn(str(p.pid), str(cm.exception))

    def test_released_when_the_holder_dies_and_stale_info_is_overwritten(self):
        p = self.holder(60)
        p.kill()                                     # no clean release: the OS drops the byte lock
        p.wait()
        deadline = time.time() + 10
        lock = emulock.EmuLock('after the crash', timeout=5, quiet=True)
        lock.acquire()
        try:
            self.assertEqual(emulock.read_info(self.path)['who'], 'after the crash')
        finally:
            lock.release()
        self.assertLess(time.time(), deadline)

    def test_nested_taker_inside_the_holder_does_not_block(self):
        with emulock.EmuLock('outer', timeout=2, quiet=True):
            self.assertEqual(os.environ['MS_EMU_LOCK_HELD'], str(os.getpid()))
            with emulock.EmuLock('inner', timeout=1, quiet=True):       # a child process sees the pid in the env: no second lock
                pass
            code = subprocess.run([sys.executable, '-c', 'import sys; sys.path.insert(0, %r); import emulock; '
                                   'emulock.EmuLock("child", timeout=1, quiet=True).acquire()' % os.path.join(ROOT, 'tools')],
                                  env=dict(os.environ), capture_output=True, text=True).returncode
            self.assertEqual(code, 0)
        self.assertNotIn('MS_EMU_LOCK_HELD', os.environ)

    def test_free_after_release_and_off_switch(self):
        with emulock.EmuLock('x', quiet=True):
            pass
        self.assertEqual(emulock.EmuLock('y', timeout=1, quiet=True).acquire().release(), None)
        os.environ['MS_EMU_LOCK'] = 'off'
        self.assertIsNone(emulock.lock_path())
        emulock.EmuLock('z', timeout=1, quiet=True).acquire().release()

    def test_status_reports_holder(self):
        self.holder(30)
        out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'emulock.py'), 'status'], env=self.env,
                             capture_output=True, text=True).stdout
        self.assertIn('HELD', out)
        self.assertIn('holder', out)


if __name__ == '__main__':
    unittest.main()
