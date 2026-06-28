import sys, os, subprocess

port = sys.argv[1] if len(sys.argv) > 1 else "COM11"
here = os.path.dirname(os.path.abspath(__file__))

def run(cmd):
    subprocess.run(cmd, shell=True, cwd=here)

print("hold blue button + reset, then press Enter")
input()
run(f"python send_firm.py Application.bin {port} --version=0")
