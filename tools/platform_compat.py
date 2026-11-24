"""Exact, fail-closed adaptations for the locked OpenGP5 controller source."""
import hashlib

def patch_main(text,controller):
    if controller not in ('generic','diagnostic'):raise ValueError('Unknown controller profile')
    # Keep Frost's confirmed RGB565 packing, logo and panel setup together.
    panel='0x3A, 1, 0x05,  0x36, 1, 0xA0,  0x28, 0,  0x29, 0,'
    if text.count(panel)!=1:raise ValueError('Pinned panel setup mismatch')
    text=text.replace(panel,panel.replace('0xA0','0xA8'))
    text=text.replace('/* 0x36 = 0xA0: rotation; colour order bit clear (0xA8 swapped red and blue) */',
                      '/* Frost: preserve confirmed RGB565 packing with panel0xA8. */')
    # Strip all newly introduced raw flash access, including erase/program.
    # Frost has no persistent-storage integration; do not silently expose it.
    begin='    } else if (proto_req[1] == 0x05 && proto_req_len == 7) {'
    finish='    } else return;'
    if text.count(begin)!=1:raise ValueError('Pinned flash dispatch start mismatch')
    start=text.index(begin);end=text.index(finish,start)
    block=text[start:end]
    if hashlib.sha256(block.encode()).hexdigest()!='3599fe215d3ce65e632c32170dd11eca6ebc0b034f062b267e795cb59e5b1012':
        raise ValueError('Pinned flash dispatch mismatch')
    text=text[:start]+text[end:]
    patches={
        'if (!gfx && dsp_ctl_step == DSP_CTL_DONE)':'if (!gfx) /* Frost muted diagnostic */',
        'return (uint16_t)(((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3));':'return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));',
        'yellow = rgb(255,255,0), green = rgb(0,255,0)':'yellow = rgb(40,156,255), green = rgb(40,156,255)',
        'if (dsp_ctl_step == DSP_CTL_DONE) fw_dsp_ready();':'if (dsp_ctl_step == DSP_CTL_DONE) { fx_startup_acked = 0; fw_dsp_ready(); }'
    }
    for old,new in patches.items():
        if text.count(old)!=1:raise ValueError('Pinned platform patch mismatch: '+old)
        text=text.replace(old,new)
    old_link='''if (dsp_ctl_step == DSP_CTL_DONE && (int32_t)(gp5_ms() - fx_next) >= 0) {
            uint32_t id, val;
            if (fw_take(&id, &val)) {
                uint8_t pl[12], rep[8], st;
                pl[0] = 0x10; pl[1] = 0x7F; pl[2] = 12; pl[3] = 0;
                for (i = 0; i < 4; i++) { pl[4 + i] = (uint8_t)(id >> (8 * i)); pl[8 + i] = (uint8_t)(val >> (8 * i)); }
                dsp_link_raw(pl, sizeof pl, rep, sizeof rep, 100, &st);
                if (st == 0x14 && id < 32) fx_startup_acked |= 1u << id;
                fx_next = gp5_ms() + 20;
            }
        }'''
    if text.count(old_link)!=1:raise ValueError('Pinned controller transport patch mismatch')
    text=text.replace(old_link,'#include "'+('slot_transport.inc' if controller=='generic' else 'transport.inc')+'"')
    if controller=='generic':
        midi_hook='            r = proto_feed(mbuf + i);'
        if text.count(midi_hook)!=1:raise ValueError('Pinned USB MIDI dispatch mismatch')
        text=text.replace(midi_hook,'            { extern void fw_midi_event(const uint8_t *); fw_midi_event(mbuf + i); }\n'+midi_hook)
        hook='    pl[n++] = 0x7E; pl[n++] = proto_req[1];'
        if text.count(hook)!=1:raise ValueError('Pinned controller bank-export hook mismatch')
        text=text.replace(hook,'#include "bank_host.inc"\n'+hook)
    return '#include "firmware/protocol/sync_wire.h"\n#include "firmware/protocol/quick_preset_wire.h"\n'+(
        '#include "firmware/protocol/bank_read_wire.h"\n' if controller=='generic' else '')+text


def validate_stack_layout(text):
    if hashlib.sha256(text.encode()).hexdigest() != '5f9d35f4c9652e201088976fd1f8cf9e68e2dfe02fd7b7f1c540fef94a8afb81':
        raise ValueError("Pinned DSP linker stack layout mismatch")

