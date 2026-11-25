"""Stage the pinned OpenGP5 platform with Frost's muted diagnostic. Never flash."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def stage(platform,destination,controller='generic',bank=None,audio_input='synthetic',audio_output='muted',cab_ir=None,hot_ram=False,service_window_cycles=None,control_scheduler=False,cab_ir_taps=128,large_code=False,dsp_64k=False,flash_hash=False,xflash=False,overlays=False,reverb_int16=False,dsp_usage=False,fcm_bench=False,ir_link=False):
    if audio_output not in ('muted','line-test'):raise ValueError('Unknown audio output profile')
    line_test=audio_output=='line-test'
    if type(control_scheduler) is not bool or (control_scheduler and (not line_test or hot_ram or service_window_cycles not in (None,16384))):
        raise ValueError('Control scheduler requires line-test, original window and flash code')
    if service_window_cycles is not None and (type(service_window_cycles) is not int or service_window_cycles not in (16384,32768) or not line_test):
        raise ValueError('Service-window override is a 16384/32768-cycle line-test diagnostic only')
    if hot_ram and not line_test:raise ValueError('hot_ram is a line-test experiment only')
    if type(large_code) is not bool or (large_code and not control_scheduler):raise ValueError('large_code requires the control scheduler line test')
    if type(dsp_64k) is not bool or (dsp_64k and not large_code):raise ValueError('dsp_64k requires the large-code line test')
    if type(flash_hash) is not bool or (flash_hash and not large_code):raise ValueError('flash_hash requires the large-code line test')
    if type(xflash) is not bool or (xflash and (not large_code or dsp_64k)):raise ValueError('xflash requires large_code at the stock DSP length')
    if type(overlays) is not bool or (overlays and not xflash):raise ValueError('overlays require xflash')
    if type(reverb_int16) is not bool or (reverb_int16 and not overlays):raise ValueError('reverb_int16 requires the overlay line-test profile')
    if type(ir_link) is not bool or (ir_link and (not overlays or not dsp_usage)):raise ValueError('ir_link requires the overlay + DSP-usage profile (stage 4)')
    if fcm_bench and (not overlays or not (ROOT/'effects/fcm/model_bank.h').is_file()):raise ValueError('fcm_bench needs --overlays and a generated effects/fcm/model_bank.h')
    if type(dsp_usage) is not bool or (dsp_usage and (not control_scheduler or controller!='generic')):raise ValueError('dsp_usage requires the generic control-scheduler profile')
    if line_test and (audio_input!='adc' or controller!='generic' or bank is not None):
        raise ValueError('Line test requires ADC, generic controller and default bank')
    if audio_input not in ('synthetic','adc'):raise ValueError('Unknown audio input profile')
    if controller not in ('generic','diagnostic'):raise ValueError('Unknown controller profile')
    if cab_ir_taps not in (128,256,512) or (cab_ir_taps!=128 and (cab_ir is None or not line_test)):
        raise ValueError('cab IR taps: 128, or 256/512 for a line-test cab IR (FFT path)')
    if cab_ir_taps==512 and not reverb_int16:
        raise ValueError('512-tap cab IR requires the compact-reverb overlay profile for RAM')
    cab_header=cab_info=None
    if cab_ir is not None:
        # Validate/convert before touching the stage; any bad file fails closed.
        from cab_ir import convert,header
        cab_data=Path(cab_ir).read_bytes();h,cab_info=convert(cab_data,Path(cab_ir).name,cab_ir_taps)
        # FFT cabinets use q15 (DSP flash); the original 128-tap profile stays float.
        cab_info=dict(cab_info,table_format='q15' if cab_ir_taps>=256 else 'float');cab_header=header(h,cab_info,cab_info['table_format'])
    platform=platform.resolve();destination=destination.resolve()
    lock=json.loads((ROOT/'dependencies.lock.json').read_text())['OpenGP5']
    revision=subprocess.check_output(['git','-C',str(platform),'rev-parse','HEAD'],text=True).strip()
    if revision!=lock['commit']:raise ValueError('OpenGP5 revision differs from dependencies.lock.json')
    if subprocess.check_output(['git','-C',str(platform),'status','--porcelain'],text=True).strip():
        raise ValueError('Use a clean pinned OpenGP5 checkout')
    if destination.exists():raise ValueError('Stage exists; choose a new path to preserve artifacts')
    bank_header=None;bank_info=None
    if bank is not None:
        if controller!='generic':raise ValueError('User banks require the generic controller')
        from preset_bank import MAX_BANK_BYTES,decode_bank,header
        with Path(bank).open('rb') as file:payload=file.read(MAX_BANK_BYTES+1)
        captures=decode_bank(payload);bank_header=header(captures)
        bank_info=dict(sha256=hashlib.sha256(payload).hexdigest(),entries=len(captures),
            generated_header_sha256=hashlib.sha256(bank_header).hexdigest())
    from platform_compat import patch_main,patch_responder,patch_dsp_link,validate_stack_layout,patch_audio,patch_flash,patch_ir_link
    flash_driver=patch_flash((platform/'platform/nds32/hal/flash.c').read_text()) if controller=='generic' else None
    controller_main=patch_main((platform/'platform/nds32/main.c').read_text(),controller)
    irq_table=app_ld=None
    if ir_link:controller_main,irq_table,app_ld=patch_ir_link(controller_main,(platform/'platform/nds32/start/irq.c').read_text(),(platform/'platform/nds32/start/app.ld').read_text())
    validate_stack_layout((platform/'platform/dsp/link.ld').read_text())
    responder=patch_responder((platform/'platform/dsp/responder.c').read_text())
    audio=patch_audio((platform/'platform/dsp/audio.h').read_text())
    dsp_link=patch_dsp_link((platform/'platform/nds32/dsp_link.c').read_text(),dsp_usage)
    # Avoid recursively copying the checkout into itself.
    if destination==platform or platform in destination.parents:raise ValueError('Stage must be outside the platform checkout')
    shutil.copytree(platform,destination,ignore=shutil.ignore_patterns('.git','fw-build','build','out','__pycache__'))
    for p in destination.rglob('*.sh'):p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n'))
    fw=destination/'firmwares/frost';dsp=fw/'dsp';dsp.mkdir(parents=True)
    for name in ('firmware.h','global_eq.h','output.h','input.h','link_rx.h','link_rx.inc','control_service.inc','stack_watch.h','stack_boot.inc','timing_watch.h','link_tx.h','link_tx.inc','link_tx_platform.inc','line_output.h','fault_report.inc','flash_probe.h','ir_files.h','ir_link.h'):
        shutil.copy2(ROOT/'firmware/dsp'/name,dsp/name)
    if audio_input=='adc':
        p=dsp/'firmware.h';p.write_text('#define FROST_AUDIO_ADC 1\n'+p.read_text())
    if line_test:
        p=dsp/'firmware.h';p.write_text('#define FROST_LINE_TEST 1\n'+('#define FROST_HOT_RAM 1\n' if hot_ram else '')+p.read_text())
    for name in ('engine','effects'):shutil.copytree(ROOT/name,dsp/name)
    shutil.copytree(ROOT/'firmware/protocol',dsp/'firmware/protocol')
    shutil.copytree(ROOT/'tests/fixtures',dsp/'tests/fixtures')
    if cab_header is not None:(dsp/'tests/fixtures/diagnostic_ir.h').write_text(cab_header)
    shutil.copytree(ROOT/'firmware/controller',fw/'nds32')
    if bank_header is not None:(fw/'nds32/user_bank.h').write_bytes(bank_header)
    # Both processors consume the same source headers. The controller gets
    # metadata only, never the DSP registry implementation or effect code.
    (fw/'nds32/engine').mkdir()
    for name in ('metadata.h','control_catalog.h','human_controls.h','library.h'):
        shutil.copy2(ROOT/'engine'/name,fw/'nds32/engine'/name)
    (fw/'nds32/firmware/protocol').mkdir(parents=True)
    shutil.copy2(ROOT/'firmware/version.h',fw/'nds32/firmware/version.h')
    for name in ('settings.h','slot_wire.h','sync_wire.h','preset_wire.h','quick_preset_wire.h','preset_read_wire.h','policy_wire.h','bank_read_wire.h','line_wire.h','dsp_usage_wire.h','fcm_wire.h','global_wire.h','tuner_wire.h','ir_wire.h','ir_link_wire.h'):
        shutil.copy2(ROOT/'firmware/protocol'/name,fw/'nds32/firmware/protocol'/name)
    ui=fw/'nds32/firmware.c';ui.write_text(('#define FROST_FACTORY_PRESETS 1\n' if controller=='generic' and line_test else '')+('#define FROST_IR_LINK 1\n#define FROST_EMPTY_BOOT_CHAIN 1\n' if ir_link else '')+('#define FROST_LINE_TEST 1\n' if line_test else '')+('#define FROST_DSP_USAGE 1\n' if dsp_usage else '')+('#define FROST_SLOT_EDITOR 1\n' if controller=='generic' else '')+
        ('#define FROST_USER_BANK 1\n' if bank_header is not None else '')+'#define FROST_CHAIN_BENCH 1\n'+ui.read_text())
    shutil.copy2(ROOT/'assets/branding/startup_logo.h',destination/'platform/nds32/boot_logo.h')
    (destination/'platform/nds32/main.c').write_text(controller_main)
    if ir_link:(destination/'platform/nds32/start/irq.c').write_text(irq_table);(destination/'platform/nds32/start/app.ld').write_text(app_ld)
    if flash_driver is not None:(destination/'platform/nds32/hal/flash.c').write_text(flash_driver)
    (destination/'platform/dsp/responder.c').write_text(
        ('#define FROST_CONTROL_SCHEDULER 1\n' if control_scheduler else '')+
        (f'#define FROST_SERVICE_WINDOW {service_window_cycles}u\n' if service_window_cycles is not None else '')+
        ('#define FROST_LINE_TEST 1\n' if line_test else '')+
        ('#define FROST_LARGE_CODE 1\n' if large_code else '')+
        ('#define FROST_FLASH_HASH 1\n' if dsp_64k or flash_hash else '')+
        ('#define FROST_FLASH_PROBE 1\n' if dsp_64k else '')+
        ('#define FROST_OVERLAYS 1\n' if overlays else '')+
        ('#define FROST_REVERB_INT16 1\n' if reverb_int16 else '')+
        ('#define FROST_DSP_USAGE 1\n' if dsp_usage else '')+
        ('#define FROST_FCM_BENCH 1\n' if fcm_bench else '')+
        ('#define FROST_IR_LINK 1\n' if ir_link else '')+
        ('#define FROST_IR_FFT_PARTS 8u\n#define FROST_IR_FFT_SPLIT_SPECTRUM 1\n' if cab_ir_taps==512 else '')+responder)
    if reverb_int16:
        link=destination/'platform/dsp/link.ld';text=link.read_text()
        text=text.replace('*(.text*) *(.rodata*)','*(.text*) KEEP(*(.frost_legacy_reverb)) *(.rodata*)')   # 32l: the legacy loops go to XFLASH
        link.write_text(text)
    if hot_ram:
        # Opt-in experiment, measured SLOWER than cached flash on the GP-5
        # (docs/Build%20Firmware.md). Per-sample audio code runs from RAM: collect .frost_hot into .data (copied
        # to executable VARS at reset) and synchronize after the copy. Pinned anchors.
        link=destination/'platform/dsp/link.ld';text=link.read_text();needle='*(.data*)'
        if text.count(needle)!=1:raise ValueError('Pinned DSP linker .data layout mismatch')
        link.write_text(text.replace(needle,'*(.data*) *(.frost_hot*)'))
        resp=destination/'platform/dsp/responder.c';text=resp.read_text();needle='    resp_main();'
        if text.count(needle)!=1:raise ValueError('Pinned DSP startup call mismatch')
        resp.write_text(text.replace(needle,'    __asm__ volatile("dsb\\nisb" ::: "memory"); /* RAM code copied by reset */\n'+needle))
    if control_scheduler and not hot_ram:
        # 25c: the slot-edit control path executes from VARS RAM (FROST_CTL_RAM,
        # .data.frost_ctl). Synchronize instruction fetch after the reset copy,
        # exactly as the proven hot-RAM build did. Pinned anchor.
        resp=destination/'platform/dsp/responder.c';text=resp.read_text();needle='    resp_main();'
        if text.count(needle)!=1:raise ValueError('Pinned DSP startup call mismatch')
        resp.write_text(text.replace(needle,'    __asm__ volatile("dsb\\nisb" ::: "memory"); /* RAM code copied by reset */\n'+needle))
    # New cold callbacks must not enter the boot-sensitive .text* function order.
    link=destination/'platform/dsp/link.ld';quick_link=link.read_text()
    if quick_link.count('*(.rodata*)')!=1:raise ValueError('Pinned DSP cold-tail linker anchor mismatch')
    link.write_text(quick_link.replace('*(.rodata*)','*(.frost_quick_cold) *(.rodata*)',1))
    if large_code:
        # 31c: steady audio code (.lcode) executes from LARGE (0x10000000, Code
        # region, as stock runs its effect modules). Load image follows .data in
        # flash; c_start copies it after zeroing .large, before the existing
        # dsb/isb. .lcode precedes .large in LARGE. Pinned anchors.
        link=destination/'platform/dsp/link.ld';text=link.read_text();needle='  .large (NOLOAD) :'
        if text.count(needle)!=1:raise ValueError('Pinned DSP linker .large layout mismatch')
        link.write_text(text.replace(needle,'  .lcode : { __lcode_start = .; *(.lcode*) . = ALIGN(4); __lcode_end = .; } > LARGE AT > FLASH\n'
                                     '  __lcode_load = LOADADDR(.lcode);\n'+needle))
        resp=destination/'platform/dsp/responder.c';text=resp.read_text();needle='    for (d = __large_start; d < __large_end; ) *d++ = 0;\n'
        if text.count(needle)!=1:raise ValueError('Pinned DSP LARGE zeroing mismatch')
        resp.write_text(text.replace(needle,needle+'    {extern uint32_t __lcode_start[],__lcode_end[],__lcode_load[];'
                                     '__builtin_memcpy(__lcode_start,__lcode_load,(uint32_t)__lcode_end-(uint32_t)__lcode_start);} /* 31c: LARGE code */\n'))
        # The flash image must carry .lcode's load bytes (objcopy uses LMAs).
        builder=destination/'platform/dsp/build.py';text=builder.read_text();needle='"--only-section=.data",'
        if text.count(needle)!=1:raise ValueError('Pinned DSP objcopy layout mismatch')
        if not xflash:builder.write_text(text.replace(needle,'"--only-section=.data", "--only-section=.lcode",'))
        else:
            # 31g: .lcode's load image lives in a Frost-owned section 0x67 (dsp-data) at DSP flash
            # 0x14000 (proven by 31f), padded with 0xFF to stock's exact length 0x55028; c_start
            # copies it from there. Section 0x63 holds only .text/.data.
            link=destination/'platform/dsp/link.ld';ltext=link.read_text()
            n1='  LARGE (rw)  :';n2='> LARGE AT > FLASH'
            if ltext.count(n1)!=1 or ltext.count(n2)!=1:raise ValueError('Pinned DSP linker XFLASH anchors mismatch')
            link.write_text(ltext.replace(n1,'  XFLASH (rx) : ORIGIN = 0x14000,    LENGTH = 0x55028\n'+n1).replace(n2,'> LARGE AT > XFLASH'))
            n3='        binf = d / "hello.bin"'
            if text.count(n3)!=1:raise ValueError('Pinned DSP image step mismatch')
            text=text.replace(n3,'        xbin = Path(str(out) + ".dsp-data")\n'
                '        subprocess.run([tool("arm-none-eabi-objcopy"), "-O", "binary", "--only-section=.lcode", str(elf), str(xbin)], check=True)\n'
                '        xdata = xbin.read_bytes(); assert 0 < len(xdata) <= 0x55028\n'
                '        xbin.write_bytes(xdata + b"\\xff" * (0x55028 - len(xdata)))\n'+n3)
            builder.write_text(text)
            fwb=destination/'platform/build_firmware.sh';ftext=fwb.read_text();n4='python3 tools/gp5tool.py pack dsp="$TMP/dsp.bin" '
            if ftext.count(n4)!=1:raise ValueError('Pinned image pack step mismatch')
            fwb.write_text(ftext.replace(n4,'python3 tools/gp5tool.py pack dsp="$TMP/dsp.bin" dsp-data="$TMP/dsp.bin.dsp-data" '))
    if dsp_64k:
        # 31e: section 0x63 at the factory loader's documented maximum (64 KB,
        # DSP flash 0x4000..0x14000) instead of stock's 0xC348 length. The
        # header holds no length; the resident reads only the vector address.
        link=destination/'platform/dsp/link.ld';text=link.read_text();needle='LENGTH = 0x10348 - 0x4400'
        if text.count(needle)!=1:raise ValueError('Pinned DSP flash length mismatch')
        link.write_text(text.replace(needle,'LENGTH = 0x14000 - 0x4400'))
        builder=destination/'platform/dsp/build.py';text=builder.read_text();needle='SEC_LEN = 0xC348'
        if text.count(needle)!=1:raise ValueError('Pinned DSP section length mismatch')
        builder.write_text(text.replace(needle,'SEC_LEN = 0x10000'))
    if overlays:
        # 31i: one output section per overlay module, linked (VMA=LMA) in dsp-data after .lcode;
        # the dsp-data image carries them. Module list must match firmware.h FROST_OVL_MODULES.
        link=destination/'platform/dsp/link.ld';ltext=link.read_text();needle='  .large (NOLOAD) :'
        if ltext.count(needle)!=1:raise ValueError('Pinned DSP linker .large anchor mismatch')
        ovl=''.join('  .ovl_%s : ALIGN(8) { __ovl_%s_start = .; KEEP(*(.ovl.%s)) . = ALIGN(4); __ovl_%s_end = .; } > XFLASH\n'%(i,i,i,i) for i in ['ts', 'bd2', 'klon', 'bb', 'ocd', 'rat', 'distplus', 'dynacomp', 'compplus', 'ge7', 'ns2', 'phase90', 'univibe', 'tr2', 'mistress', 'm117', 'vb2', 'dd3', 'dm2', 'pog', 'fcm', 'drop', 'ns1x', 'multiband', 'highpass', 'lowpass', 'autowah', 'treble_boost', 'foxx', 'tape', 'vintage_fuzz', 'acoustic', 'ampheads', 'rotary', 'ringmod', 'crusher', 'harmtrem', 'muff', 'boost', 'swell', 'cali_eq', 'pingpong', 'sl100'])
        link.write_text(ltext.replace(needle,ovl+'  .frost_global_eq : ALIGN(4) { *(.frost_global_eq) *(.frost_xcode) *(.frost_xdata) *(.frost_legacy_reverb_code) } > XFLASH\n'+needle))
        builder=destination/'platform/dsp/build.py';btext=builder.read_text();n5='"--only-section=.lcode", str(elf), str(xbin)'
        if btext.count(n5)!=1:raise ValueError('Pinned dsp-data objcopy mismatch')
        builder.write_text(btext.replace(n5,'"--only-section=.lcode", "--only-section=.ovl_*", "--only-section=.frost_global_eq", str(elf), str(xbin)'))
    (destination/'platform/dsp/audio.h').write_text(audio)
    (destination/'platform/nds32/dsp_link.c').write_text(dsp_link)
    builder=destination/'platform/dsp/build.py';text=builder.read_text();needle='        binf = d / "hello.bin"'
    if text.count(needle)!=1:raise ValueError('DSP ELF retention patch mismatch')
    builder.write_text(text.replace(needle,'        import shutil\n        audit = ROOT / "build" / "audit"\n        audit.mkdir(parents=True, exist_ok=True)\n        shutil.copy2(elf, audit / "dsp.elf")\n'+needle))
    inputs={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
            for name in ('engine','effects','firmware','assets/branding','tests/fixtures')
            for p in (ROOT/name).rglob('*') if p.is_file()}
    (destination/'frost-input.json').write_text(json.dumps(dict(platform_commit=revision,release_version=re.search(r'#define FROST_VERSION "([^"]+)"',(ROOT/'firmware/version.h').read_text())[1],inputs=inputs,
        platform_adaptations=dict(dsp_tx='queued1024-drain16-reserve516',startup_prepare='one-private-block-reset-before-DMA',audio_timing_trace='first8-worst-firstlate-last',stack_watermark='4KiB-before-C',raw_uart_pacing='8bytes-3ticks-over16',dsp_rx_recovery='bounded-v1',panel_madctl='0xA8',pixel_packing='RGB565',raw_flash_commands=False,
            bank_bridge='7E46' if controller=='generic' else None),
        staging_tools={name:hashlib.sha256((ROOT/'tools'/name).read_bytes()).hexdigest() for name in ('prepare_gp5.py','platform_compat.py')},
        purpose='Audio-capable unity-master line-input test; no native NAM' if line_test else 'Muted effects-only diagnostic; no NAM',ir_link=ir_link,audio_output=audio_output,output_package='GP5_Frost_line_test.bin' if line_test else 'GP5_Frost_muted.bin',line_test_cap_q16=65536 if line_test else 0,service_window_cycles=service_window_cycles or 16384,control_scheduler=control_scheduler,dsp_usage=dsp_usage,fcm_bench=fcm_bench,reverb_int16=reverb_int16,hot_ram=hot_ram,audio_input=audio_input,controller_profile=controller,user_bank=bank_info,cab_ir=None if cab_info is None else dict(cab_info,header_sha256=hashlib.sha256(cab_header.encode()).hexdigest()),hard_muted=not line_test,hardware_ready=False),indent=2)+'\n')
    return destination

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--platform',type=Path,default=ROOT/'.deps/OpenGP5')
    p.add_argument('--stage',type=Path,default=ROOT/'build/gp5-muted')
    p.add_argument('--controller',choices=('generic','diagnostic'),default='generic')
    p.add_argument('--bank',type=Path,help='Validated lossless .fbank; immutable controller ROM, never flash storage')
    p.add_argument('--audio-input',choices=('synthetic','adc'),default='synthetic',help='chain input; both profiles always write stereo silence')
    p.add_argument('--audio-output',choices=('muted','line-test'),default='muted',help='line-test is audio-capable, requires ADC/generic/default bank and separate flash/sound approval')
    p.add_argument('--cab-ir',type=Path,help='User cabinet IR WAV baked in as the boot IR (see tools/cab_ir.py: mono, 44.1 kHz, first 128 taps, L1-normalized)')
    p.add_argument('--cab-ir-taps',type=int,choices=(128,256,512),default=128,help='Kept cab IR taps; 256 uses FFT, 512 also needs --reverb-int16 for RAM (line test)')
    p.add_argument('--hot-ram',action='store_true',help='Experiment: run per-sample audio code from VARS RAM (measured slower on the GP-5; not recommended)')
    p.add_argument('--service-window-cycles',type=int,choices=(16384,32768),help='Line-test diagnostic override; default stays 16384; no timing acceptance implied')
    p.add_argument('--control-scheduler',action='store_true',help='Opt-in early admission, fair RX/report phases and unsolicited progress diagnostics')
    p.add_argument('--large-code',action='store_true',help='31c experiment: steady audio code executes from LARGE (needs --control-scheduler)')
    p.add_argument('--flash-hash',action='store_true',help='31f: read-only DSP flash hash command at the stock section length (needs --large-code)')
    p.add_argument('--overlays',action='store_true',help='31i: stock-style overlay modules in dsp-data, loaded into LARGE per route (needs --xflash)')
    p.add_argument('--reverb-int16',action='store_true',help='31j: opt-in int16 reverb histories; keeps legacy boot helpers (needs --overlays)')
    p.add_argument('--fcm-bench',action='store_true',help='EXPERIMENTAL FCM timing candidate: POG slot runs the baked effects/fcm/model_bank.h model (needs --overlays)')
    p.add_argument('--ir-link',action='store_true',help='Stage 4: cabinet IR convolution on the controller over the SPI audio link (needs --overlays --dsp-usage)')
    p.add_argument('--dsp-usage',action='store_true',help='31m: top-right DSP block-load meter from existing reports, no polling')
    p.add_argument('--xflash',action='store_true',help='31g: LARGE-code load image in a Frost-owned section 0x67 at DSP 0x14000 (needs --large-code)')
    p.add_argument('--dsp-64k',action='store_true',help='31e experiment: 64 KB DSP section and read-only flash hash command (needs --large-code)')
    a=p.parse_args();print(stage(a.platform,a.stage,a.controller,a.bank,a.audio_input,a.audio_output,a.cab_ir,a.hot_ram,a.service_window_cycles,a.control_scheduler,a.cab_ir_taps,a.large_code,a.dsp_64k,a.flash_hash,a.xflash,a.overlays,a.reverb_int16,a.dsp_usage,a.fcm_bench,a.ir_link))
