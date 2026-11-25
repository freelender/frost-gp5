/* Host fixture for the stage 5 drop tune kernel (effects/drop_tune). */
#include "../effects/drop_tune/drop_tune.c"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static FrostModulation ring;static float jumps[16],scores[16];static FrostDropTune dt;static unsigned splices,max_d;
API void dt_init(void){dt.ring=&ring;frost_drop_reset(&dt);splices=0;max_d=0;}
API void dt_run(const float *x,float *y,unsigned n,float drop,float mix)
{
    float p[2]={drop,mix};unsigned i;
    for(i=0;i<n;++i){unsigned f=dt.head.fade;float d0=dt.head.d;y[i]=frost_drop_sample(&dt,x[i],p);if(!f && dt.head.fade){if(splices<16){jumps[splices]=d0-dt.head.d;scores[splices]=dt.head.last;}++splices;}if((unsigned)dt.head.d>max_d)max_d=(unsigned)dt.head.d;}
}
API unsigned dt_splices(void){return splices;}
API unsigned dt_max_distance(void){return max_d;}
API float dt_distance(void){return dt.head.d;}
API unsigned dt_state_bytes(void){return sizeof(FrostDropTune);}
API float dt_jump(unsigned i){return i<16?jumps[i]:0;}
API float dt_score(unsigned i){return i<16?scores[i]:0;}
