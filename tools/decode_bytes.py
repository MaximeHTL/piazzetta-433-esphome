# Hypothese : UART inverse 2400 bauds, 8 bits LSB d'abord, parite paire.
# Repos = 0, bit de start = 1, puis 8 bits de donnees, 1 bit de parite, stop = 0.
import json,sys
def octets(sig):
    sig=sig+'0'*12
    out=[];i=0;err=0
    while i < len(sig)-10:
        if sig[i]=='1':
            d=sig[i+1:i+9]; p=sig[i+9]; st=sig[i+10] if i+10<len(sig) else '0'
            v=int(d[::-1],2)
            ok = (d.count('1')+int(p))%2==0 and st=='0'
            out.append(('%02X'%v)+('' if ok else '!'))
            if not ok: err+=1
            i+=11
        else: i+=1
    return out,err
if __name__=='__main__':
    p=sys.argv[1]; t0=sys.argv[2] if len(sys.argv)>2 else '00'; t1=sys.argv[3] if len(sys.argv)>3 else '99'
    for l in open(p,encoding='utf-8'):
        try: d=json.loads(l)
        except: continue
        if not (t0<=d.get('h','')<=t1): continue
        if d['k']=='trame' and d['src']!='inconnu':
            o,e=octets(d['sig'])
            print(d['h'],d['src'][:5].ljust(5),d['type'][:12].ljust(12),' '.join(o))
        elif d['k'] in ('repere',): print(d['h'],'>>>',d.get('texte',''))
