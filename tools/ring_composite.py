"""ringcomp.py IN.ppm CLEAR_DEG OUT.png [L R U D] - what the headset shows: the dumped eyes with the comfort ring quad
composited as the runtime would (head-locked quad 5 m ahead, centred on the view axis, alpha = smoothstep(R0, R0*K, r)
in the quad's half-size units; the half-size from the clear angle: tan(clear) / R0). Both eyes the same quad."""
import sys, math
import numpy as np
from PIL import Image
R0, K = 0.16, 1.75
src, clear, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
fov = [float(x) for x in sys.argv[4:8]] if len(sys.argv) >= 8 else [-0.7894, 0.7894, 0.8264, -0.8264]
im = np.asarray(Image.open(src).convert('RGB')).astype(np.float32)
H, W2 = im.shape[:2]
W = W2 // 2
half = math.tan(math.radians(clear)) / R0
l, r, u, d = [math.tan(a) for a in fov]
xs = l + (np.arange(W) + 0.5) / W * (r - l)
ys = u - (np.arange(H) + 0.5) / H * (u - d)
tx, ty = np.meshgrid(xs, ys)
rr = np.sqrt(tx * tx + ty * ty) / half
t = np.clip((rr - R0) / (R0 * K - R0), 0, 1)
alpha = t * t * (3 - 2 * t)
alpha[(np.abs(tx) > half) | (np.abs(ty) > half)] = 1.0  # outside the quad: nothing (here: never, the quad covers)
a = alpha[..., None]
res = im.copy()
for e in range(2):
    res[:, e * W:(e + 1) * W] = im[:, e * W:(e + 1) * W] * (1 - a)
Image.fromarray(np.clip(res, 0, 255).astype(np.uint8)).resize((1600, int(1600 * H / W2))).save(out)
print("composited", out, "clear", clear, "deg; darkened share of the view %.1f %%" % (100 * float(alpha.mean())))