def patch_responder(text):
    a=text.index('static uint8_t crc8(');b=text.index('static void log_frame',a)
    if hashlib.sha256(text[a:b].encode()).hexdigest()!='a37a1466e033e75965115f5a93864c4226d0b960a74fd86eedac8eae91886b3d':
        raise ValueError('Pinned DSP transmit hooks mismatch')
    text=text[:a]+'#include "link_tx_platform.inc"\n\n'+text[b:]

    # Paint the reserved stack before the first C call. Pin both reset sequence
    # and linker contract; never clear a live stack from a command or fw_init.
    prefix='    __asm volatile("ldr r0, =0x20006C00\\n"  /* stack below the rx buffer; status block stays clear */\n'
    if text.count(prefix)!=1:raise ValueError('Pinned reset stack top mismatch')
    reset='                   "msr msp, r0\\n"\n'
    if text.count(reset)!=1:raise ValueError('Pinned reset stack hook mismatch')
    text=text.replace(reset,reset+'#include "stack_boot.inc"\n')
    begin='    const uint32_t BUF_MAX = 512u;'
    end='\n    }\n}\n\nvoid default_handler'
    if text.count(begin)!=1:raise ValueError('Pinned DSP receiver start mismatch')
    a=text.index(begin);b=text.index(end,a);old=text[a:b]
    if hashlib.sha256(old.encode()).hexdigest()!='2941f31e90ac74ba13900b064a63d91b629f7b9c0858a73b84c5edadfa4adff1':raise ValueError('Pinned DSP receiver mismatch')
    # Leave audio/report calls in their original order; only replace receive parsing.
    # Each non-audio step is DWT-timed for servicing-gap attribution (0x7f3c).
    # 26b: opt-in fault reporter replaces the spin-forever handler only on the
    # control-scheduler profile; the default preprocessor output is unchanged.
    handler='void default_handler(void) {\n    ST[3] = 0xDEAD0000u | (REG(0xE000ED04u) & 0x1FFu);\n    for (;;) {}\n}\n'
    if text[b:].count(handler)!=1:raise ValueError('Pinned DSP default handler mismatch')
    tail=text[b:].replace(handler,'#ifdef FROST_CONTROL_SCHEDULER\n#include "fault_report.inc"\n#else\n'+handler+'#endif\n')
    text=text[:b]+tail
    return text[:a]+"""    for (;;) {
        ST[1]++;
#ifdef AUDIO_PASSTHROUGH
        audio_poll();
#endif
#define FROST_SERVICE_WATCH 1
        uint32_t frost_iteration=REG(0xE0001004u),frost_svc=frost_iteration;
#ifdef FROST_CONTROL_SCHEDULER
#include "control_service.inc"
#else
#include "link_tx.inc"
        frost_svc=frost_service_note(&frost_service,FROST_SERVICE_TX,frost_svc,REG(0xE0001004u));
        if (frost_service_open(&frost_service,frost_timing.last_end,REG(0xE0001004u)) && (REG(0xE000E010u) & 0x10000u)) {
            frost_service_take(&frost_service); send_report();   /* COUNTFLAG stays set until read */
        }
        frost_svc=frost_service_note(&frost_service,FROST_SERVICE_REPORT,frost_svc,REG(0xE0001004u));
#include "link_rx.inc"
#endif
        frost_service_iteration(&frost_service,frost_iteration,REG(0xE0001004u));
"""+text[b:]


def patch_dsp_link(text,dsp_usage=False):
    old='    gp5_uart0_send(tx, len, 100);           /* 404 bytes take 9 ms at 460800 baud */'
    if text.count(old)!=1:raise ValueError('Pinned raw UART send hook mismatch')
    text=text.replace(old,'#include "paced_uart.inc"')
    # beta.12: the receive interrupt fires only for the first byte into an empty
    # 4-byte FIFO. A flash write stalls this CPU while the DSP keeps sending; the
    # bytes then wait in a full FIFO and the post-write interrupt clear removes the
    # only trigger, so the link never drained again (DSP replies, reports and the
    # IR-link resume all lost until power-off; seen on the pedal 2026-11-24).
    # The main-loop poll now also takes any waiting bytes, with interrupts held
    # off for those few microseconds so it cannot race the handler on the ring.
    # beta.12c: not by masking only the UART line. The IR-link interrupt (lowest
    # priority, ~0.7 ms of FIR work) could preempt the drain while the UART line
    # was masked, so DSP replies overran the 4-byte FIFO (seen on the pedal with
    # a 512-tap IR last: UART errors and bad frames at 255, the editor never
    # synced). With interrupts off the IR work waits until after the drain, and
    # the UART line, which has the higher priority, can interrupt it as designed.
    old='    if (dsp_irq_off) drain();           /* only after the safety net in UART0_Interrupt tripped */'
    if text.count(old)!=1:raise ValueError('Pinned DSP link poll drain mismatch')
    text=text.replace(old,'    if (dsp_irq_off) drain();           /* only after the safety net in UART0_Interrupt tripped */\n'
                      '    else { int gie = GIE_STATE_GET(); GIE_DISABLE(); drain(); if (gie) GIE_ENABLE(); }   /* bytes left without an interrupt */')
    if dsp_usage:
        anchor='#include "dsp_link.h"'
        if text.count(anchor)!=1:raise ValueError('Pinned DSP report callback include mismatch')
        text=text.replace(anchor,anchor+'\n__attribute__((weak)) void fw_dsp_report(const uint8_t *p,unsigned n){(void)p;(void)n;}')
        anchor='    for (i = 0; i < dsp_unsol_len; i++) dsp_unsol[i] = pl[i];'
        if text.count(anchor)!=1:raise ValueError('Pinned DSP report callback mismatch')
        text=text.replace(anchor,anchor+'\n    fw_dsp_report(pl,n);')
    return text


