"""Calls one of the mod's exported calls in a running game, the way another plugin would.

    python Tools/VR/plugin_call.py <pid> <export> [<number> [<number>]]

A small piece of code runs in the game on a thread of its own: GetModuleHandleW(nullptr), GetProcAddress, then the call.
So a run also proves that a plugin can find the call. It prints 0 or 1, or `missing` when the export is not found.
Importable: call(pid, name, first, second).
"""
import ctypes
import struct
import sys
from ctypes import wintypes

PROCESS_ALL = 0x1FFFFF
MEM_COMMIT_RESERVE = 0x3000
MEM_RELEASE = 0x8000
PAGE_EXECUTE_READWRITE = 0x40
MISSING = 0xFF

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
kernel32.GetProcAddress.restype = ctypes.c_void_p
kernel32.GetProcAddress.argtypes = [wintypes.HMODULE, ctypes.c_char_p]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
kernel32.CreateRemoteThread.restype = wintypes.HANDLE
kernel32.CreateRemoteThread.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]


def shim(aNameAddress, aFirst, aSecond):
    """x64 code: find the export on the main module and call it with two numbers. Returns its bool, or 0xFF when missing."""
    get_module = kernel32.GetProcAddress(kernel32._handle, b'GetModuleHandleW')
    get_proc = kernel32.GetProcAddress(kernel32._handle, b'GetProcAddress')
    code = b'\x48\x83\xEC\x28'                                # sub rsp, 0x28
    code += b'\x31\xC9'                                       # xor ecx, ecx
    code += b'\x48\xB8' + struct.pack('<Q', get_module)       # mov rax, GetModuleHandleW
    code += b'\xFF\xD0'                                       # call rax
    code += b'\x48\x89\xC1'                                   # mov rcx, rax
    code += b'\x48\xBA' + struct.pack('<Q', aNameAddress)     # mov rdx, name
    code += b'\x48\xB8' + struct.pack('<Q', get_proc)         # mov rax, GetProcAddress
    code += b'\xFF\xD0'                                       # call rax
    code += b'\x48\x85\xC0'                                   # test rax, rax
    code += b'\x74\x14'                                       # jz missing
    code += b'\xB9' + struct.pack('<I', aFirst)               # mov ecx, first
    code += b'\xBA' + struct.pack('<I', aSecond)              # mov edx, second
    code += b'\xFF\xD0'                                       # call rax
    code += b'\x0F\xB6\xC0'                                   # movzx eax, al
    code += b'\x48\x83\xC4\x28\xC3'                           # add rsp, 0x28; ret
    code += b'\xB8' + struct.pack('<I', MISSING)              # missing: mov eax, 0xFF
    code += b'\x48\x83\xC4\x28\xC3'                           # add rsp, 0x28; ret
    return code


def call(aPid, aName, aFirst=0, aSecond=0):
    """Returns True, False or None when the game has no such export. A call returning nothing gives a meaningless bool."""
    process = kernel32.OpenProcess(PROCESS_ALL, False, aPid)
    if not process:
        raise OSError('cannot open process %d: %d' % (aPid, ctypes.get_last_error()))
    memory = None
    try:
        name = aName.encode() + b'\0'
        memory = kernel32.VirtualAllocEx(process, None, 4096, MEM_COMMIT_RESERVE, PAGE_EXECUTE_READWRITE)
        if not memory:
            raise OSError('cannot allocate in the game: %d' % ctypes.get_last_error())
        code = shim(memory + 512, aFirst, aSecond)
        written = ctypes.c_size_t()
        if not kernel32.WriteProcessMemory(process, memory, code, len(code), ctypes.byref(written)):
            raise OSError('cannot write the code: %d' % ctypes.get_last_error())
        if not kernel32.WriteProcessMemory(process, memory + 512, name, len(name), ctypes.byref(written)):
            raise OSError('cannot write the name: %d' % ctypes.get_last_error())
        thread = kernel32.CreateRemoteThread(process, None, 0, memory, None, 0, None)
        if not thread:
            raise OSError('cannot start the thread: %d' % ctypes.get_last_error())
        try:
            if kernel32.WaitForSingleObject(thread, 10000) != 0:
                # Left allocated: the thread may still run in that memory.
                memory = None
                raise TimeoutError('the call did not come back in 10 s')
            result = wintypes.DWORD()
            kernel32.GetExitCodeThread(thread, ctypes.byref(result))
        finally:
            kernel32.CloseHandle(thread)
        return None if result.value == MISSING else bool(result.value)
    finally:
        if memory:
            kernel32.VirtualFreeEx(process, memory, 0, MEM_RELEASE)
        kernel32.CloseHandle(process)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    numbers = [int(value, 0) for value in sys.argv[3:5]]
    answer = call(int(sys.argv[1]), sys.argv[2], *numbers)
    print('missing' if answer is None else int(answer))
