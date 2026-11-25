"""ARM emulator cost of the DROP and OCTAVE kernels, working tree vs a Git revision.

Compiles tools/pitch_arm_bench.c with the firmware's arm-none-eabi-gcc flags
for both trees, runs every 64-sample block in the Unicorn Cortex-M4 emulator
and reports instructions per block (mean and max over chord/attack input)
plus the overlay module sizes (.ovl.drop / .ovl.pog). These are EMULATOR
INSTRUCTION COUNTS, not pedal cycles; the pedal's slot-load meter decides.

Needs unicorn and pyelftools (e.g. the frost-shimmer-32a emu-venv Python) and
the Linux ARM toolchain (run through WSL on Windows).

  <emu python> tools/pitch_arm_bench.py --toolchain <dir with arm-none-eabi-gcc (Windows or Linux path)> [--base HEAD] [--json out.json]
"""
import argparse,json,os,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
FLAGS=['-mcpu=cortex-m4','-mthumb','-mfloat-abi=hard','-mfpu=fpv4-sp-d16','-O2','-ffreestanding','-fno-math-errno','-nostdlib','-nostartfiles',
       '-ffunction-sections','-fdata-sections','-DGP5_FIRMWARE','-Wno-unused']
LD=('MEMORY { RAM (rwx) : ORIGIN = 0x10000000, LENGTH = 0x400000 }\n'
    'SECTIONS { .all : { *(.text*) *(.lcode*) *(.frost*) *(.ovl*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) } > RAM }\n')

def wsl_path(p):
    p=str(Path(p).resolve()).replace('\\','/')
    return '/mnt/'+p[0].lower()+p[2:] if os.name=='nt' and p[1]==':' else p
def tool(tc,name,args):
    cmd=[(wsl_path(tc) if os.name=='nt' and ':' in tc else tc).rstrip('/')+'/'+name]+args
    if os.name=='nt':cmd=['wsl','-e']+cmd
    r=subprocess.run(cmd,capture_output=True,text=True)
    if r.returncode:raise SystemExit(r.stderr[-3000:])
    return r.stdout

def compile_tree(tc,src,out):
    out.mkdir(parents=True,exist_ok=True);(out/'bench.ld').write_text(LD)
    bench=wsl_path(ROOT/'tools/pitch_arm_bench.c');inc=wsl_path(src)
    tool(tc,'arm-none-eabi-gcc',FLAGS+['-I'+inc,'-c',bench,'-o',wsl_path(out/'bench.o')])
    tool(tc,'arm-none-eabi-gcc',FLAGS+['-I'+inc,'-T',wsl_path(out/'bench.ld'),bench,'-lgcc','-o',wsl_path(out/'bench.elf')])
    sizes={}
    for line in tool(tc,'arm-none-eabi-objdump',['-h',wsl_path(out/'bench.o')]).splitlines():
        f=line.split()
        if len(f)>3 and f[1].startswith('.ovl.'):
            m=f[1][5:].split('.')[0];sizes[m]=sizes.get(m,0)+int(f[2],16)
    return out/'bench.elf',sizes

class Emu:
    def __init__(s,path):
        import unicorn
        from unicorn import arm_const as arm
        from elftools.elf.elffile import ELFFile
        s.arm=arm
        u=unicorn.Uc(unicorn.UC_ARCH_ARM,unicorn.UC_MODE_THUMB|unicorn.UC_MODE_MCLASS);u.ctl_set_cpu_model(arm.UC_CPU_ARM_CORTEX_M4)
        u.mem_map(0x10000000,0x400000);u.mem_map(0x20000000,0x100000);u.mem_map(0xe0000000,0x100000);u.mem_map(0x01000000,0x1000)
        with open(path,'rb') as f:
            e=ELFFile(f);s.sym={x.name:x['st_value'] for x in e.get_section_by_name('.symtab').iter_symbols()}
            for seg in e.iter_segments():
                if seg['p_type']=='PT_LOAD' and seg['p_filesz']:u.mem_write(seg['p_paddr'],seg.data())
        u.mem_write(0x01000000,b'\x00\xbe');u.mem_write(0xe000ed88,struct.pack('<I',0xf<<20))
        s.u=u;s.n=0
        u.hook_add(unicorn.UC_HOOK_CODE,s._count)
    def _count(s,uc,a,size,ud):s.n+=1
    def call(s,name,*args):
        a=s.arm;regs=[a.UC_ARM_REG_R0,a.UC_ARM_REG_R1,a.UC_ARM_REG_R2,a.UC_ARM_REG_R3]
        for i,v in enumerate(args):s.u.reg_write(regs[i],v)
        s.u.reg_write(a.UC_ARM_REG_SP,0x20080000);s.u.reg_write(a.UC_ARM_REG_LR,0x01000001)
        s.n=0;s.u.emu_start(s.sym[name]|1,0x01000000);return s.n

SETTINGS=[('DROP -1',0,(0.,100.)),('DROP -2',0,(1.,100.)),('DROP -6',0,(5.,100.)),
          ('OCTAVE default',1,(70.,40.,40.)),('OCTAVE sub only',1,(0.,100.,0.)),('OCTAVE up only',1,(0.,0.,100.))]
def measure(elf,blocks=700):
    e=Emu(elf);res={}
    for name,k,p in SETTINGS:
        e.u.mem_write(e.sym['bench_p'],struct.pack('<4f',*(list(p)+[0.]*4)[:4]));e.call('bench_reset',k)
        counts=[(e.call('bench_fill',0 if b<20 else 2),e.call('bench_block',k))[1] for b in range(blocks)]
        s=counts[40:];res[name]=dict(mean=round(sum(s)/len(s)),p99=sorted(s)[int(.99*len(s))],max=max(s))
    return res

def main():
    a=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument('--toolchain',required=True);a.add_argument('--base',default='HEAD');a.add_argument('--json')
    o=a.parse_args()
    from pitch_harness import export_tree
    base=export_tree(o.base,ROOT/'build/pitch/base-src')
    out={}
    for tag,src in (('base',base),('work',ROOT)):
        elf,sizes=compile_tree(o.toolchain,src,ROOT/('build/pitch/arm-'+tag))
        out[tag]=dict(overlay_bytes=sizes,blocks=measure(elf))
    for name,_,_ in SETTINGS:
        b,w=out['base']['blocks'][name],out['work']['blocks'][name]
        print('%-16s instructions/block mean %6d -> %6d   p99 %6d -> %6d   max %6d -> %6d'%(name,b['mean'],w['mean'],b['p99'],w['p99'],b['max'],w['max']))
    for m in sorted(set(out['base']['overlay_bytes'])|set(out['work']['overlay_bytes'])):
        print('overlay %-6s %5s -> %5s bytes'%(m,out['base']['overlay_bytes'].get(m),out['work']['overlay_bytes'].get(m)))
    if o.json:Path(o.json).write_text(json.dumps(out,indent=1))

if __name__=='__main__':main()
