# Usage: neutralize_branches.py in.spvasm out.spvasm noswitch|nobranch|both [fraction]
# Experiment: pins OpSwitch selectors to 0 and/or OpBranchConditional conditions to true, so the dead arms
# can be removed with spirv-opt. Shows how much of a compile time comes from control flow. The output computes
# wrong values; it is only for timing the driver compile with kyty_spvbench.
import re,sys,random
src,dst,mode=sys.argv[1],sys.argv[2],sys.argv[3]
frac=float(sys.argv[4]) if len(sys.argv)>4 else 1.0
L=open(src).read().split('\n')
booltype=next(re.match(r'\s*(%\S+) = OpTypeBool',l).group(1) for l in L if re.search(r'= OpTypeBool\b',l))
# selector types for switches: assume uint
uint=next(re.match(r'\s*(%\S+) = OpTypeInt 32 0',l).group(1) for l in L if re.search(r'= OpTypeInt 32 0\b',l))
rnd=random.Random(7); out=[]; n=0; inserted=False
for l in L:
    s=l.strip()
    if not inserted and re.search(r'= OpTypeBool\b',l):
        out.append(l); out.append('%kc_true = OpConstantTrue '+booltype); out.append('%kc_zero = OpConstant '+uint+' 0'); inserted=True; continue
    if mode in('noswitch','both') and s.startswith('OpSwitch '):
        p=s.split(); p[1]='%kc_zero'; l='               '+' '.join(p); n+=1
    elif mode in('nobranch','both') and s.startswith('OpBranchConditional ') and rnd.random()<frac:
        p=s.split(); p[1]='%kc_true'; l='               '+' '.join(p); n+=1
    out.append(l)
open(dst,'w').write('\n'.join(out)); print(mode,frac,'changed',n)