def patch_flash(text):
    """31s: the pinned driver erases/programs only the 8 KB settings store.
    Frost also allows files run A, 0x13F000..0x15FFFF (OpenGP5 0.5 layout:
    after every app, unused by Valeton's firmware), for AMP model files
    (firmware/controller/fcm_files.h). Everything else stays refused."""
    old="""static int in_store(uint32_t addr, unsigned len)
{
    return addr >= GP5_FLASH_STORE && len <= GP5_FLASH_STORE_SIZE && addr - GP5_FLASH_STORE <= GP5_FLASH_STORE_SIZE - len;
}"""
    new="""static int in_store(uint32_t addr, unsigned len)
{
    return (addr >= GP5_FLASH_STORE && len <= GP5_FLASH_STORE_SIZE && addr - GP5_FLASH_STORE <= GP5_FLASH_STORE_SIZE - len) ||
           /* Frost: AMP model files, run A 0x13F000..0x15FFFF only */
           (addr >= 0x13F000u && len <= 0x21000u && addr - 0x13F000u <= 0x21000u - len);
}"""
    if text.count(old)!=1:raise ValueError('Pinned flash range check mismatch')
    return text.replace(old,new)

def patch_audio(text):
    # Capture timing without changing legacy timer start/end or DMA sequencing.
    if hashlib.sha256(text.encode()).hexdigest() != 'd03c9b8130d3ef639f5d52ff2a74ad953603e845a91c1b607783bce6a3144d9b':
        raise ValueError('Pinned DSP audio timing hooks mismatch')
    text=text.replace('#include "gp5_dsp.h"', '#include "timing_watch.h"\n#include "gp5_dsp.h"',1)
    text=text.replace('    REG(AIF + 0x00) = 0x8070741Bu;', '    REG(AIF + 0x00) = 0x8070741Bu;\n#ifdef GP5_FIRMWARE\n    frost_timing.last_end = REG(0xE0001004u); /* first-gap anchor, after enable */\n#endif')
    text=text.replace('#include "timing_watch.h"', '#include "timing_watch.h"\nstatic void fw_prepare(void);',1)
    text=text.replace('    fw_init();', '    fw_init();\n    fw_prepare(); /* Frost boot-only rehearsal, before DMA/audio enable */',1)
    text=text.replace('        fw_process(blk, bl, br);','        uint32_t frost_input_end = REG(0xE0001004u);\n        fw_process(blk, bl, br);\n        uint32_t frost_process_end = REG(0xE0001004u);')
    text=text.replace('        uint32_t dt = REG(0xE0001004u) - t0;','        uint32_t frost_end = REG(0xE0001004u);\n        uint32_t dt = frost_end - t0;')
    text=text.replace('        if (REG(ADMA + 0x04) & 0xFu) ST[31]++;', '        uint32_t frost_pending = REG(ADMA + 0x04) & 0xFu;\n        frost_timing_record(&frost_timing,t0,frost_input_end,frost_process_end,frost_end,frost_pending);\n        if (frost_pending) ST[31]++;')
    text=text.replace('static void fw_prepare(void);', 'static void fw_prepare(void);\n#ifdef FROST_LINE_TEST\nstatic void fw_output_block(const int32_t *,const int32_t *,volatile uint32_t *,volatile uint32_t *,uint32_t);\n#endif')
    begin='        for (int i = 0; i < 64; i++) {\n            outL[i] = (uint32_t)(int32_t)(((int64_t)bl[i] * g) >> 16);'
    a=text.index(begin);b=text.index('        uint32_t frost_end',a)
    text=text[:a]+'#ifdef FROST_LINE_TEST\n        fw_output_block(bl,br,outL,outR,(uint32_t)g);\n#else\n'+text[a:b]+'#endif\n'+text[b:]
    return text

