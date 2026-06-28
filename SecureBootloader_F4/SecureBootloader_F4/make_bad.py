from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
import sys

binfile = sys.argv[1] if len(sys.argv) > 1 else "Application.bin"

fake_key = ec.generate_private_key(ec.SECP256R1())
fake_pem = fake_key.private_bytes(
    serialization.Encoding.PEM,
    serialization.PrivateFormat.TraditionalOpenSSL,
    serialization.NoEncryption()
)
open("wrong_private.pem", "wb").write(fake_pem)
print("wrong_private.pem written")
