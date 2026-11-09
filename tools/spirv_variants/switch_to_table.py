# Usage: switch_to_table.py in.spvasm out.spvasm
# Turns a lookup OpSwitch (40+ cases, every case branches straight to a merge OpPhi) into a load from a
# Private constant array indexed by the selector (128 entries). Unlike the other two scripts the result keeps
# the original meaning; this is how the flat AMD format decoder was prototyped.
import re,sys
src,dst=sys.argv[1],sys.argv[2]
L=open(src).read().split('\n')
def find_def(name):
    for l in L:
        m=re.match(r'\s*'+re.escape(name)+r' = (.*)',l)
        if m: return m.group(1)
uint=next(re.match(r'\s*(%\S+) = OpTypeInt 32 0',l).group(1) for l in L if re.search(r'= OpTypeInt 32 0\b',l))
label_line={}
for i,l in enumerate(L):
    m=re.match(r'\s*(%\S+) = OpLabel',l)
    if m: label_line[m.group(1)]=i
new_globals=[];edits={};count=0
for i,l in enumerate(L):
    s=l.strip()
    if not s.startswith('OpSwitch '): continue
    p=s.split(); sel=p[1]; default=p[2]; pairs=list(zip(p[3::2],p[4::2]))
    if len(pairs)<40: continue
    merge=L[i-1].split()[1]  # OpSelectionMerge %m None
    ml=label_line[merge]; phi=L[ml+1].strip()
    m=re.match(r'(%\S+) = OpPhi (%\S+) (.*)',phi)
    res,ty,rest=m.groups(); r=rest.split(); val={r[k+1]:r[k] for k in range(0,len(r),2)}
    # all case labels must just branch to merge
    ok=all(L[label_line[lab]+1].strip()=='OpBranch '+merge for _,lab in pairs+[(None,default)])
    if not ok: print('skip',sel); continue
    case={int(c):lab for c,lab in pairs}
    entries=[val[case.get(k,default)] for k in range(128)]
    t=f'%kt{count}'
    new_globals+= [f'%kt_u128 = OpConstant {uint} 128' if count==0 else None,
                   f'{t}_arrty = OpTypeArray {ty} %kt_u128',
                   f'{t}_c = OpConstantComposite {t}_arrty '+' '.join(entries),
                   f'{t}_pty = OpTypePointer Private {t}_arrty',
                   f'{t}_pe = OpTypePointer Private {ty}',
                   f'{t}_v = OpVariable {t}_pty Private {t}_c']
    blockstart=i-1
    edits[i-1]=[f'{t}_ptr = OpAccessChain {t}_pe {t}_v {sel}', f'{t}_val = OpLoad {ty} {t}_ptr']
    edits[i]=['OpBranch '+merge]
    edits[ml+1]=[f'{res} = OpCopyObject {ty} {t}_val']
    count+=1
first_fn=next(i for i,l in enumerate(L) if re.search(r'= OpFunction ',l))
out=[]
for i,l in enumerate(L):
    if i==first_fn: out+= [g for g in new_globals if g]
    if i in edits: out+=['               '+e for e in edits[i]]; continue
    out.append(l)
open(dst,'w').write('\n'.join(out)); print('tables',count)
