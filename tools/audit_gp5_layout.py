"""Offline ELF32 audit of the GP-5 boot-sensitive region. Never accesses hardware.

Addresses, sizes and function order through fw_process must match. Executable
bytes from gain_sample through fw_process must match too; ARM $d mapping symbols distinguish literal pools, whose
relocations are reported separately. This is a layout gate, not a boot test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import re

class Elf:
    def __init__(self,path):
        self.data=Path(path).read_bytes()
        h=struct.unpack_from('<16sHHIIIIIHHHHHH',self.data)
        if h[0][:6]!=b'\x7fELF\x01\x01' or h[2]!=40:raise ValueError('expected little-endian ARM ELF32')
        self.sections=[struct.unpack_from('<10I',self.data,h[6]+i*h[11]) for i in range(h[12])]
        self.symbols=[]
        for section in self.sections:
            if section[1]!=2:continue
            strings=self.section_data(self.sections[section[6]])
            for offset in range(section[4],section[4]+section[5],section[9]):
                name,value,size,info,other,index=struct.unpack_from('<IIIBBH',self.data,offset)
                end=strings.find(b'\0',name)
                self.symbols.append(dict(name=strings[name:end].decode(),address=value & ~1 if info&15==2 else value,size=size,kind=info&15,section=index))
        self.functions=sorted((s for s in self.symbols if s['kind']==2 and s['size']),key=lambda s:(s['address'],s['name']))
        self.mappings=sorted((s for s in self.symbols if s['name'].split('.')[0] in ('$d','$t','$a')),key=lambda s:s['address'])
    def section_data(self,s):return self.data[s[4]:s[4]+s[5]]
    def memory(self,addr,n):
        for s in self.sections:
            if s[1]!=8 and s[3]<=addr and addr+n<=s[3]+s[5]:
                start=s[4]+addr-s[3];return self.data[start:start+n]
        raise ValueError('unmapped address '+hex(addr))
    def is_literal(self,addr,section):
        mappings=[s for s in self.mappings if s['section']==section and s['address']<=addr]
        return bool(mappings) and mappings[-1]['name'].split('.')[0]=='$d'

def audit(baseline,candidate):
    old,new=Elf(baseline),Elf(candidate)
    stop=next(s for s in old.functions if s['name']=='fw_process')
    end=stop['address']+stop['size']
    begin=next(s['address'] for s in old.functions if s['name']=='gain_sample')
    a=[s for s in old.functions if s['address']<end]
    b=[s for s in new.functions if s['address']<end]
    layout=lambda rows:[(s['name'],s['address'],s['size']) for s in rows]
    same=layout(a)==layout(b);literal_changes=[];code_changes=[];unverified_literals=[];renamed_literal_objects=[]
    newby={s['name']:s for s in new.symbols if s['name'] and not s['name'].startswith('$')}
    for s in a:
        if s['address']<begin:continue
        addr,n=s['address'],s['size']
        x,y=old.memory(addr,n),new.memory(addr,n)
        changed=[i for i in range(n) if x[i]!=y[i]]
        literals=[i for i in changed if old.is_literal(addr+i,s['section']) and new.is_literal(addr+i,s['section'])]
        if len(literals)!=len(changed):code_changes.append(s['name'])
        if literals:
            literal_changes.append(dict(function=s['name'],changed_bytes=len(literals)))
            for word in sorted({(addr+i)&~3 for i in literals}):
                before=struct.unpack('<I',old.memory(word,4))[0]
                after=struct.unpack('<I',new.memory(word,4))[0]
                targets=[t for t in old.symbols if t['size'] and t['address']<=before<t['address']+t['size'] and t['name'] in newby and after==newby[t['name']]['address']+before-t['address']]
                if not targets:
                    # GCC renumbers local constants when a new local table is
                    # introduced (e.g. CSWTCH.239 -> CSWTCH.242). Accept only
                    # the same stem/size, identical full object bytes and offset.
                    stem=lambda name:re.sub(r"\.\d+$","",name)
                    for previous in old.symbols:
                        if not previous['size'] or not previous['address']<=before<previous['address']+previous['size']:continue
                        for current in new.symbols:
                            if current['name']==previous['name'] or stem(current['name'])!=stem(previous['name']) or current['size']!=previous['size']:continue
                            if after!=current['address']+before-previous['address']:continue
                            if old.memory(previous['address'],previous['size'])==new.memory(current['address'],current['size']):
                                targets.append(previous)
                                renamed_literal_objects.append(dict(before=previous['name'],after=current['name'],bytes_identical=True))
                if not targets:unverified_literals.append(dict(function=s['name'],address=hex(word),before=hex(before),after=hex(after)))
    result=dict(baseline=str(baseline),candidate=str(candidate),
        baseline_sha256=hashlib.sha256(old.data).hexdigest(),candidate_sha256=hashlib.sha256(new.data).hexdigest(),
        region_begin=hex(begin),region_end=hex(end),function_count=len(a),addresses_sizes_order_identical=same,
        executable_bytes_identical=not code_changes,changed_code_functions=code_changes,
        literal_pool_changes=literal_changes,renamed_literal_objects=renamed_literal_objects,unverified_literal_relocations=unverified_literals,
        guarded_byte_identical=all(old.memory(s['address'],s['size'])==new.memory(s['address'],s['size']) for s in a if s['name']=='frost_chain_process_guarded'),
        passed=same and not code_changes and not unverified_literals,
        limit='Startup code before gain_sample is outside the executable-byte comparison. Changed literals must retain named object/offset; compiler-renumbered locals additionally require identical full object bytes. Passing does not prove startup or pedal timing.')
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--report',type=Path)
    args=p.parse_args();result=audit(args.baseline,args.candidate)
    output=json.dumps(result,indent=2)+'\n'
    if args.report:
        with args.report.open('x',encoding='utf-8') as f:f.write(output)
    print(output,end='')
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
