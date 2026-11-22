#include <stdint.h>
#include "../firmware/controller/dsp_usage.c"
#define FROST_DSP_USAGE 1
#define FROST_SLOT_EDITOR 1
#define FROST_LINE_TEST 1
static uint32_t usage_now;
static uint16_t usage_pixels[160*80];
static unsigned usage_blits,usage_bad_blit,usage_last_y,usage_max_y;
uint32_t gp5_ms(void){return usage_now;}
void gp5_text(unsigned row,const char *s){(void)row;(void)s;}
void gp5_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *p)
{
    unsigned i,j;++usage_blits;usage_last_y=y;if(y+h>usage_max_y)usage_max_y=y+h;
    if(x+w>160 || y+h>80){usage_bad_blit=1;return;}
    for(j=0;j<h;++j)for(i=0;i<w;++i)usage_pixels[(y+j)*160+x+i]=p[j*w+i];
}
/* AMP model files (fcm_files.h): an erased, read-only run A for this fixture. */
static uint8_t usage_files[33u*4096u];
#define FROST_FILE_AT(a) ((const volatile uint8_t *)(usage_files+((a)-0x13f000u)))
int gp5_flash_erase(uint32_t a){(void)a;return -1;}
int gp5_flash_program(uint32_t a,const uint32_t *s,unsigned n){(void)a;(void)s;(void)n;return -1;}
void gp5_uart0_rx_err_clear(void){}
void gp5_uart0_rx_int_clear(void){}
static unsigned host_backlight=100;void gp5_backlight_duty(unsigned percent){host_backlight=percent;}
#include "../firmware/controller/firmware.c"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API
#endif
static FrostDSPUsage meter;
API void du_init(uint32_t now){frost_dsp_usage_init(&meter,now);}
API void du_report(const uint8_t *p,unsigned n,uint32_t now){frost_dsp_usage_report(&meter,p,n,now);}
API unsigned du_tick(uint32_t now){return frost_dsp_usage_tick(&meter,now);}
API const char *du_text(void){static char text[12];frost_dsp_usage_text(&meter,text);return text;}
API void du_ui_init(void)
{
    {unsigned k;for(k=0;k<sizeof(usage_files);++k)usage_files[k]=0xff;}
    usage_now=0;usage_blits=usage_bad_blit=usage_last_y=usage_max_y=0;
    fw_init();editor.ready=1;editor.revision=1;
    {const unsigned types[6]={14,1,22,3,5,8};unsigned i;
     for(i=0;i<6;++i){editor.types[i]=types[i];editor.order[i]=i;editor.desired_enabled[i]=1;}}
}
API void du_ui_report(const uint8_t *p,unsigned n,uint32_t now){usage_now=now;fw_dsp_report(p,n);}
API void du_ui_draw(uint32_t now){usage_now=now;while(fw_draw(0,0)){};}
API void du_ui_reset_blits(void){usage_blits=usage_max_y=0;}
API unsigned du_ui_blits(void){return usage_blits;}
API unsigned du_ui_max_y(void){return usage_max_y;}
API unsigned du_ui_bad_blit(void){return usage_bad_blit;}
API const uint16_t *du_ui_pixels(void){return usage_pixels;}
API unsigned du_ui_enabled(unsigned slot){return editor.desired_enabled[slot];}
API void du_ui_foot(unsigned slot,uint32_t now){selected=slot;usage_now=now;fw_foot();}
