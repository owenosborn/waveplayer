#include <stdio.h>
#include <stdlib.h>

#include "m_pd.h"

static t_class *fileplayer_tilde_class;

typedef struct _fileplayer_tilde {
    t_object  x_obj;
    t_outlet *x_out;
    t_int x_loop_start;
    t_int x_loop_end;
    t_int x_pos;
    uint8_t x_buf[1024];
    FILE *x_fh;

} t_fileplayer_tilde;

t_int *fileplayer_tilde_perform(t_int *w)
{
    t_fileplayer_tilde *x = (t_fileplayer_tilde *)(w[1]);
    t_sample *out = (t_sample *)(w[2]);
    int n = (int)(w[3]);

    x->x_pos -= n;
    if (x->x_pos < x->x_loop_start) x->x_pos = x->x_loop_end;
    fseek(x->x_fh, x->x_pos * 2, SEEK_SET);
    fread(x->x_buf,1,n*2,x->x_fh);

    int i = 0;
    int16_t tmp = 0;
    for (i = n-1; i>=0; i--){
        tmp = 0;
        tmp = x->x_buf[i * 2 + 1] << 8;
        tmp |= x->x_buf[i * 2];
        *out++ = (t_float)tmp / 65536;

    }
    return (w+4);
}

void fileplayer_tilde_dsp(t_fileplayer_tilde *x, t_signal **sp)
{
    dsp_add(fileplayer_tilde_perform, 3, x, sp[0]->s_vec, (t_int)sp[0]->s_n);
}

void fileplayer_tilde_free(t_fileplayer_tilde *x)
{
    outlet_free(x->x_out);
}

void *fileplayer_tilde_new(t_floatarg f)
{
    t_fileplayer_tilde *x = (t_fileplayer_tilde *)pd_new(fileplayer_tilde_class);

    x->x_out=outlet_new(&x->x_obj, &s_signal);

    x->x_loop_start = 1100;
    x->x_loop_end = 200000;
    x->x_pos = x->x_loop_end;

    x->x_fh = fopen("test.wav","r");
    if( x->x_fh == NULL)
    {
        perror("Unable to read file\n");
        //return(1);
    }

    return (void *)x;
}

void fileplayer_tilde_setup(void) {
  fileplayer_tilde_class = class_new(gensym("fileplayer~"),
        (t_newmethod)fileplayer_tilde_new,
        0, sizeof(t_fileplayer_tilde),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);

  class_addmethod(fileplayer_tilde_class,
        (t_method)fileplayer_tilde_dsp, gensym("dsp"), A_CANT, 0);
}
