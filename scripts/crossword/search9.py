# Local search for 9x9 (or n x n) American templates: penalty = short runs + disconnection; keep valid distinct ones.
# Usage: python3 search9.py <n> <min blocks> <max blocks> [max run]   (prints valid symmetric templates to paste into templates.json)
import random, sys, templates as t
n=int(sys.argv[1]); bmin=int(sys.argv[2]); bmax=int(sys.argv[3]); maxrun=int(sys.argv[4]) if len(sys.argv)>4 else n
pairs=[]
for r in range(n):
    for c in range(n):
        q=(n-1-r,n-1-c)
        if (r,c)<=q: pairs.append(((r,c),q))
def rows_of(bl): return [''.join('#' if (r,c) in bl else '.' for c in range(n)) for r in range(n)]
def pen(rows):
    rl=t.runs(rows); p=sum(3-l for l in rl if l<3)*2
    if t.problems(rows) and not p: p+=1
    nb=sum(x.count('#') for x in rows)
    if nb<bmin: p+=bmin-nb
    if nb>bmax: p+=nb-bmax
    p+=sum(1 for l in rl if l>maxrun)
    return p
rnd=random.Random(7); found={}
for restart in range(3000):
    bl=set()
    for a,b in rnd.sample(pairs, rnd.randint(bmin//2,bmax//2)): bl.add(a); bl.add(b)
    cur=pen(rows_of(bl))
    for step in range(400):
        if cur==0: break
        a,b=rnd.choice(pairs); nb=set(bl)
        if a in nb: nb.discard(a); nb.discard(b)
        else: nb.add(a); nb.add(b)
        p=pen(rows_of(nb))
        if p<=cur or rnd.random()<0.05: bl,cur=nb,p
    rows=rows_of(bl)
    if cur==0 and not t.problems(rows) and not t.has_2x2_block(rows):
        k=t.canon(rows)
        if k not in found: found[k]=list(k)
out=[]
for r in found.values():
    s=t.stats(r)
    full=[i for i,row in enumerate(r) if '#' not in row]; cols=[''.join(x) for x in zip(*r)]; fc=[i for i,c in enumerate(cols) if '#' not in c]
    stacked=any(b-a==1 for a,b in zip(full,full[1:])) or any(b-a==1 for a,b in zip(fc,fc[1:]))
    out.append((round(s['threes']/s['words'],2),stacked,-s['avg'],r,s))
out.sort(key=lambda x:(x[1],x[0],x[2]))
print(len(out))
for th,st,av,r,s in out[:60]: print('|'.join(r),s['blocks'],s['words'],s['threes'],s['avg'],s['max'],'STACK' if st else '')
