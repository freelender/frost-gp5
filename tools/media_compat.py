"""Backport the locked OpenGP5 1.0 USB/BLE drivers onto Frost's base platform.

The two source pins stay independent: Frost keeps its UART/storage/recovery
adaptations and its single SPI/IR IRQ. No device access or generated source
is stored in the product checkout.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
FILES=('usb_audio.c','usb_audio.h','usb_resample.h','usb_midi.c','usb_midi.h',
       'hal/usb.c','hal/usb.h','proto.c','proto.h','gp5_log.c',
       'bt/radio.c','bt/radio.h','bt/ble.c','bt/ble.h','bt/gp5_bt.c')

def validate(source):
    source=source.resolve()
    lock=json.loads((ROOT/'dependencies.lock.json').read_text())['OpenGP5Media']
    revision=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
    if revision!=lock['commit']:raise ValueError('Media source differs from dependencies.lock.json')
    if subprocess.check_output(['git','-C',str(source),'status','--porcelain'],text=True).strip():
        raise ValueError('Use a clean pinned OpenGP5 media checkout')
    return source,revision

def replace(s,old,new):
    if s.count(old)!=1:raise ValueError('Media integration anchor mismatch: '+old[:100])
    return s.replace(old,new)

def apply(stage,source):
    source,revision=validate(source)
    app=stage/'platform/nds32';up=source/'platform/nds32'
    hashes={}
    for name in FILES:
        original=(up/name).read_bytes();hashes[name]=hashlib.sha256(original).hexdigest()
        p=app/name;p.parent.mkdir(parents=True,exist_ok=True)
        s=original.decode('utf-8').replace('\r\n','\n')
        # The base linker has ample ordinary RAM but no .lowram region.
        s=s.replace('#define LOWRAM __attribute__((section(".lowram")))','#define LOWRAM')
        if name=='usb_midi.c':
            s=replace(s,'"OpenGP5", "OpenGP5", USB_SERIAL, 0, "OpenGP5 MIDI"',
                      '"Frost", "GP-5 Open FW", "F144", 0, "Frost MIDI"')
        if name=='bt/gp5_bt.c':
            s=replace(s,"0x08, 0x09, 'O', 'p', 'e', 'n', 'G', 'P', '5'", "0x06, 0x09, 'F', 'r', 'o', 's', 't'")
            s=s.replace('advertising as OpenGP5','advertising as Frost')
        if name=='bt/ble.h':
            s=replace(s,'void ble_poll(void);','void gp5_bt_set_enabled(int on);\nvoid ble_poll(void);')
        if name=='bt/ble.c':
            s=replace(s,'int ble_start(const uint8_t addr[6]',
                'static unsigned frost_ble_ready;\nint ble_start(const uint8_t addr[6]')
            # Initialize RF before audio starts, but wait for saved settings
            # before scheduling advertising. ON/OFF never calls radio_start.
            s=replace(s,'BIT_SET(BLE + 0x00, 1u << 8, 1u << 8);',
                'BIT_SET(BLE + 0x00, 1u << 8, 0);')
            s=replace(s,'    ble.state = BLE_ADVERTISING;\n    adv_schedule(slot_now());\n    return 0;',
                '    ble.state = BLE_OFF;\n    frost_ble_ready = 1;\n    return 0;')
            s=replace(s,'void ble_poll(void) {',
                '#include "media_bluetooth.inc"\nvoid ble_poll(void) {')
            s=replace(s,'    uint32_t now = gp5_ms(), on;',
                '    uint32_t now = gp5_ms(), on;\n    if (ble.state == BLE_OFF) return;')
            s=replace(s,'    tr(0x10 | was, st);',
                '    if (was == BLE_OFF) return;\n    tr(0x10 | was, st);')
            s=replace(s,'    tr(0x20, st);',
                '    if (ble.state == BLE_OFF) return;\n    tr(0x20, st);')
        if name=='proto.c':
            # A USB SysEx and a BLE SysEx may be interleaved. Their parser
            # storage must remain separate while replies follow proto_port.
            s=replace(s,'static uint8_t nib[2 * (4 + PROTO_REQ_MAX)];',
                      'static uint8_t port_nib[2][2 * (4 + PROTO_REQ_MAX)];\n#define nib port_nib[proto_port]')
            s=replace(s,'static unsigned nnib;', 'static unsigned port_nnib[2];\n#define nnib port_nnib[proto_port]')
            s=replace(s,'static uint8_t in_frame;', 'static uint8_t port_in_frame[2];\n#define in_frame port_in_frame[proto_port]')
            s=replace(s,'        proto_reply(0x01, 0, ack, 3);',
                      '        if (proto_port) return PROTO_NONE; /* flashing is USB-only */\n        proto_reply(0x01, 0, ack, 3);')
        p.write_text(s,encoding='utf-8',newline='\n')
    shutil.copy2(source/'platform/dsp/gp5_fmt.h',app/'gp5_fmt.h')
    # Add only the API declarations the imported drivers use. Frost's own
    # firmware API and defaults remain those of the base platform.
    p=app/'gp5_firmware.h';s=p.read_text();s+='''
#ifndef FROST_MEDIA_PLATFORM_API
#define FROST_MEDIA_PLATFORM_API
#define GP5_SAMPLE_RATE 44100u
#define GP5_BLOCK 64u
#define GP5_CTL_MHZ 288u
void gp5_log(const char *,...);
void gp5_bt_start(void);
void ble_poll(void);
uint32_t gp5_ms(void);
#endif
''';p.write_text(s,newline='\n')
    p=app/'dsp_ctl.h';s=p.read_text();s+='\nextern volatile uint8_t dsp_ctl_direct_off;\n';p.write_text(s,newline='\n')
    p=app/'main.c';s=p.read_text()
    s=replace(s,'#include "led.h"', '#include "led.h"\n#include "usb_audio.h"')
    s=replace(s,'    int dsp_show = 0, host_asked, status = 0;',
              '    unsigned asked_port=0;\n    int dsp_show = 0, host_asked, status = 0;')
    s=replace(s,'        n = usb_midi_read(mbuf);\n        host_asked = 0;',
              '        usb_audio_poll();ble_poll();\n        host_asked = 0;\n        for(unsigned port=0;port<2u;++port){\n        proto_port=(uint8_t)port;\n        n=port?gp5_midi_port_read(mbuf,sizeof mbuf):usb_midi_read(mbuf);')
    s=replace(s,'if (proto_req[1] == 0x00) host_asked = 1;',
              'if (proto_req[1] == 0x00) { host_asked = 1;asked_port=port; }')
    s=replace(s,'        dsp_link_poll();', '        }\n        proto_port=0;\n        dsp_link_poll();')
    s=replace(s,'                proto_reply(0x01, 0, pl, 7);',
              '                proto_port=(uint8_t)asked_port;proto_reply(0x01, 0, pl, 7);proto_port=0;')
    # Start radio after the tick and interrupt mask, before the DSP/IR link
    # starts. It calibrates for about 1.1 s; never inside the audio callback.
    s=replace(s,'    usb_midi_init();','    gp5_bt_start();\n    usb_midi_init();')
    p.write_text(s,newline='\n')
    p=app/'start/irq.c';s=p.read_text();s=replace(s,'extern void Timer2Interrupt(void);',
        'extern void gp5_usb_irq(void);\nextern void gp5_bt_irq(void);\nextern void gp5_ble_irq(void);\nextern void Timer2Interrupt(void);')
    s=replace(s,'    [GP5_IRQ_TIMER2] = H(Timer2Interrupt),',
        '    [GP5_IRQ_TIMER2] = H(Timer2Interrupt),\n    [GP5_IRQ_USB] = H(gp5_usb_irq),\n    [18] = H(gp5_bt_irq),\n    [19] = H(gp5_ble_irq),')
    p.write_text(s,newline='\n')
    p=app/'build.sh';s=p.read_text();s=replace(s,'# Platform (start-up,',
        'CF="$CF -DFROST_USB_AUDIO=1 -DGP5_USB_AUDIO=1 -DGP5_USB_PLAY_AT=166u -DGP5_USB_REC_AT=166u"\n\n# Platform (start-up,')
    s=replace(s,'"$APP/main.c"', '"$APP/usb_audio.c" "$APP/gp5_log.c" "$APP/bt/radio.c" "$APP/bt/ble.c" "$APP/bt/gp5_bt.c" "$APP/main.c"')
    p.write_text(s,newline='\n')
    p=stage/'platform/dsp/responder.c';p.write_text('#define FROST_USB_AUDIO 1\n'+p.read_text(),newline='\n')
    p=stage/'firmwares/frost/nds32/firmware.c';p.write_text('#define FROST_USB_AUDIO 1\n'+p.read_text(),newline='\n')
    return dict(commit=revision,source_sha256=hashes,usb_audio='stereo-16bit-44100',bluetooth='BLE-MIDI',
                recording='post-effects/pre-master/pre-playback',physical_validation=False)
