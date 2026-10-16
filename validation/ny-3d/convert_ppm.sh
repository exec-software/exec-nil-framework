#!/bin/bash
# Simple PPM to PNG conversion

# Read width, height, maxval from PPM
WIDTH=$(grep -v '^#' procedural_output.ppm | awk 'NR==2 {print}' | awk '{print $1}')
HEIGHT=$(grep -v '^#' procedural_output.ppm | awk 'NR==2 {print}' | awk '{print $2}')
MAXVAL=$(grep -v '^#' procedural_output.ppm | awk 'NR==3 {print}')

# Convert using Python - just read the file and create image
echo "Converting ${WIDTH}x${HEIGHT} PPM to PNG..."

# Use head to get just the pixel data part and convert
python3 << 'PYEOF'
from PIL import Image
import sys

# Read PPM file
with open('procedural_output.ppm', 'r') as f:
    lines = f.readlines()

# Parse width from line 2 (index 1), height from line 3 (index 2)
w = int(lines[1].strip())
h = int(lines[2].strip())

# Collect pixel values from lines 7 onwards
pixels = []
for line in lines[6:]:
    if line.startswith('#'):
        continue
    parts = line.strip().split()
    for part in parts:
        if part:
            pixels.append(int(part))

# Create image
img = Image.new('RGB', (w, h))
img.putdata([(pixels[i], pixels[i+1], pixels[i+2]) for i in range(0, min(len(pixels), w*h*3), w*h*3)])
img.save('procedural_output.png')
print(f"Saved procedural_output.png ({w}x{h})")
PYEOF
