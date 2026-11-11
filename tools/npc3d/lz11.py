import sys
def lz11(d):
    assert d[0]==0x11
    n=d[1]|d[2]<<8|d[3]<<16; p=4
    if n==0: n=int.from_bytes(d[4:8],'little'); p=8
    o=bytearray()
    while len(o)<n:
        f=d[p]; p+=1
        for b in range(8):
            if len(o)>=n: break
            if f&(0x80>>b):
                a=d[p]; t=a>>4
                if t==0:
                    l=(((a&15)<<4)|(d[p+1]>>4))+0x11; dist=(((d[p+1]&15)<<8)|d[p+2])+1; p+=3
                elif t==1:
                    l=(((a&15)<<12)|(d[p+1]<<4)|(d[p+2]>>4))+0x111; dist=(((d[p+2]&15)<<8)|d[p+3])+1; p+=4
                else:
                    l=t+1; dist=(((a&15)<<8)|d[p+1])+1; p+=2
                for i in range(l): o.append(o[-dist])
            else: o.append(d[p]); p+=1
    return bytes(o)
if __name__=='__main__':
    import os
    for f in sys.argv[2:]:
        out=os.path.join(sys.argv[1],os.path.basename(f)[:-3])
        open(out,'wb').write(lz11(open(f,'rb').read()))
