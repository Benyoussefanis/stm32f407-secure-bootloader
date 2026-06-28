demo video - stm32f407 secure bootloader


what the video shows:

the board is already flashed with the bootloader and the application.
at the start the app is running normally, green LED blinking.

pressing the blue button triggers bootloader mode, orange LED turns on.

then send_firm.py is run from the PC with version 2:

  python send_firm.py Application.bin COM11 --version=2

the python output shows the handshake, each chunk transferring, then the board
verifies the signature and version, prints INSTALL OK, green LED turns on.
board reboots back into the app.

then the board is reset into bootloader mode again, this time send_firm.py is run
with version 0:

  python send_firm.py Application.bin COM11 --version=0

the board detects the version is lower than what is stored, prints ROLLBACK BLOCKED,
red LED turns on. the old app is untouched, board boots back into it normally.

the python script output alone is enough to confirm what happened.
the red and green LEDs give visual confirmation on the hardware side.
