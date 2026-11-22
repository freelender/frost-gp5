/* Shared bounded RGB565 drawing and value formatting for both editor profiles. */
static unsigned length(const char *s){unsigned n=0;while(s[n])++n;return n;}
static void pixel(int x,int y,uint16_t color)
{ if(x>=0 && x<160 && y>=0 && y<80)framebuffer[y*160+x]=color; }
static void rect(int x,int y,int width,int height,uint16_t color)
{ int i,j;for(j=0;j<height;++j)for(i=0;i<width;++i)pixel(x+i,y+j,color); }
static void line(int x,int y,int end_x,int end_y,uint16_t color)
{
    int dx=end_x-x,dy=end_y-y,step_x=dx<0?-1:1,step_y=dy<0?-1:1;
    int error,e2;if(dx<0)dx=-dx;if(dy<0)dy=-dy;error=dx-dy;
    for(;;){pixel(x,y,color);if(x==end_x && y==end_y)break;e2=2*error;
        if(e2>-dy){error-=dy;x+=step_x;}if(e2<dx){error+=dx;y+=step_y;}}
}
static void arrow(int x,int y,int end_x,int end_y)
{
    line(x,y,end_x,end_y,WIRE);
    if(y==end_y){int d=end_x>x?1:-1;line(end_x,end_y,end_x-3*d,end_y-2,WIRE);line(end_x,end_y,end_x-3*d,end_y+2,WIRE);}
    else {line(end_x,end_y,end_x-2,end_y-3,WIRE);line(end_x,end_y,end_x+2,end_y-3,WIRE);}
}
static void text(int x,int y,const char *s,uint16_t color)
{
    for(;*s;++s,x+=6){unsigned c=(unsigned char)*s,col,row;if(c<32 || c>90)continue;
        for(col=0;col<5;++col)for(row=0;row<7;++row)if(frost_font[c-32][col]&(1u<<row))pixel(x+(int)col,y+(int)row,color);}
}
static char *number(char *p,unsigned n)
{ char reversed[12];unsigned count=0;do{reversed[count++]=(char)('0'+n%10);n/=10;}while(n);while(count)*p++=reversed[--count];return p; }
static void formatted(char *out,const Parameter *p,int value)
{
    char *s=out;unsigned n=value<0?(unsigned)-value:(unsigned)value;
    if(p->labels && value>=p->minimum && value<=p->maximum){
        const char *label=p->labels[value-p->minimum];while(*label)*s++=*label++;*s=0;return;
    }
    if(value<0)*s++='-';s=number(s,n/(unsigned)p->scale);
    if(p->scale>1){unsigned divisor=(unsigned)p->scale/10;*s++='.';while(divisor){*s++=(char)('0'+(n/divisor)%10);divisor/=10;}}
    {const char *unit=p->unit;while(*unit)*s++=*unit++;}*s=0;
}
