# Lion of Judah artwork

- `lion_source.webp` — pixel-art crowned lion with the red-gold-green flag, **supplied by the user** with the v0.3.0 task (1254 × 1254 RGB, white background). Rights remain with the supplier.
- Compiled by `tools/lion_assets.py` (Pillow): bounding box of non-white pixels, per-cell dominant colour of the ~12 px art grid (no smoothing), nearest-colour mapping into a 15-colour palette sampled from the original + transparency, 100 × 88 pixels, 4 bpp, run-length coded (1790 bytes) into `src/ui/rr_lion_atlas.h`. `build/lion_preview.png` shows the result at 8×.
- Animation is done at render time by moving sprite zones (head/crown, flag cloth, tail, body) and small eye overlays; no extra frames are stored.
