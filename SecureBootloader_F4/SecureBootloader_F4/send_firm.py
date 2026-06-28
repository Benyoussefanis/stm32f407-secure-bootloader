import sys, os, struct, time, hashlib, glob, serial
from Crypto.Cipher import AES
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature

PKT_HANDSHAKE    = 0x01
PKT_UPDATE_START = 0x04
PKT_DATA         = 0x05
PKT_ACK          = 0x06
PKT_NACK         = 0x07
PKT_UPDATE_DONE  = 0x08
SYNC_1, SYNC_2   = 0xAA, 0x55

BAUD  = 115200
CHUNK = 128
PORT  = "COM11"


def stm32_crc32(data):
    crc = 0xFFFFFFFF
    for i in range(0, len(data), 4):
        word = struct.unpack_from("<I", data, i)[0]
        crc ^= word
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def sign_firmware(fw_bytes, keyfile="private.pem"):
    key_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), keyfile)
    if not os.path.exists(key_path):
        print(f"ERROR: {keyfile} not found")
        sys.exit(1)
    with open(key_path, "rb") as f:
        private_key = serialization.load_pem_private_key(f.read(), password=None)
    der_sig = private_key.sign(fw_bytes, ec.ECDSA(hashes.SHA256()))
    r, s = decode_dss_signature(der_sig)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def derive_key(uid_bytes, nonce_bytes):
    return hashlib.sha256(uid_bytes + nonce_bytes).digest()[:16]


def encrypt_chunk(key, nonce, seq, plaintext):
    iv = bytearray(nonce)
    iv[8]  ^= (seq >> 24) & 0xFF
    iv[9]  ^= (seq >> 16) & 0xFF
    iv[10] ^= (seq >>  8) & 0xFF
    iv[11] ^= (seq      ) & 0xFF
    cipher = AES.new(key, AES.MODE_GCM, nonce=bytes(iv))
    ct, tag = cipher.encrypt_and_digest(plaintext)
    return ct + tag


def build_packet(ptype, seq, data=b""):
    header = bytes([SYNC_1, SYNC_2, ptype, seq,
                    (len(data) >> 8) & 0xFF, len(data) & 0xFF])
    frame = header + data
    while len(frame) % 4:
        frame += b"\x00"
    crc = stm32_crc32(frame)
    return header + data + struct.pack("<I", crc)


def send_packet(ser, ptype, seq, data=b"", label=""):
    pkt = build_packet(ptype, seq, data)
    print(f"  -> {label} (seq={seq}, {len(data)} bytes)")
    for attempt in range(3):
        ser.write(pkt)
        deadline = time.time() + 5.0
        buf = bytearray()
        while time.time() < deadline:
            b = ser.read(1)
            if not b:
                continue
            buf += b
            if len(buf) >= 10:
                for i in range(len(buf) - 9):
                    if buf[i] == SYNC_1 and buf[i+1] == SYNC_2:
                        if buf[i+2] == PKT_ACK and buf[i+3] == seq:
                            print(f"  <- ACK")
                            return True
                        if buf[i+2] == PKT_NACK:
                            print(f"  <- NACK (attempt {attempt+1})")
                            break
                buf = buf[-9:]
        print(f"  <- no reply (attempt {attempt+1})")
    return False


def main():
    binfile  = None
    port     = PORT
    version  = None
    wrongkey = False

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg.upper().startswith("COM") or arg.startswith("/dev/"):
            port = arg
        elif arg.startswith("--version="):
            version = int(arg.split("=", 1)[1])
        elif arg == "--version" and i + 1 < len(args):
            version = int(args[i + 1])
            i += 1
        elif arg == "--wrongkey":
            wrongkey = True
        elif arg.lower().endswith(".bin"):
            binfile = arg
        i += 1

    here = os.path.dirname(os.path.abspath(__file__))

    if binfile and not os.path.exists(binfile):
        for parent in [here, os.path.join(here, ".."), os.path.join(here, "..", "..")]:
            candidate = os.path.join(parent, binfile)
            if os.path.exists(candidate):
                binfile = candidate
                break

    if not binfile:
        for search in [here, os.path.join(here, ".."), os.path.join(here, "..", "..")]:
            found = glob.glob(os.path.join(search, "*.bin"))
            if found:
                binfile = found[0]
                break

    if not binfile or not os.path.exists(binfile):
        print("ERROR: no .bin file found")
        sys.exit(1)

    if version is None:
        print("ERROR: --version required")
        print("  python send_firm.py Application.bin COM11 --version=2")
        sys.exit(1)

    fw = open(binfile, "rb").read()

    print(f"\nFirmware : {os.path.basename(binfile)}  ({len(fw)} bytes)")
    print(f"Port     : {port}  @  {BAUD} baud")
    print(f"Version  : {version}\n")

    keyfile = "wrong_private.pem" if wrongkey else "private.pem"
    if wrongkey:
        print("[1/5] Signing with WRONG key ...")
    else:
        print("[1/5] Signing ...")
    sig = sign_firmware(fw, keyfile)
    print(f"      OK  ({sig.hex()[:16]}...)\n")

    print("[2/5] Opening serial port ...")
    ser = serial.Serial(port, BAUD, timeout=0.5)
    time.sleep(0.2)
    ser.reset_input_buffer()

    print("[3/5] Handshake ...")
    if not send_packet(ser, PKT_HANDSHAKE, 0, label="HANDSHAKE"):
        print("Handshake failed. Hold blue button and reset.")
        ser.close(); sys.exit(1)

    time.sleep(0.05)
    uid = ser.read(12)
    if len(uid) != 12:
        print(f"UID read failed, got {len(uid)} bytes")
        ser.close(); sys.exit(1)
    print(f"      Device UID : {uid.hex()}")

    nonce = os.urandom(12)
    key   = derive_key(uid, nonce)
    print(f"      Nonce      : {nonce.hex()}")
    print(f"      Session key OK\n")

    print("[4/5] Starting update ...")
    if not send_packet(ser, PKT_UPDATE_START, 0, nonce, label="UPDATE_START"):
        print("UPDATE_START failed")
        ser.close(); sys.exit(1)
    print()

    print("[5/5] Sending chunks ...")
    seq   = 1
    total = (len(fw) + CHUNK - 1) // CHUNK
    for off in range(0, len(fw), CHUNK):
        chunk = fw[off : off + CHUNK]
        enc   = encrypt_chunk(key, nonce, seq, chunk)
        print(f"  chunk {seq}/{total}", end="  ")
        if not send_packet(ser, PKT_DATA, seq & 0xFF, enc, label="DATA"):
            print(f"Failed at chunk {seq}.")
            ser.close(); sys.exit(1)
        seq += 1

    print("\nSending UPDATE_DONE ...")
    padded    = fw + b"\xFF" * ((-len(fw)) % 4)
    image_crc = stm32_crc32(padded)
    payload   = struct.pack("<I", image_crc) + struct.pack("<I", version) + sig
    send_packet(ser, PKT_UPDATE_DONE, 0, payload, label="UPDATE_DONE")

    print("\nWaiting for board ...")
    ser.timeout = 8.0
    raw = ser.read(256)
    if raw:
        text = "".join(chr(c) if 32 <= c < 127 or c in (10, 13) else "." for c in raw)
        result = text.strip()
        print(f"Board: {result}")
        if "VERIFY FAILED" in result:
            print("security check failed: signature rejected")
        elif "ROLLBACK BLOCKED" in result:
            print("security check failed: version too old")
    else:
        print("No response, check LEDs")

    ser.close()


if __name__ == "__main__":
    main()
