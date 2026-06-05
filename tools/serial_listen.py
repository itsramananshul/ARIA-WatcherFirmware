import serial, time
try:
    s = serial.Serial("COM6", 115200, timeout=1)
except Exception as e:
    print("SERIAL OPEN FAILED:", e); raise SystemExit
s.reset_input_buffer()
end = time.time() + 40
n=0; last=time.time()
print("--- 40s capture: click Chat once + wait. (heartbeat every 8s) ---")
while time.time() < end:
    try: line = s.readline().decode("utf-8","replace").rstrip()
    except Exception: continue
    if time.time()-last > 8:
        print(f"...[alive {int(time.time()-(end-40))}s]"); last=time.time()
    if not line: continue
    n+=1
    low=line.lower()
    if any(k in low for k in ["main2c","main1c","screen_start","avatar","virscr","virclick","scr_load","_screen_change"]):
        print(">>>", line)
print(f"--- done, {n} total lines seen ---")
s.close()
