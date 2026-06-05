from PIL import Image
icon = Image.open(r"C:\Users\akans\Downloads\mic_icon_preview.png").convert("RGBA")
bg = Image.new("RGBA",(80,80),(20,20,20,255))
bg.alpha_composite(icon)
bg.convert("RGB").resize((160,160)).save(r"C:\Users\akans\Downloads\mic_on_dark.png")
print("ok")
