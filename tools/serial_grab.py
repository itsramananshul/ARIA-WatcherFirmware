import serial, time
s = serial.Serial("COM6", 115200, timeout=1)
s.setDTR(False); s.setRTS(True); time.sleep(0.12); s.setDTR(False); s.setRTS(False)
s.reset_input_buffer()
end = time.time() + 13
print("--- boot ---")
while time.time() < end:
    try: line = s.readline().decode("utf-8","replace").rstrip()
    except Exception: continue
    if line and any(k in line for k in ["Loaded app from","abort","Guru Meditation","panic","assert"]):
        print(line)
s.close(); print("--- done ---")