def patch_ir_link(main,irq,app_ld):
    """Stage 4 (docs/Build%20Firmware.md): the controller half of the IR link, ported
    from OpenGP5 0.5.5 (MIT): SPI-slave DMA interrupt (line 13) to Frost's
    gp5_link_irq, and its loader hand-over rule: after the audio link has run,
    stop it, tell the DSP to go quiet, restart with a hand-over mark in the top
    32 bytes of RAM, and hand over to the factory loader first thing on that
    restart (upstream: the loader crashes otherwise once the host connects)."""
    old='extern void Timer2Interrupt(void);      /* led.c: LED brightness */\n'
    if irq.count(old)!=1:raise ValueError('Pinned IRQ handler list mismatch')
    irq=irq.replace(old,old+'extern void gp5_link_irq(void);         /* Frost IR link (ir_link.h): SPI slave exchange done (DMA channel 4) */\n')
    old='    [GP5_IRQ_TIMER2] = H(Timer2Interrupt),\n'
    if irq.count(old)!=1:raise ValueError('Pinned IRQ table mismatch')
    irq=irq.replace(old,old+'    [13] = H(gp5_link_irq),\n')
    old='    RAM (rwx) : ORIGIN = 0x20004000, LENGTH = 0x44000\n'
    if app_ld.count(old)!=1:raise ValueError('Pinned controller RAM layout mismatch')
    app_ld=app_ld.replace(old,'    RAM (rwx) : ORIGIN = 0x20004000, LENGTH = 0x44000 - 0x20   /* the top 32 bytes: the hand-over mark (main.c) */\n')
    old='#include "led.h"\n'
    if main.count(old)!=1:raise ValueError('Pinned controller include mismatch')
    main=main.replace(old,old+'void frost_irl_stop(void);                   /* Frost IR link (ir_link.h) */\nint frost_irl_running(void);\n'
        '/* The hand-over mark: the top 32 bytes of RAM, outside the app\'s RAM (start/app.ld). Survives a system reset. */\n'
        '#define HANDOVER_MARK  ((volatile uint32_t *)0x20047FE0u)\n#define HANDOVER_MAGIC 0x4C445231u                   /* "LDR1" */\n',1)
    old='''                put_line(4, "TO LOADER", yellow, black);
                delay_ms(100);                   /* as stock; the reply is already with the host */
                loader_handover();'''
    if main.count(old)!=1:raise ValueError('Pinned loader hand-over mismatch')
    main=main.replace(old,'''                put_line(4, "TO LOADER", yellow, black);
                if (frost_irl_running()) {
                    /* Frost IR link (OpenGP5 0.5.5 rule): the DSP stops its SPI exchanges (0x7F4E quiet, twice in
                     * case a busy DSP misses one), the link stops here, then a restart with the mark set hands
                     * over before anything else runs. */
                    static const uint8_t quiet[12] = { 0x4E, 0x7F, 12, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
                    uint8_t qrep[52], qst;
                    dsp_link_raw(quiet, sizeof quiet, qrep, sizeof qrep, 50, &qst);
                    delay_ms(5);
                    dsp_link_raw(quiet, sizeof quiet, qrep, sizeof qrep, 50, &qst);
                    delay_ms(100);
                    frost_irl_stop();
                    HANDOVER_MARK[0] = HANDOVER_MAGIC; HANDOVER_MARK[1] = ~HANDOVER_MAGIC;
                    __nds32__gie_dis();
                    *(volatile uint32_t *)0x40022008u = 1;   /* system reset (the vendor's Reset_McuSystem) */
                    for (;;) ;
                }
                delay_ms(100);                   /* as stock; the reply is already with the host */
                loader_handover();''')
    old='''    board_init();
    io_init();
    gp5_stage = 3;'''
    if main.count(old)!=1:raise ValueError('Pinned controller start-up mismatch')
    main=main.replace(old,'''    board_init();
    io_init();
    if (HANDOVER_MARK[0] == HANDOVER_MAGIC && HANDOVER_MARK[1] == ~HANDOVER_MAGIC) {
        HANDOVER_MARK[0] = 0; HANDOVER_MARK[1] = 0;      /* once only */
        loader_handover();                       /* the host asked for update mode before this restart */
    }
    gp5_stage = 3;''')
    return main,irq,app_ld
