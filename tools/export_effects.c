#define _CRT_SECURE_NO_WARNINGS 1
/* Writes web/effects.json from the firmware's own control catalog (the same
 * names, ranges, steps and labels the pedal screen uses). Built and run by
 * tools/export_effects.py. */
#include <stdio.h>
#include "../engine/control_catalog.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
static void str(FILE *f,const char *s){fputc('"',f);for(;s && *s;++s){if(*s=='"'||*s=='\\')fputc('\\',f);fputc(*s,f);}fputc('"',f);}
API int export_effects(const char *path)
{
    FILE *f=fopen(path,"w");unsigned t,p,k,c;
    if(!f)return 0;
    fprintf(f,"{\n \"categories\": [");
    for(c=0;c<FROST_CATEGORIES;++c){if(c)fputc(',',f);str(f,frost_category_names[c]);}
    fprintf(f,"],\n \"colors\": [");
    for(c=0;c<FROST_CATEGORIES;++c)fprintf(f,"%s\"#%06lX\"",c?",":"",frost_category_rgb[c]);
    fprintf(f,"],\n \"picker\": [");
    for(c=0;c<FROST_CATEGORIES;++c){
        fprintf(f,"%s[",c?",":"");
        for(k=frost_picker_start[c];k<frost_picker_start[c+1];++k)fprintf(f,"%s%u",k>frost_picker_start[c]?",":"",(unsigned)frost_picker_types[k]);
        fputc(']',f);
    }
    fprintf(f,"],\n \"effects\": {\n");
    for(t=0;t<FROST_EFFECTS;++t){
        const FrostControlEffect *e=&frost_control_effects[t];
        fprintf(f,"  \"%u\": {\"short\": ",t);str(f,e->short_name);fprintf(f,", \"name\": ");str(f,e->name);
        fprintf(f,", \"category\": %u, \"params\": [",frost_type_category(t));
        for(p=0;p<e->count;++p){
            const FrostControlParameter *q=&e->p[p];
            fprintf(f,"%s{\"name\": ",p?", ":"");str(f,q->name);fprintf(f,", \"unit\": ");str(f,q->unit);
            fprintf(f,", \"min\": %d, \"max\": %d, \"step\": %d, \"initial\": %d, \"scale\": %d",q->minimum,q->maximum,q->step,q->initial,q->scale);
            if(q->labels){fprintf(f,", \"labels\": [");for(k=0;k<=(unsigned)(q->maximum-q->minimum);++k){if(k)fputc(',',f);str(f,q->labels[k]);}fputc(']',f);}
            fputc('}',f);
        }
        fprintf(f,"]}%s\n",t+1<FROST_EFFECTS?",":"");
    }
    fprintf(f," }\n}\n");fclose(f);return 1;
}
