- stm32f407 secure bootloader

i got into embedded because i wanted to understand what actually happens inside a microcontroller, not just use libraries. i started digging into how the STM32 boots, how it manages memory, how the CPU works at the register level. at some point i found the bootloader, the piece of code that runs before everything else and decides what gets executed. that felt like the real starting point. if you control the bootloader, you control the chip.

at the same time i had been reading about how companies protect firmware updates. most devices out in the field get updated over the air or over a cable, and if that process is not secured anyone can push malicious firmware. i decided to build a bootloader that does it properly: encrypted transfer, signature verification, anti-rollback. not to follow a tutorial but to actually understand each piece and implement it myself.


- what it does

- AES-128-GCM encryption over UART, session key derived from board UID and a random nonce each time
- ECDSA P-256 signature checked before anything is written to flash
- version number stored in flash, older firmware gets rejected
- firmware goes into a separate download slot first, existing app is untouched until everything passes
- if power cuts during update the board boots the old app normally


- threat model

protects against someone intercepting or injecting on the UART line during an update, and against someone with a copy of the firmware binary trying to modify and reflash it. the session key changes every time so replaying a captured session does not work. the signature means a modified binary gets rejected even if the attacker has the encrypted transfer.


- how the handshake works

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

each chunk has its own GCM authentication tag. a tampered chunk fails immediately and the update aborts before anything is written to the app region.


- hardware

- stm32f407g-disc1
- ftdi ft232 usb to uart adapter (the st-link on the discovery board only does programming, not UART)
- wiring: ftdi RX to PA2, ftdi TX to PA3, GND to GND


- setup

what you need: STM32CubeProgrammer, python 3, git (includes openssl on windows), an FTDI adapter.


step 1: flash the bootloader

open STM32CubeProgrammer and connect the board via the ST-LINK USB cable.
go to the Erasing & Programming tab, set start address to 0x08000000, browse to Bootloader/Debug/Bootloader.bin and click Start Programming.


step 2: flash the application

still in STM32CubeProgrammer, set start address to 0x08008000, browse to Application/Debug/Application.bin and flash it.
the board now has something to run on normal boot.
after this all updates go through UART using send_firm.py.


step 3: install python packages

    pip install pyserial pycryptodome cryptography


step 4: generate your key pair

openssl is needed. on windows it comes with git but is not added to PATH by default.
if openssl is not recognized, run it with the full path:

    C:\Program Files\Git\usr\bin\openssl.exe ecparam -name prime256v1 -genkey -noout -out private.pem

or add C:\Program Files\Git\usr\bin to PATH in system environment variables, then:

    openssl ecparam -name prime256v1 -genkey -noout -out private.pem
    openssl ec -in private.pem -pubout -out public.pem

private.pem is the signing key. never share it or commit it.


step 5: generate the C header and rebuild the bootloader

    python gen_pubkey.py public.pem Bootloader/Core/Inc/pubkey.h

then open the Bootloader project in STM32CubeIDE, do Project then Clean, then Build.
skipping the Clean means the old public key stays compiled in and every update will fail with VERIFY FAILED.
flash the new bootloader binary to 0x08000000 again.


step 6: wire the FTDI and enter bootloader mode

connect the FTDI: RX to PA2, TX to PA3, GND to any GND pin.
plug it into the PC, check device manager for the COM port.
hold the blue button and press reset. orange LED means the bootloader is waiting.


step 7: send firmware

    python send_firm.py Application.bin COM7 --version=1

replace COM7 with the actual FTDI port. chunks transfer, then INSTALL OK. green LED turns on and the board reboots.

to see UART output open PuTTY or TeraTerm on the FTDI COM port at 115200 baud 8N1.


- version numbers

the bootloader stores the last installed version in flash sector 7. sending a lower version returns ROLLBACK BLOCKED. always use a version equal to or higher than the last one flashed. if unsure what is stored, use --version=10.


- using the pre-built binaries

Application.bin in the repo root is already built. the bootloader binary is in Bootloader/Debug/.
to test without generating keys, private.pem matching the compiled public key is needed. that file is not in the repo.

if you generate your own keys, rebuild the bootloader after gen_pubkey.py or the signature check will always fail.


- LEDs

    orange PD13   update mode, waiting for data
    blue   PD15   receiving chunks
    green  PD12   install OK, rebooting
    red    PD14   something failed, check UART output at 115200 baud


- flash layout

    0x08000000  bootloader     sectors 0-1    32kb
    0x08008000  application    sectors 2-5   224kb
    0x08040000  download slot  sector 6      128kb
    0x08060000  version store  sector 7        8 bytes

firmware downloads into sector 6 first. only after CRC, signature, and version all pass does the bootloader erase sectors 2-5 and copy from there.


- security tests

test 1: wrong signature

    python test_wrongkey.py COM7 --version=2

generates a random key, signs the firmware with it and sends it. board prints VERIFY FAILED and red LED.
version must be valid so the board reaches the signature check, not the version check.

test 2: anti-rollback

    python test_rollback.py COM7

sends firmware with version 0. board prints ROLLBACK BLOCKED and red LED.

hold blue button and press reset before each test. the scripts wait for Enter.


- issues i ran into

full list is in issues.txt. the main ones:

openssl not recognized on windows: git installs it but does not add it to PATH. add C:\Program Files\Git\usr\bin to PATH or run with the full path.

VERIFY FAILED after generating new keys: skipped Project Clean before building in CubeIDE. the old public key was still compiled in.

ROLLBACK BLOCKED during wrong key test: version sent was lower than stored so the board rejected on version before reaching the signature check. always pass a valid version with the wrong key test.

chunk 1 takes 2-3 attempts before ACK: normal. the board is erasing the download slot when the first chunk arrives. the script retries automatically.

make_bad.py accepted by bootloader: original version flipped a byte then re-signed with the real key so it got accepted. rewrote it to sign with a random throwaway key instead.

VERIFY FAILED on a fresh clone: generated own keys but sent Application.bin signed with the original private key. the compiled public key does not match. rebuild the application after setting up new keys.


- debugging

UART output at 115200 baud 8N1 on the FTDI COM port in PuTTY or TeraTerm shows what the bootloader is doing at each step.

expressions i watched during debug sessions:

    FLASH->SR           BSY bit and error flags (PGSERR, WRPERR)
    USART2->SR          RXNE, TXE, ORE flags
    SCB->VTOR           vector table address, checked this was updated before jump
    uwTick              HAL tick counter, if stuck at 0 SysTick is not running
    rx_buf.head         increments on every received byte
    rx_buf.tail         moves when main loop reads, lag between head and tail shows backpressure
    version_stored      version number read back from flash sector 7
    flash_status        local variable to catch BSY timeout

memory browser addresses i used:

    0x08000000    first word is the stack pointer, should be 0x20020000 on F407
    0x08008000    first word is the app stack pointer, 0xFFFFFFFF means nothing is there
    0x08060000    version bytes in sector 7, readable as a 32-bit word
    0x40023C00    FLASH registers, SR at +0x0C, CR at +0x10

also used the memory browser to read the public key bytes at the address pubkey.h compiles to,
to make sure a clean build had actually picked up the new key.

if the blue button is pressed during a transfer the board resets. the old firmware stays untouched.


- what i want to add next

ideas_next.txt has the full list. the main one is a second mcu acting as a hardware gate that verifies the signature before the firmware even reaches the main board. that would make the signature check independent of the bootloader itself.


- libraries

- micro-ecc by kenneth mackay, ECDSA P-256
- tiny-aes-c by kokke, AES-128
- sha256 by brad conte
- GCM layer written by me on top of tiny-aes-c
