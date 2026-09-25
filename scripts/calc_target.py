import pefile
import struct
import capstone

# Find out what function 0xA35290 is in the binary
# Let's inspect the caller of 0xA35290 at 0x8004AE and 0x8005CA
# What function is 0x800450 and 0x800500?

source_path = r"d:\stellarismcp\source\stellaris_4.5_source.cpp"

# In 0xAC47B0:
# 0xAC48DD: call 0x7FF7780672b0 (RVA 0x4972B0? Wait, let's calculate target of 0xAC48DD)
# 0xAC48DD is: call 0x7FF7780672b0
# Target RVA: 0x7FF7780672b0 - 0x7FF777BD0000 = 0x4972B0!

print(f"Target of 0xAC48DD: RVA 0x4972B0")

