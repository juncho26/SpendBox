# Converts the PBM screens written by `make screens` into enlarged PNGs.  usage: topng.py <in_dir> <out_dir>
import glob, os, sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, "screen-*.pbm"))):
    im = Image.open(p).convert("L").resize((600, 600), Image.NEAREST)
    im.save(os.path.join(dst, os.path.basename(p).replace(".pbm", ".png")))
