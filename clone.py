#!/usr/bin/python
#
# Given v7 and v8 objects at 6m, create versions at different heights
#

refheight=6
cut=3

for thing in ['Safedock2S', 'Safegate']:
    for pole in ['','-pole']:
        for height in [3, 3.5, 4, 4.5, 5, 5.5, 6.5, 7, 7.5, 8]:
            infilename=("DGSs-%s/%s-%sm%s.obj") % (thing, thing, refheight, pole)
            infile=file(infilename, 'rt')
            outfilename=("DGSs-%s/%s-%sm%s.obj") % (thing, thing, height, pole)
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


refdist=20

for thing in ['Safedock2S', 'Safegate']:
    for dist in [16, 18, 20, 22, 24, 26, 28, 30]:
        for height in [3, 3.5, 4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8]:
            if height==refheight and dist==refdist: continue
            infilename=("Standalone-%s/SA-%sm-%s-%sm.obj") % (thing, refdist, thing, refheight)
            infile=file(infilename, 'rt')
            infilename=("Standalone-%s/SA-%sm-%s-%sm.obj") % (thing, dist, thing, height)
            outfile=file(outfilename, 'wt')
            print outfilename

            for line in infile:
                tokens=line.split()
                if not tokens:
                    outfile.write('\n')
                    continue
                if tokens[0]=='VT':
                    if float(tokens[2])>cut:
                        newheight=float(tokens[2])-refheight+height
                    else:
                        newheight=float(tokens[2])
                    if float(tokens[3])<-cut:
                        newdist=float(tokens[3])+refdist-dist
                    else:
                        newdist=float(tokens[3])
                    outfile.write("VT\t%9.4f %9.4f %9.4f\t%6.3f %6.3f %6.3f\t%-6s %-6s\n" % (
                        float(tokens[1]),
                        newheight,
                        newdist,
                        float(tokens[4]),float(tokens[5]),float(tokens[6]),
                        float(tokens[7]),float(tokens[8])))
                elif tokens[0]=='ANIM_trans':
                    if float(tokens[2])>cut:
                        newheight1=float(tokens[2])-refheight+height
                        newheight2=float(tokens[5])-refheight+height
                    else:
                        newheight1=float(tokens[2])
                        newheight2=float(tokens[5])
                    if float(tokens[3])<-cut:
                        newdist1=float(tokens[3])+refdist-dist
                        newdist2=float(tokens[6])+refdist-dist
                    else:
                        newdist1=float(tokens[3])
                        newdist2=float(tokens[6])
                    outfile.write("%sANIM_trans\t%9.4f %9.4f %9.4f\t%9.4f %9.4f %9.4f\t%s" % (
                        line.split('A')[0],
                        float(tokens[1]),
                        newheight1,
                        newdist1,
                        float(tokens[4]),
                        newheight2,
                        newdist2,
                        line.split(None,7)[-1]))
                else:
                    outfile.write(line)
                    
            outfile.close()
            infile.close()
