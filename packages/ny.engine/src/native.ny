module ny.engine

# Win32 Constants
pub let MB_OK: u32 = (0 as u32)
pub let WS_OVERLAPPEDWINDOW: u32 = (13565952 as u32)
pub let CW_USEDEFAULT: i32 = -2147483648
pub let SW_SHOW: i32 = 5
pub let PM_REMOVE: u32 = (1 as u32)

@interop("kernel32")
pub fn GetModuleHandleA(lpModuleName: text) -> i64
    return (0 as i64)
end fn

@interop("user32")
pub fn MessageBoxA(hWnd: i64, lpText: text, lpCaption: text, uType: u32) -> i32
    return 0
end fn

@interop("user32")
pub fn LoadCursorA(hInstance: i64, lpCursorName: text) -> i64
    return (0 as i64)
end fn

@interop("user32")
pub fn CreateWindowExA(
    dwExStyle: u32,
    lpClassName: text,
    lpWindowName: text,
    dwStyle: u32,
    x: i32, y: i32,
    nWidth: i32, nHeight: i32,
    hWndParent: i64,
    hMenu: i64,
    hInstance: i64,
    lpParam: i64
) -> i64
    return (0 as i64)
end fn

@interop("user32")
pub fn DefWindowProcA(hWnd: i64, Msg: u32, wParam: u64, lParam: i64) -> i64
    return (0 as i64)
end fn

@interop("user32")
pub fn DispatchMessageA(lpMsg: ref<MSG>) -> i64
    return (0 as i64)
end fn

@interop("user32")
pub fn FindWindowA(lpClassName: text, lpWindowName: text) -> i64
    return (0 as i64)
end fn

# Kernel32 Memory
@interop("kernel32")
pub fn GlobalAlloc(uFlags: u32, dwBytes: i64) -> i64
    return (0 as i64)
end fn

@interop("kernel32")
pub fn RtlMoveMemory(destination: i64, source: i64, length: i64)
end fn

# Intrinsics
@interop("std_env")
pub fn vm__get_address(obj: any) -> i64
    return (0 as i64)
end fn

@interop("std_env")
pub fn vm__get_cstr(s: text) -> i64
    return (0 as i64)
end fn

@interop("std_env")
pub fn vm__alloc_bytes(size: i64) -> i64
    return GlobalAlloc((64 as u32), size) # 0x0040 = GMEM_FIXED | GMEM_ZEROINIT
end fn

@interop("std_env")
pub fn vm__write_bytes(dest: i64, offset: i64, src: any) -> i64
end fn
    # src is any, but backend intercepts usually?
    # If not intercepted, we need address.
    # But vm__write_bytes in engine_vulkan took string.
    # Native impl should use RtlMoveMemory if src is address.
    # If src is object, we need address.
    let src_addr = vm__get_address(src)
    # Length? We don't know length of 'any'.
    # This intrinsic is hard to implement generically in Ny without size.
    # But engine_vulkan implementation used specific types.
    # For now, return 0 (stub) or implement if specific?
    # Native backends should use the specialized Vulkan implementation.
    return (0 as i64)
end fn
