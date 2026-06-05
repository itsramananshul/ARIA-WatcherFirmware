from PIL import Image, ImageDraw
img = Image.open(r"C:\Users\akans\Downloads\ChatGPT Image Jun 4, 2026, 03_45_08 PM.png").convert("RGBA")
W=H=400
scale=min(W/img.width,H/img.height)
nw,nh=round(img.width*scale),round(img.height*scale)
img=img.resize((nw,nh),Image.LANCZOS)
c=Image.new("RGBA",(W,H),(0,0,0,255))
c.paste(img,((W-nw)//2,(H-nh)//2),img)
d=ImageDraw.Draw(c)
d.ellipse([0,0,W-1,H-1],outline=(60,60,60,255))
c.convert("RGB").save(r"C:\Users\akans\Downloads\aria_logo_preview_400.png")
print("ok")
