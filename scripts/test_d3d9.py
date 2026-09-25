import ctypes
from ctypes import wintypes

d3d9 = ctypes.WinDLL("d3d9.dll")
Direct3DCreate9 = d3d9.Direct3DCreate9
Direct3DCreate9.restype = ctypes.c_void_p
Direct3DCreate9.argtypes = [ctypes.c_uint]

pD3D = Direct3DCreate9(32) # D3D_SDK_VERSION = 32
print(f"pD3D: {hex(pD3D) if pD3D else 'NULL'}")

if pD3D:
    # Read vtable of IDirect3D9
    vt = ctypes.cast(pD3D, ctypes.POINTER(ctypes.c_void_p)).contents.value
    print(f"IDirect3D9 vtable: {hex(vt)}")
    # vfunc[3] is GetAdapterCount
    fn_GetAdapterCount = ctypes.cast(vt + 3 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
    proto_count = ctypes.WINFUNCTYPE(ctypes.c_uint, ctypes.c_void_p)
    f_count = proto_count(fn_GetAdapterCount)
    count = f_count(pD3D)
    print(f"Adapter count: {count}")
    
    # Check adapter identifiers
    class D3DADAPTER_IDENTIFIER9(ctypes.Structure):
        _fields_ = [
            ("Driver", ctypes.c_char * 512),
            ("Description", ctypes.c_char * 512),
            ("DeviceName", ctypes.c_char * 32),
            ("DriverVersion", ctypes.c_longlong),
            ("VendorId", ctypes.c_ulong),
            ("DeviceId", ctypes.c_ulong),
            ("SubSysId", ctypes.c_ulong),
            ("Revision", ctypes.c_ulong),
            ("DeviceIdentifier", ctypes.c_byte * 16),
            ("WHQLLevel", ctypes.c_ulong)
        ]
    
    fn_GetAdapterIdentifier = ctypes.cast(vt + 5 * 8, ctypes.POINTER(ctypes.c_void_p)).contents.value
    proto_ident = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_uint, ctypes.c_ulong, ctypes.POINTER(D3DADAPTER_IDENTIFIER9))
    f_ident = proto_ident(fn_GetAdapterIdentifier)
    
    for i in range(count):
        ident = D3DADAPTER_IDENTIFIER9()
        hr = f_ident(pD3D, i, 0, ctypes.byref(ident))
        print(f"Adapter {i}: hr={hex(hr & 0xFFFFFFFF)}, DeviceName={ident.DeviceName.decode('ascii', errors='ignore')}, Desc={ident.Description.decode('ascii', errors='ignore')}")
