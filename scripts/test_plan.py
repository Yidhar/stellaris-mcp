import ctypes
import win32process
import win32api
import psutil
import struct

# Test CAddBuildableToQueueCommand::IsValid with vtable 0x2390F28
# We will do this test via injecting or checking through our loaded DLL over pipe!
# But wait, our stellaris_bridge.dll is already loaded in PID 77744!
# If we update outliner_manager.cpp, recompile and reload the DLL,
# we can call the pipe endpoint immediately!

print("Ready to update outliner_manager.cpp with 0x2390F28!")
