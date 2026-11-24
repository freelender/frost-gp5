"""beta.12: the controller's DSP link must not stall after a flash write.

The receive interrupt of the BP1048 UART0 fires for the first byte into an
empty 4-byte FIFO. A flash write stalls the controller while the DSP keeps
sending, so bytes wait in a full FIFO; the post-write interrupt clear then
removed the only trigger and nothing drained the FIFO again (seen on the
pedal: DSP replies, reports and the IR-link resume lost until power-off).
This compiles the pinned dsp_link.c, original and patched, against a FIFO
model of that receiver. Needs a fetched platform checkout (like
test_platform_compat); skips otherwise."""
import ctypes as ct
import os
from pathlib import Path
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools')]
from host import build
from platform_compat import patch_dsp_link

EXPORT = '__declspec(dllexport)' if os.name == 'nt' else '__attribute__((visibility("default")))'
HAL = r"""
#include <stdint.h>
/* UART0 receiver model: 4-byte FIFO; the interrupt flag rises when a byte
 * enters an EMPTY FIFO; a byte into a full FIFO is lost and sets an error. */
static uint8_t fifo[4];static unsigned fifo_n,int_flag,err_flag,irq_enabled=1,irq_masked;
static uint32_t ms;
/* Global interrupt enable, and an IR-link interrupt (lowest priority, ~0.7 ms of FIR work)
 * that preempts the main loop at its next receiver access if interrupts are on. */
static int gie=1;static uint8_t pre_bytes[64];static unsigned pre_n;
static void run_preempt(void);
uint32_t gp5_ms(void){return ms;}
void gp5_gpio_uart0_pins(void){}
int gp5_uart0_init(uint32_t b,uint32_t c){(void)b;(void)c;return 1;}
void gp5_uart0_rx_fifo_clear(void){fifo_n=0;}
void gp5_uart0_rx_int_from_first_byte(void){}
void gp5_uart0_rx_int_enable(int on){irq_enabled=(unsigned)on;}
uint32_t gp5_uart0_rx_status(void){return int_flag|(err_flag?4u:0u);}
void gp5_uart0_rx_int_clear(void){int_flag=0;}
void gp5_uart0_rx_err_clear(void){err_flag=0;}
int gp5_uart0_tx_int_pending(void){return 0;}
void gp5_uart0_tx_int_clear(void){}
int gp5_uart0_recv_byte(uint8_t *b){if(pre_n && gie)run_preempt();unsigned i;if(!fifo_n)return 0;*b=fifo[0];for(i=1;i<fifo_n;++i)fifo[i-1]=fifo[i];--fifo_n;return 1;}
uint32_t gp5_uart0_send(const uint8_t *b,uint32_t n,uint32_t t){(void)b;(void)t;return n;}
void NVIC_EnableIRQ(int n){(void)n;irq_masked=0;}
void GIE_DISABLE(void){gie=0;}
int GIE_STATE_GET(void){return gie;}
void GIE_ENABLE(void){gie=1;if(pre_n)run_preempt();}
void NVIC_DisableIRQ(int n){(void)n;irq_masked=1;}
void NVIC_SetPriority(int n,int p){(void)n;(void)p;}
void UART0_Interrupt(void);
/* One byte on the wire; `stalled`: the controller cannot take interrupts (flash write). */
EXPORT void wire(uint8_t b,int stalled)
{
    if(fifo_n==4u){err_flag=1;return;}
    if(!fifo_n)int_flag=1;
    fifo[fifo_n++]=b;
    if(!stalled && irq_enabled && !irq_masked && (int_flag || err_flag))UART0_Interrupt();
}
/* After a flash write: what fcm_files.h / preset_store.h do, then the pending interrupt if any. */
EXPORT void after_flash(void){gp5_uart0_rx_err_clear();gp5_uart0_rx_int_clear();if(irq_enabled && (int_flag || err_flag))UART0_Interrupt();}
EXPORT unsigned fifo_count(void){return fifo_n;}
/* While the IR interrupt runs, interrupts nest (the UART line has the higher priority) but a
 * masked UART line stays masked: these bytes then reach a 4-byte FIFO with nobody reading. */
static void run_preempt(void){uint8_t b[64];unsigned i,n=pre_n;for(i=0;i<n;++i)b[i]=pre_bytes[i];pre_n=0;for(i=0;i<n;++i)wire(b[i],0);}
EXPORT void preempt_next(const uint8_t *b,unsigned n){unsigned i;for(i=0;i<n && i<64u;++i)pre_bytes[i]=b[i];pre_n=n<64u?n:64u;}
"""
SHIM = r"""
#define GP5_IRQ_UART0 7
void NVIC_EnableIRQ(int n);void NVIC_DisableIRQ(int n);void NVIC_SetPriority(int n,int p);void gp5_gpio_uart0_pins(void);
void GIE_ENABLE(void);void GIE_DISABLE(void);int GIE_STATE_GET(void);
"""
TAIL = r"""
EXPORT void link_init(void){dsp_link_init();}
EXPORT void link_poll(void){dsp_link_poll();}
EXPORT unsigned link_frames(void){return dsp_rx_frames;}
EXPORT unsigned link_bytes(void){return dsp_rx_bytes;}
EXPORT unsigned link_errs(void){return dsp_rx_errs;}
"""


