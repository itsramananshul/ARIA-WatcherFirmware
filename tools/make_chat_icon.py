from PIL import Image, ImageDraw
W=H=80; S=4
img=Image.new("RGBA",(W*S,H*S),(0,0,0,0)); d=ImageDraw.Draw(img); white=(255,255,255,255)
# speech bubble: rounded rect + tail
bx0,by0,bx1,by1 = 12*S,16*S,68*S,56*S
d.rounded_rectangle([bx0,by0,bx1,by1], radius=12*S, fill=white)
# tail (triangle bottom-left)
d.polygon([(26*S,56*S),(26*S,70*S),(40*S,56*S)], fill=white)
# three dots (cut out as transparent)
for cx in (28,40,52):
    d.ellipse([cx*S-4*S, 34*S-4*S, cx*S+4*S, 34*S+4*S], fill=(0,0,0,0))
img=img.resize((W,H),Image.LANCZOS); img.save(r"C:\Users\akans\Downloads\chat_icon_preview.png")
px=img.load(); out=bytearray()
for y in range(H):
    for x in range(W):
        r,g,b,a=px[x,y]; c=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)
        out.append(c&0xFF); out.append((c>>8)&0xFF); out.append(a)
lines=["    "+",".join("0x%02x"%v for v in out[i:i+32])+"," for i in range(0,len(out),32)]
c=f'''#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

// ARIA Chat menu icon (speech bubble), 80x80 TRUE_COLOR_ALPHA
const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_aria_chat_png_data[] = {{
{chr(10).join(lines)}
}};
const lv_img_dsc_t ui_img_aria_chat_png = {{
    .header.always_zero = 0, .header.w = {W}, .header.h = {H},
    .data_size = sizeof(ui_img_aria_chat_png_data),
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA, .data = ui_img_aria_chat_png_data
}};
'''
open(r"C:\Users\akans\Downloads\watcher-firmware\examples\factory_firmware\main\view\ui\images\ui_img_aria_chat_png.c","w").write(c)
# preview on dark
bg=Image.new("RGBA",(80,80),(20,20,20,255)); bg.alpha_composite(img); bg.convert("RGB").resize((160,160)).save(r"C:\Users\akans\Downloads\chat_on_dark.png")
print("wrote chat icon", len(out), "bytes")
