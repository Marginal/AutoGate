#!/usr/bin/python
#
# Given v7 and v8 objects at 6m, create versions at different heights
#

refheight=6
cut=3

for obj in ['Safedock2S-%sm.obj',
            'Safedock2S-%sm-pole.obj',
            'Safegate-%sm.obj',
            'Safegate-%sm-pole.obj']:
    for height in [3, 3.5, 4, 4.5, 5, 5.5, 6.5, 7, 7.5, 8]:
        infilename=("DGSs/"+obj) % refheight
        infile=file(infilename, 'rt')
        outfilename=("DGSs/"+obj)% height
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
                #print line
                assert(line[71]=='\t')
                outfile.write("\tANIM_trans\t%9.4f %9.4f %9.4f\t%9.4f %9.4f %9.4f%s" % (
                    float(tokens[1]),
                    float(tokens[2])-refheight+height,
                    float(tokens[3]),
                    float(tokens[4]),
                    float(tokens[5])-refheight+height,
                    float(tokens[6]),
                    line[71:]))
            else:
                outfile.write(line)

        outfile.close()
        infile.close()


refdist=20

for obj in ['SA-%sm-Safedock2S-%sm.obj',
            'SA-%sm-Safegate-%sm.obj']:
    for dist in [16, 18, 20, 22, 24]:
        for height in [4, 4.5, 5, 5.5, 6, 6.5, 7, 7.5, 8]:
            if height==refheight and dist==refdist: continue
            infilename=("Standalone_DGSs/"+obj) % (refdist, refheight)
            infile=file(infilename, 'rt')
            outfilename=("Standalone_DGSs/"+obj)% (dist, height)
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
                elif tokens[0]=='ANIM_trans' and line[71]=='\t':
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
                    outfile.write("\tANIM_trans\t%9.4f %9.4f %9.4f\t%9.4f %9.4f %9.4f%s" % (
                        float(tokens[1]),
                        newheight1,
                        newdist1,
                        float(tokens[4]),
                        newheight2,
                        newdist2,
                        line[71:]))
                else:
                    outfile.write(line)
                    
            outfile.close()
            infile.close()
