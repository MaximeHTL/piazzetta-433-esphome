import re,sys
U=416.67
def bits(pr):
    s=''
    for a,b in pr:
        s+='1'*max(1,round(a/U)); s+='0'*min(round(b/U),30)
    return s
for line in open(sys.argv[1],encoding='utf-8',errors='replace'):
    m=re.search(r'RAW \((\d+)\): \+',line)
    if not m: continue
    pr=[(int(a),int(b)) for a,b,r in re.findall(r'\+(\d+)-(\d+)\((-?\d+)\)',line)]
    rs=max(int(r) for r in re.findall(r'\((-?\d+)\)',line))
    s=bits(pr)
    # split at sync gaps (>=12 zeros)
    parts=[p for p in re.split(r'0{12,}',s)]
    src='TEL' if rs>-50 else 'POELE'
    print(line[:10],src.ljust(5),int(m.group(1)),' | '.join(parts[1:])[:170])
