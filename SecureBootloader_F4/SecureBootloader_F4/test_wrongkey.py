import sys, os, subprocess

port = "COM11"
version = None

args = sys.argv[1:]
i = 0
while i < len(args):
    arg = args[i]
    if arg.upper().startswith("COM") or arg.startswith("/dev/"):
        port = arg
    elif arg.startswith("--version="):
        version = arg.split("=", 1)[1]
    elif arg == "--version" and i + 1 < len(args):
        version = args[i + 1]
        i += 1
    i += 1

if version is None:
    print("usage: python test_wrongkey.py COM7 --version=2")
    print("version must match or exceed what is stored on the board")
    sys.exit(1)

here = os.path.dirname(os.path.abspath(__file__))

def run(cmd):
    subprocess.run(cmd, shell=True, cwd=here)

print("hold blue button + reset, then press Enter")
input()
run("python make_bad.py Application.bin")
run(f"python send_firm.py Application.bin {port} --version={version} --wrongkey")