def platform():
    for p in (ROOT / '.deps/OpenGP5', ROOT / 'build/platform-ce8417-review'):
        if (p / 'platform/nds32/dsp_link.c').exists(): return p
    return None


def frame(payload):
    c = 0
    for x in payload:
        c ^= x
        for _ in range(8): c = ((c << 1) ^ 7) & 255 if c & 128 else (c << 1) & 255
    n = len(payload) + 4
    return bytes((c, n & 255, n >> 8, 1)) + payload


@unittest.skipIf(platform() is None, 'fetch locked OpenGP5 source to run platform integration checks')
class DspLinkDrainTests(unittest.TestCase):
    def lib(self, patched):
        p = platform() / 'platform/nds32'
        src = (p / 'dsp_link.c').read_text()
        if patched: src = patch_dsp_link(src).replace('#include "paced_uart.inc"', 'gp5_uart0_send(tx, len, 100);')
        for inc in ('#include "hal/gpio.h"', '#include "hal/tick.h"', '#include "start/irq.h"', '#include "hal/uart0.h"'):
            src = src.replace(inc, '')
        src = src.replace('#include "dsp_link.h"', '#include "dsp_link.h"\n' + SHIM.replace('EXPORT', EXPORT) +
                          '#include "hal_uart0_decl.h"')
        folder = Path(tempfile.mkdtemp(dir=ROOT / 'build'))
        (folder / 'dsp_link.h').write_text((p / 'dsp_link.h').read_text())
        (folder / 'hal_uart0_decl.h').write_text((p / 'hal/uart0.h').read_text())
        (folder / 'link.c').write_text(src + TAIL.replace('EXPORT', EXPORT))
        (folder / 'hal.c').write_text(HAL.replace('EXPORT', EXPORT))
        name = 'dsp_link_%s_%s' % ('patched' if patched else 'original', folder.name)   # one DLL per build (Windows locks loaded DLLs)
        c = ct.CDLL(str(build(ROOT / ('build/' + name + ('.dll' if os.name == 'nt' else '.so')), folder / 'link.c', includes=[folder], flags=[str(folder / 'hal.c')])))
        c.wire.argtypes = [ct.c_uint8, ct.c_int]; c.preempt_next.argtypes = [ct.c_char_p, ct.c_uint]
        c.link_init(); return c

    def run_case(self, c):
        report = frame(bytes.fromhex('01401800') + b'FRX1' + bytes(16))
        for b in report: c.wire(b, 0)
        c.link_poll(); before = c.link_frames()
        for b in report[:9]: c.wire(b, 1)        # a report arrives during a flash write
        c.after_flash()
        for b in report[9:] + report * 3: c.wire(b, 0); c.link_poll()   # the main loop polls all the time
        return before, c.link_frames(), c.fifo_count()

    def test_original_receiver_stalls_after_a_flash_write(self):
        before, after, waiting = self.run_case(self.lib(False))
        self.assertEqual(before, 1); self.assertEqual(after, 1); self.assertEqual(waiting, 4)   # stuck: nothing more received

    def test_patched_poll_drains_and_the_link_recovers(self):
        before, after, waiting = self.run_case(self.lib(True))
        self.assertEqual(before, 1); self.assertGreaterEqual(after, 3); self.assertEqual(waiting, 0)

    def test_ir_interrupt_during_the_poll_drain_loses_no_reply_bytes(self):
        # beta.12c on the pedal: a 512-tap IR last in the chain. The IR-link interrupt
        # fired while the main-loop drain had the UART line masked, so the DSP's reply
        # overran the FIFO (UART errors and bad frames at 255, the editor never synced).
        c = self.lib(True)
        reply = frame(bytes.fromhex('41 7f 2c 00') + bytes(40))
        c.link_poll(); c.preempt_next(reply, len(reply))
        for _ in range(4): c.link_poll()        # the reply arrives while the IR interrupt runs
        self.assertEqual(c.link_errs(), 0); self.assertEqual(c.link_frames(), 1); self.assertEqual(c.fifo_count(), 0)


if __name__ == '__main__':
    unittest.main()
