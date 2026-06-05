import serial, time
s = serial.Serial("COM6", 115200, timeout=1)
s.reset_input_buffer()
end = time.time() + 25
print("--- 25s RAW: wake device, go to Chat tile, press wheel 2-3x ---")
while time.time() < end:
    try: line = s.readline().decode("utf-8","replace").rstrip()
    except Exception: continue
    if line: print(line[:160])
s.close(); print("--- done ---")
