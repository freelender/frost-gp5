#include <stdint.h>
#include "gp5_firmware.h"
static uint32_t test_time=1000;
static uint16_t displayed[160*80];
static unsigned blits,bad_blit,text_calls;
uint32_t gp5_ms(void){return test_time;}
void gp5_text(unsigned line,const char *s){(void)line;(void)s;++text_calls;}
void gp5_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *p)
{
    unsigned row,col;
    if(x+w>160 || y+h>80 || !w || !h){bad_blit=1;return;}
    for(row=0;row<h;++row)for(col=0;col<w;++col)displayed[(y+row)*160+x+col]=p[row*w+col];
    ++blits;
}
#include "../firmware/controller/firmware.c"
#if defined(_WIN32)
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void ui_init(void){unsigned i;test_time=1000;blits=bad_blit=text_calls=0;for(i=0;i<12800;++i)displayed[i]=0;fw_init();}
API void ui_turn(int d){fw_para_turn(d);}
API void ui_press(void){test_time+=50;fw_para_push();}
API void ui_foot(void){test_time+=50;fw_foot();}
API void ui_bounce(void){fw_foot();}
API void ui_advance(unsigned milliseconds){test_time+=milliseconds;}
API int ui_once(void){return fw_draw(0,0);}
API uint16_t ui_color(unsigned r,unsigned g,unsigned b){return COLOR(r,g,b);}
API unsigned ui_selected(void){return selected;}
API unsigned ui_view(void){return view;}
API unsigned ui_parameter(void){return parameter;}
API unsigned ui_enabled(unsigned i){return i<6?enabled[i]:0;}
API int ui_value(unsigned i,unsigned j){return i<6 && j<3?values[i][j]:0;}
API unsigned ui_paint(void){unsigned i=0;while(fw_draw(0,0)){if(++i>11)return 99;}return i;}
API uint16_t ui_pixel(unsigned x,unsigned y){return x<160 && y<80?displayed[y*160+x]:0;}
API unsigned ui_blits(void){return blits;}
API unsigned ui_bad(void){return bad_blit|text_calls;}
API unsigned ui_safe(void){uint32_t id=0,v=0;fw_knob(255);fw_dsp_ready();return fw_level()==0 && fw_setting_count()==0 && fw_take(&id,&v)==0;}

