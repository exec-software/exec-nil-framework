#!/usr/bin/env python3
from PIL import Image
import re

# Read PPM file
with open('procedural_output.ppm', 'r') as f:
    lines = [l.strip() for l in f.readlines() if l.strip()]

# Parse PPM structure:
# Line 0: P3
# Line 1: comment
# Line 2: comment
# Line 3: width
# Line 4: height
# Line 5: max value
# Then pixel RGB values

# Find width, height, max value lines
width = int(lines[3])
height = int(lines[4])
max_val = int(lines[5])

# Collect all pixel values
pixels = []
for line in lines[6:]:
    if line and not line.startswith('#') and not line.startswith('P3'):
        # Split line into values
        parts = line.split()
        for part in parts:
            if part.isdigit():
                pixels.append(int(part))

# Create image
img = Image.new('RGB', (width, height))
# Take first width*height*3 pixel values (RGB triplets)
rgb_data = []
for i in range(0, min(len(pixels), width*height*3), 3):
    rgb_data.append((pixels[i], pixels[i+1], pixels[i+2]))

img.putdata(rgb_data[:width*height])
img.save('procedural_output.png')
print(f"Saved procedural_output.png ({width}x{height})")
