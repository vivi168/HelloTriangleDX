import parse
import struct
import os
import re
import argparse
from PIL import Image

class RawImage:
    def __init__(self, img, alpha = None):
        self.width, self.height = img.size
        self.alpha = alpha
        self.img = img.convert('RGBA')

    def pack_header(self):
        return struct.pack('<HH', self.width, self.height)

    def pack_pixel(self, x, y):
        pixel = self.img.getpixel((x, y))

        if (pixel[0], pixel[1], pixel[2]) == self.alpha:
            alpha = 0
        else:
            alpha = pixel[3]

        # print(pixel[0], pixel[1], pixel[2], 'ALPHA:', alpha)

        return struct.pack('<BBBB', pixel[0], pixel[1], pixel[2], alpha)

    def pack_pixels(self):
        pixel_data = bytearray()

        for y in range(self.height):
            for x in range(self.width):
                pixel_data += self.pack_pixel(x, y)\

        return pixel_data

    def pack(self):
        return self.pack_header() + self.pack_pixels()

def is_valid_hex_color(hex_color):
    if hex_color == None:
        return False

    hex_color_pattern = re.compile(r'^(#)?([A-Fa-f0-9]{6})$')
    return bool(hex_color_pattern.match(hex_color))

def hex_to_rgb(hex_color):
    if not is_valid_hex_color(hex_color):
        return None

    hex_color = hex_color.lstrip('#')
    return tuple(int(hex_color[i:i+2], 16) for i in (0, 2, 4))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Convert image to raw pixel data')
    parser.add_argument('infile', help='Input file path')
    parser.add_argument('outfile', help='Output file path (optional)', nargs='?')
    parser.add_argument('--alpha', help='transparent color (optional) (#aabbcc)', nargs='?')

    args = parser.parse_args()

    if args.outfile == None:
        outfile = os.path.splitext(os.path.basename(args.infile))[0] + ".raw"
    else:
        outfile = args.outfile

    img = Image.open(args.infile)
    m = RawImage(img, hex_to_rgb(args.alpha))

    with open(outfile, 'wb') as f:
        f.write(m.pack())





