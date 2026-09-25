import ctypes

d3d9 = ctypes.WinDLL("d3d9.dll")
Direct3DCreate9 = d3d9.Direct3DCreate9
Direct3DCreate9.restype = ctypes.c_void_p
Direct3DCreate9.argtypes = [ctypes.c_uint]

pD3D = Direct3DCreate9(32)

class D3DPRESENT_PARAMETERS(ctypes.Structure):
    _fields_ = [
        ("BackBufferWidth", ctypes.c_uint),
        ("BackBufferHeight", ctypes.c_uint),
        ("BackBufferFormat", ctypes.c_uint),
        ("BackBufferCount", ctypes.c_uint),
        ("MultiSampleType", ctypes.c_uint),
        ("MultiSampleQuality", ctypes.c_ulong),
        ("SwapEffect", ctypes.c_uint),
        ("hDeviceWindow", ctypes.c_void_p),
        ("Windowed", ctypes.c_int),
        ("EnableAutoDepthStencil", ctypes.c_int),
        ("AutoDepthStencilFormat", ctypes.c_uint),
        ("Flags", ctypes.c_ulong),
        ("FullScreen_RefreshRateInHz", ctypes.c_uint),
        ("PresentationInterval", ctypes.c_uint)
    ]

# First, kill the stuck stellaris.exe PID 86652!
import os
os.system("taskkill /F /IM stellaris.exe")

user32 = ctypes.windll.user32
hwnd = user32.CreateWindowExA(0, b"STATIC", b"test", 0x80000000, 0, 0, 100, 100, 0, 0, 0, 0)

vt = ctypes.cast(pD3D, ctypes.POINTER(ctypes.c_void_p)).contents.value
fn_CreateDevice = ctypes.cast(vt + 16 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
proto_create = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(D3DPRESENT_PARAMETERS), ctypes.POINTER(ctypes.c_void_p))
f_create = proto_create(fn_CreateDevice)

for adapter in [0, 1]:
    pp = D3DPRESENT_PARAMETERS()
    pp.BackBufferWidth = 1920
    pp.BackBufferHeight = 1080
    pp.BackBufferFormat = 22 # D3DFMT_X8R8G8B8
    pp.BackBufferCount = 1
    pp.MultiSampleType = 0
    pp.MultiSampleQuality = 0
    pp.SwapEffect = 1 # D3DSWAPEFFECT_DISCARD
    pp.hDeviceWindow = hwnd
    pp.Windowed = 1
    pp.EnableAutoDepthStencil = 0
    pp.Flags = 0
    pp.FullScreen_RefreshRateInHz = 0
    pp.PresentationInterval = 0 # D3DPRESENT_INTERVAL_DEFAULT
    
    pDev = ctypes.c_void_p()
    hr = f_create(pD3D, adapter, 1, hwnd, 0x40, ctypes.byref(pp), ctypes.byref(pDev))
    print(f"Adapter {adapter} CreateDevice: hr={hex(hr & 0xFFFFFFFF)}, pDev={hex(pDev.value or 0)}")
