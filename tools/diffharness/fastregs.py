"""Batched register access for a unicorn M68K machine (test speed, 2026-10-11).

`Uc.reg_write` / `reg_read` cost about 3 us each in Python (a ctypes object, a byref and a status check per register); a harness
call writes 17 registers and reads 16 back, and the test suite makes millions of calls.  FastRegs keeps the ctypes buffers of a
fixed register list alive and moves all of them in one `uc_reg_write_batch` / `uc_reg_read_batch` call.  It uses two private
methods of unicorn 2.x (`_do_reg_write_batch`, `_do_reg_read_batch`); `FastRegs.make` returns None when they are missing, and the
callers then fall back to the plain loop.
"""
import ctypes


class FastRegs:
    def __init__(self, uc, reg_ids):
        self.uc = uc
        self.ids = list(reg_ids)
        n = len(self.ids)
        cls = type(uc)._select_reg_class
        self.bufs = [cls(r)(0) for r in self.ids]
        self._idlist = (ctypes.c_int * n)(*self.ids)
        self._ptrs = (ctypes.c_void_p * n)(*(ctypes.c_void_p(ctypes.addressof(b)) for b in self.bufs))
        self._count = ctypes.c_int(n)

    @staticmethod
    def make(uc, reg_ids):
        if not (hasattr(uc, '_do_reg_write_batch') and hasattr(uc, '_do_reg_read_batch') and hasattr(type(uc), '_select_reg_class')):
            return None
        try:
            return FastRegs(uc, reg_ids)
        except Exception:        # pragma: no cover - a unicorn with another internal layout
            return None

    def write(self, values):
        """Write `values` (one per register of the list, in order)."""
        for b, v in zip(self.bufs, values):
            b.value = v
        if self.uc._do_reg_write_batch(self._idlist, self._ptrs, self._count):
            raise RuntimeError('unicorn: register batch write failed')

    def read(self):
        """-> the values of all registers of the list, in order."""
        if self.uc._do_reg_read_batch(self._idlist, self._ptrs, self._count):
            raise RuntimeError('unicorn: register batch read failed')
        return [b.value for b in self.bufs]


def add_mem_hook(uc, htype, fn, begin=1, end=0):
    """`uc.hook_add(htype, cb, begin=, end=)` for a memory access hook whose callback is fn(access, addr, size, value).

    unicorn's own hook_add wraps every callback in two python frames (an exception guard and an argument shuffle) before
    the callback runs; a hook that fires millions of times per test run (every write of an emulated routine) pays that on
    each call.  This registers fn's closure straight as the C callback (`fn` must not raise: the guard is gone).  Falls back
    to the ordinary hook_add when this unicorn has no such internals.  -> the hook handle."""
    try:
        from unicorn.unicorn_py3.unicorn import HOOK_MEM_ACCESS_CFUNC
        do_hook_add = uc._Uc__do_hook_add
    except (ImportError, AttributeError):
        return uc.hook_add(htype, lambda _uc, access, addr, size, value, _ud: fn(access, addr, size, value), begin=begin, end=end)
    cb = HOOK_MEM_ACCESS_CFUNC(lambda _h, access, addr, size, value, _key: fn(access, addr, size, value))
    return do_hook_add(htype, cb, begin, end)          # keeps cb alive
