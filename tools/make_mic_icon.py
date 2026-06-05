from PIL import Image, ImageDraw
W=H=80
img = Image.new("RGBA",(W*4,H*4),(0,0,0,0))   # supersample 4x for smooth edges
d = ImageDraw.Draw(img)
S=4
cx = W*S//2
white=(255,255,255,255)
# mic capsule (rounded rect)
cap_w, cap_h = 26*S, 42*S
cap_x0 = cx - cap_w//2
cap_y0 = 12*S
d.rounded_rectangle([cap_x0, cap_y0, cap_x0+cap_w, cap_y0+cap_h], radius=cap_w//2, fill=white)
# pickup bracket: U arc around lower capsule
br_w = 46*S
br_x0 = cx - br_w//2
br_y0 = 26*S
br_y1 = 26*S + 40*S
d.arc([br_x0, br_y0, br_x0+br_w, br_y1], start=20, end=160, fill=white, width=5*S)
# stand
d.rectangle([cx-2*S, 62*S, cx+2*S, 70*S], fill=white)
# base
d.rounded_rectangle([cx-15*S, 70*S, cx+15*S, 74*S], radius=2*S, fill=white)
img = img.resize((W,H), Image.LANCZOS)
img.save(r"C:\Users\akans\Downloads\mic_icon_preview.png")
px = img.load()
out = bytearray()
for y in range(H):
    for x in range(W):
        r,g,b,a = px[x,y]
        c=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)
        out.append(c&0xFF); out.append((c>>8)&0xFF); out.append(a)
lines=["    "+",".join("0x%02x"%v for v in out[i:i+32])+"," for i in range(0,len(out),32)]
body="\n".join(lines)
c=f'''#include "../ui.h"

#ifndef LV_ATTRIBUTE_MEM_ALIGN
    #define LV_ATTRIBUTE_MEM_ALIGN
#endif

// ARIA Recording menu icon (microphone), 80x80 TRUE_COLOR_ALPHA
const LV_ATTRIBUTE_MEM_ALIGN uint8_t ui_img_aria_rec_png_data[] = {{
{body}
}};
const lv_img_dsc_t ui_img_aria_rec_png = {{
    .header.always_zero = 0,
    .header.w = {W},
    .header.h = {H},
    .data_size = sizeof(ui_img_aria_rec_png_data),
    .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
    .data = ui_img_aria_rec_png_data
}};
'''
open(r"C:\Users\akans\Downloads\watcher-firmware\examples\factory_firmware\main\view\ui\images\ui_img_aria_rec_png.c","w").write(c)
print("wrote icon:", len(out), "bytes")
