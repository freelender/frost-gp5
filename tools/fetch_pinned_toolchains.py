"""Download OpenGP5's pinned Linux x86_64 compiler archives; verify SHA-256."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import os
from pathlib import Path
import ssl
import subprocess
import urllib.error
import urllib.request

ARCHIVES = (
    ('andes-v3s.txz',
     'https://github.com/andestech/Andes-Development-Kit/releases/download/ast-v3_2_5-release-linux/nds32le-elf-mculib-v3s.txz',
     'ff10eb1fa9dce66f8aeca5267df01ce0353748f2f2364a550eceb10c7953abc3'),
    ('arm-14.2.rel1-linux-x86_64.tar.xz',
     'https://developer.arm.com/-/media/Files/downloads/gnu/14.2.rel1/binrel/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi.tar.xz',
     '62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823'),
)

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''): h.update(block)
    return h.hexdigest()

def fetch(entry, folder):
    name,url,expected=entry
    path=folder/name
    if not path.exists() or digest(path)!=expected:
        partial=path.with_suffix(path.suffix+'.partial')
        if not partial.exists() or digest(partial)!=expected:
            try:
                with urllib.request.urlopen(url,timeout=60) as response, partial.open('wb') as output:
                    for block in iter(lambda:response.read(1024*1024),b''): output.write(block)
            except urllib.error.URLError as error:
                if os.name!='nt' or not isinstance(error.reason,ssl.SSLCertVerificationError): raise
                # Windows curl uses the system trust store. Do not disable TLS
                # checks; the exact published archive hash is checked below too.
                subprocess.run(['curl.exe','--fail','--location','--retry','2',
                                '--output',str(partial),url],check=True)
        if digest(partial)!=expected: raise RuntimeError(f'{name}: SHA-256 mismatch; partial retained for inspection')
        partial.replace(path)
    print(f'{name}: {path.stat().st_size} bytes, SHA-256 verified',flush=True)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'build/pinned-toolchains')
    a=ap.parse_args(); a.output.mkdir(parents=True,exist_ok=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures=[pool.submit(fetch,entry,a.output) for entry in ARCHIVES]
        for future in futures: future.result()
