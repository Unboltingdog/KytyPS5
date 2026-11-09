# Usage: neutralize_switches.py in.spvasm out.spvasm <i,j,...>
# Experiment: pins the selectors of the listed OpSwitch instructions (0-based, in file order) to 0. Used to
# find which switches make a shader slow to compile. Output is for timing only.
import re,sys
src,dst,keep=sys.argv[1],sys.argv[2],sys.argv[3]  # keep = comma list of switch indices to neutralize
idx=set(int(x) for x in keep.split(','))
L=open(src).read().split('\n')
booltype=next(re.match(r'\s*(%\S+) = OpTypeBool',l).group(1) for l in L if re.search(r'= OpTypeBool\b',l))
uint=next(re.match(r'\s*(%\S+) = OpTypeInt 32 0',l).group(1) for l in L if re.search(r'= OpTypeInt 32 0\b',l))
out=[];k=0;ins=False
for l in L:
    s=l.strip()
    if not ins and re.search(r'= OpTypeBool\b',l):
        out+= [l,'%kc_zero = OpConstant '+uint+' 0']; ins=True; continue
    if s.startswith('OpSwitch '):
        if k in idx:
            p=s.split(); p[1]='%kc_zero'; l='               '+' '.join(p)
        k+=1
    out.append(l)
open(dst,'w').write('\n'.join(out))
