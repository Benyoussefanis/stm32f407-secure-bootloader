import sys
from cryptography.hazmat.primitives import serialization

pem_path    = sys.argv[1] if len(sys.argv) > 1 else "public.pem"
header_path = sys.argv[2] if len(sys.argv) > 2 else "pubkey.h"

with open(pem_path, "rb") as f:
    pub = serialization.load_pem_public_key(f.read())

raw = pub.public_bytes(
    serialization.Encoding.X962,
    serialization.PublicFormat.UncompressedPoint
)
key_bytes = raw[1:]
assert len(key_bytes) == 64

hex_lines = []
for i in range(0, 64, 16):
    chunk = key_bytes[i:i+16]
    hex_lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")

header = f"""#ifndef PUBKEY_H
#define PUBKEY_H

#include <stdint.h>

static const uint8_t PUBLIC_KEY[64] = {{
{chr(10).join(hex_lines)}
}};

#endif
"""

with open(header_path, "w") as f:
    f.write(header)

print(f"wrote {header_path}")
