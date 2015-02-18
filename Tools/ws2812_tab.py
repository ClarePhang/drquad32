#!/usr/bin/env python
#
import sys

gamma = 2.5

print "// Generated by ws2812_tab.py, gamma %.3f" % gamma
print "//"
print "static const uint32_t ws2812_tab[256][2] = {"

for i in range(256):
    # apply gamma correction
    #
    ig = int( ((i+0.5) / 255.5) ** gamma * 255 + .5 )

    print "    { ",

    comment = "// %3d -> %3d: " % (i, ig)
    bitstring = ""

    for j in reversed( range(8) ):
        if  ig & (1 << j):
            comment += "1"
            bitstring += "00"	# don't clear bit
        else:
            comment += "0"
            bitstring += "ff"	# clear bit

    dw = []
    for j in range(2):
        # slice into 32 bit-words and convert to little endian
        #
        dw.append( "0x%s" % bitstring[j*8 : j*8 + 8] [::-1] )

    print ", ".join(dw),

    if (i != 255):
        print "}, ", comment
    else:
        print "}  ", comment

print "};"