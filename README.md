# STM32F407 Secure Bootloader

> bare-metal bootloader with AES-128-GCM encrypted transfer, ECDSA P-256 signature verification, and anti-rollback protection — built from scratch on the STM32F407G-DISC1

i got into embedded because i wanted to understand what actually happens inside a microcontroller, not just use libraries. i started digging into how the STM32 boots, how it manages memory, how the CPU works at the register level. at some point i found the bootloader, the piece of code that runs before everything else and decides what gets executed. that felt like the real starting point. if you control the bootloader, you control the chip.

at the same time i had been reading about how companies protect firmware updates. most devices out in the field get updated over a cable, and if that process is not secured anyone can push malicious firmware. i decided to build a bootloader that does it properly: encrypted transfer, signature verification, anti-rollback. not to follow a tutorial but to actually understand each piece and implement it myself.

---

## what it does

- **AES-128-GCM** encryption over UART, session key derived from board UID and a random nonce each time
- **ECDSA P-256** signature checked before anything is written to flash
- version number stored in flash, older firmware gets rejected
- firmware goes into a separate download slot first, existing app is untouched until everything passes
- if power cuts during update the board boots the old app normally

---

## threat model

protects against someone intercepting or injecting on the UART line during an update, and against someone with a copy of the firmware binary trying to modify and reflash it. the session key changes every time so replaying a captured session does not work. the signature means a modified binary gets rejected even if the attacker has the encrypted transfer.

---

## how the handshake works

```
PC                                  STM32
 |                                     |
 |<-- nonce (12 bytes) ----------------|
 |                                     |
 |    session_key = SHA256(uid+nonce)[:16]
 |                                     |
 |-- chunk 0 (AES-GCM, IV=nonce^0) -->|
 |-- chunk 1 (AES-GCM, IV=nonce^1) -->|
 |-- chunk N ...                    -->|
 |                                     |
 |-- UPDATE_DONE (CRC + version + sig)->|
 |                                     |   verify CRC
 |                                     |   verify ECDSA signature
 |                                     |   check version >= stored
 |<-- ACK or NACK --------------------|
```

each chunk has its own GCM authentication tag. a tampered chunk fails immediately and the update aborts before anything is written to the app region.

---

## hardware

| part | detail |
|------|--------|
| board | STM32F407G-DISC1 |
| UART adapter | FTDI FT232 USB-to-UART |
| wiring | FTDI RX → PA2 · FTDI TX → PA3 · GND → GND |

> the ST-LINK on the discovery board only does programming — not UART. you need an FTDI adapter.

---

## setup

**what you need:** STM32CubeProgrammer, Python 3, Git (includes OpenSSL on Windows), an FTDI adapter.

### step 1 — flash the bootloader

open STM32CubeProgrammer and connect via the ST-LINK USB cable.
go to **Erasing & Programming**, set start address to `0x08000000`, browse to `Bootloader/Debug/Bootloader.bin`, click **Start Programming**.

### step 2 — flash the application

set start address to `0x08008000`, browse to `Application/Debug/Application.bin`, flash it.
the board now has something to run on normal boot. after this, all updates go through `send_firm.py`.

### step 3 — install Python packages

```bash
pip install pyserial pycryptodome cryptography
```

### step 4 — generate your key pair

OpenSSL is needed. on Windows it comes with Git but is not added to PATH by default.
if `openssl` is not recognized, run with the full path:

```bash
"C:\Program Files\Git\usr\bin\openssl.exe" ecparam -name prime256v1 -genkey -noout -out private.pem
```

or add `C:\Program Files\Git\usr\bin` to PATH, then:

```bash
openssl ecparam -name prime256v1 -genkey -noout -out private.pem
openssl ec -in private.pem -pubout -out public.pem
```

> `private.pem` is the signing key. never share it or commit it.

### step 5 — generate the C header and rebuild

```bash
python gen_pubkey.py public.pem Bootloader/Core/Inc/pubkey.h
```

then open the Bootloader project in STM32CubeIDE, do **Project → Clean**, then **Build**.

> skipping Clean means the old public key stays compiled in and every update will fail with `VERIFY FAILED`.

flash the new bootloader binary to `0x08000000` again.

### step 6 — wire the FTDI and enter bootloader mode

connect FTDI: RX → PA2, TX → PA3, GND → any GND pin.
plug into the PC and check Device Manager for the COM port.
**hold the blue button and press reset** — orange LED means the bootloader is waiting.

### step 7 — send firmware

```bash
python send_firm.py Application.bin COM7 --version=1
```

replace `COM7` with the actual FTDI port. chunks transfer, then `INSTALL OK`. green LED turns on and the board reboots.

to see UART output open PuTTY or TeraTerm on the FTDI COM port at **115200 baud 8N1**.

---

## version numbers

