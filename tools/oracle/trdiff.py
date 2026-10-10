# Compare a MAME V60 trace (tools/oracle/trace.lua) with s32run --trace:
# python3 trdiff.py DIR  (DIR/mame.tr, DIR/ours.tr). MAME's "loops for N"
# lines are skipped by advancing ours to the next MAME PC.
import sys,re
S=sys.argv[1]
m=[]
for l in open(S+'/mame.tr'):
    x=re.match(r'([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]{8}) ([0-9A-F]+):',l)
    if x: m.append((x.group(1),x.group(2),x.group(3),x.group(4),int(x.group(5),16)&0xffffffff, l.strip()))
    elif 'loops' in l: m.append(('loop',l.strip()))
o=[l.split() for l in open(S+'/ours.tr')]
i=j=0; n=0
while i<len(m) and j<len(o):
    if m[i][0]=='loop':
        i+=1
        if i>=len(m): break
        target=m[i][4]
        while j<len(o) and int(o[j][4],16)!=target: j+=1
        continue
    mp=m[i][4]; op=int(o[j][4],16)
    if mp!=op or m[i][:4]!=tuple(o[j][:4]):
        print('DIVERGE at mame line',i,'ours',j)
        for k in range(max(0,i-8),i+3): print('M',m[k][-1] if m[k][0]!='loop' else m[k][1])
        for k in range(max(0,j-8),j+3): print('O',' '.join(o[k]))
        break
    i+=1;j+=1;n+=1
print('matched',n,'of mame',len(m))
