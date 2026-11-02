#!/usr/bin/env python3
"""Convert the actual C++ framebuffer renders; captions are outside the LCD area."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
import sys
OUT = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT / 'build/dashboard'
FONT = ROOT / 'Arduino/resources/fonts/NanumGothic-Regular.ttf'
for ppm in OUT.glob('*.ppm'):
    with Image.open(ppm) as im:
        im.save(ppm.with_suffix('.png'))
    ppm.unlink()

def sheet(filename, items, columns, orientation, scale=2):
    w, h = (320, 240) if orientation == 'landscape' else (240, 320)
    pad, caption, top = 24, 48, 68
    tile_w, tile_h = w*scale, h*scale
    rows = (len(items)+columns-1)//columns
    im = Image.new('RGB', (pad+(tile_w+pad)*columns, top+(tile_h+caption+pad)*rows), '#20242b')
    draw = ImageDraw.Draw(im)
    font = ImageFont.truetype(str(FONT), 23)
    draw.text((pad, 18), 'SpaceSmart · 실제 픽셀 렌더링 / 표시값은 예시', font=font, fill='#c3ccd6')
    for i,(name,label) in enumerate(items):
        x,y=pad+(i%columns)*(tile_w+pad), top+(i//columns)*(tile_h+caption+pad)
        draw.text((x,y),label,font=font,fill='#e9edf3')
        with Image.open(OUT/f'{orientation}-{name}.png') as screen:
            im.paste(screen.resize((tile_w,tile_h),Image.Resampling.NEAREST),(x,y+caption))
    im.save(OUT/filename)

themes=[('mint-environment','01 소프트 민트'),('blue-environment','02 차콜 블루'),('amber-environment','03 웜 앰버'),('violet-environment','04 미드나이트 바이올렛')]
sheet('four-themes.png', themes + [(name.replace('environment','graph'),label+' · 그래프') for name,label in themes], 4, 'portrait')
pages=[('environment','실내 환경'),('graph','환경 그래프'),('computer','컴퓨터'),('custom','커스텀'),('agents','AI 작업'),('focus','집중 타이머'),('clock','시계'),('diagnostics','기기 진단'),('settings','설정'),('light','조명'),('ledtest','LED 시험'),('lightsetup','조명 설정')]
for orientation in ('landscape','portrait'):
    sheet(f'{orientation}-all-modes.png',[(f'mint-{p}',label) for p,label in pages],4,orientation)
    sheet(f'{orientation}-missing-data.png',[(f'missing-{p}',label) for p,label in pages],4,orientation)
print('Saved PNG previews and five contact sheets to',OUT)