the bootloader stores the last installed version in flash sector 7. sending a lower version returns `ROLLBACK BLOCKED`. always use a version equal to or higher than the last one flashed. if unsure what is stored, use `--version=10`.

---

## using the pre-built binaries

`Application.bin` in the repo root is already built. the bootloader binary is in `Bootloader/Debug/`.
to test without generating keys, `private.pem` matching the compiled public key is needed — that file is not in the repo.

if you generate your own keys, rebuild the bootloader after `gen_pubkey.py` or the signature check will always fail.

---

## LEDs

| LED | pin | meaning |
|-----|-----|---------|
| orange | PD13 | update mode, waiting for data |
| blue | PD15 | receiving chunks |
| green | PD12 | install OK, rebooting |
| red | PD14 | something failed — check UART output at 115200 baud |

---

## flash layout

```
0x08000000  bootloader     sectors 0-1     32 KB
0x08008000  application    sectors 2-5    224 KB
0x08040000  download slot  sector 6       128 KB
0x08060000  version store  sector 7         8 bytes
```

firmware downloads into sector 6 first. only after CRC, signature, and version all pass does the bootloader erase sectors 2–5 and copy from there.

---

## security tests

### test 1 — wrong signature

```bash
python test_wrongkey.py COM7 --version=2
```

generates a random key, signs the firmware with it and sends it. board prints `VERIFY FAILED` and red LED.
version must be valid so the board reaches the signature check, not the version check.

### test 2 — anti-rollback

```bash
python test_rollback.py COM7
```

sends firmware with version 0. board prints `ROLLBACK BLOCKED` and red LED.

> hold blue button and press reset before each test. the scripts wait for Enter.

---

## issues i ran into

a full list is in [issues.txt](issues.txt). the main ones:

| issue | cause | fix |
|-------|-------|-----|
| `openssl` not recognized on Windows | Git installs it but doesn't add it to PATH | add `C:\Program Files\Git\usr\bin` to PATH or use the full path |
| `VERIFY FAILED` after new keys | skipped Project Clean — old key still compiled in | always **Project → Clean → Build** after `gen_pubkey.py` |
| `ROLLBACK BLOCKED` during wrong key test | version sent was lower than stored — rejected before reaching signature check | always pass a valid version with the wrong key test |
| chunk 1 retries 2–3 times | board erasing download slot when first chunk arrives | normal — retry logic handles it automatically |
| `make_bad.py` accepted by bootloader | original version re-signed with the real key after flipping a byte | rewrote it to sign with a random throwaway key |
| `VERIFY FAILED` on fresh clone | sent `Application.bin` signed with original key after generating new ones | rebuild after setting up new keys |

---

## debugging

UART output at **115200 baud 8N1** on the FTDI COM port in PuTTY or TeraTerm shows what the bootloader is doing at each step.

**expressions I watched in CubeIDE:**

| expression | what it shows |
|------------|---------------|
| `FLASH->SR` | BSY bit and error flags (PGSERR, WRPERR) |
| `USART2->SR` | RXNE, TXE, ORE flags |
| `SCB->VTOR` | vector table address — checked this was updated before jump |
| `uwTick` | HAL tick counter — if stuck at 0, SysTick is not running |
| `rx_buf.head` | increments on every received byte |
| `rx_buf.tail` | moves when main loop reads — lag shows backpressure |
| `version_stored` | version number read back from flash sector 7 |
| `flash_status` | local variable to catch BSY timeout |

**memory browser addresses:**

| address | what to look for |
|---------|-----------------|
| `0x08000000` | first word = stack pointer, should be `0x20020000` on F407 |
| `0x08008000` | first word = app stack pointer, `0xFFFFFFFF` means nothing is flashed |
| `0x08060000` | version bytes in sector 7, readable as a 32-bit word |
| `0x40023C00` | FLASH registers — SR at `+0x0C`, CR at `+0x10` |

also used the memory browser to read the public key bytes at the address `pubkey.h` compiles to, to confirm a clean build had actually picked up the new key.

> if the blue button is pressed during a transfer, the board resets. the old firmware stays untouched.

---

## what i want to add next

full list in [ideas_next.txt](ideas_next.txt). the main one is a second MCU acting as a hardware gate that verifies the signature before the firmware even reaches the main board — making the signature check independent of the bootloader itself.

---

## libraries

| library | author | license | use |
|---------|--------|---------|-----|
| [micro-ecc](https://github.com/kmackay/micro-ecc) | Kenneth MacKay | BSD-2-Clause | ECDSA P-256 |
| [tiny-aes-c](https://github.com/kokke/tiny-AES-c) | Kokke | Unlicense | AES-128 block cipher |
| [sha256](https://github.com/B-Con/crypto-algorithms) | Brad Conte | public domain | SHA-256 |
| aes_gcm.c | written by me | — | GCM layer on top of tiny-aes-c |
