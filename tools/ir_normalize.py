"""Offline cabinet gain calibration: response peak near unity, L1 <= 7.999."""
import cmath
import math

def response_gain(h):
    a=[complex(v) for v in h]+[0j]*(1024-len(h))
    j=0
    for i in range(1,1024):
        bit=512
        while j&bit:j^=bit;bit>>=1
        j^=bit
        if i<j:a[i],a[j]=a[j],a[i]
    n=2
    while n<=1024:
        root=cmath.exp(-2j*math.pi/n)
        for start in range(0,1024,n):
            w=1+0j
            for k in range(n//2):
                u=a[start+k];v=w*a[start+k+n//2]
                a[start+k]=u+v;a[start+k+n//2]=u-v;w*=root
        n*=2
    peak=max(map(abs,a[:513]));l1=sum(map(abs,h))
    if not peak>0 or not math.isfinite(peak):raise ValueError('IR response is invalid')
    return min(1/peak,7.999/l1)
