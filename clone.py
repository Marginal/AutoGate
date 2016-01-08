#!/usr/bin/python
#
# Given objects at 6m, create versions at different heights
#

from os import listdir

refheight=6
cut=3

for thing in ['Safedock2S', 'SafedockT2', 'Safegate']:
    for static in ['','Static-']:
        for pole in ['','-pole']:
            for height in [3, 3.5, 4, 4.5, 5, 5.5, 6.5, 7, 7.5, 8]:
                infilename = "DGSs-%s/%s%s-%sm%s.obj" % (thing, static, thing, refheight, pole)
                infile=file(infilename, 'rt')
                outfilename = "DGSs-%s/%s%s-%sm%s.obj" % (thing, static, thing, height, pole)
                outfile=file(outfilename, 'wt')
                print outfilename

                for line in infile:
                    tokens=line.split()
                    if not tokens:
                        outfile.write('\n')
                        continue
                    if tokens[0]=='VT' and float(tokens[2])>cut:
                        outfile.write("VT\t%9.4f %9.4f %9.4f\t%6.3f %6.3f %6.3f\t%-6s %-6s\n" % (
                            float(tokens[1]),
                            float(tokens[2])-refheight+height,
                            float(tokens[3]),
                            float(tokens[4]),float(tokens[5]),float(tokens[6]),
                            float(tokens[7]),float(tokens[8])))
                    elif tokens[0]=='ANIM_trans' and float(tokens[2])>cut:
                        outfile.write("%sANIM_trans\t%9.4f %9.4f %9.4f\t%9.4f %9.4f %9.4f\t%s" % (
                            line.split('A')[0],
                            float(tokens[1]),
                            float(tokens[2])-refheight+height,
                            float(tokens[3]),
                            float(tokens[4]),
                            float(tokens[5])-refheight+height,
                            float(tokens[6]),
                            line.split(None,7)[-1]))
                    else:
                        outfile.write(line)

            outfile.close()
            infile.close()
