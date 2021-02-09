#include <stdio.h>
#include <stdlib.h>

#include "m_pd.h"

#define BUFSIZE 1024 // to read from disk, in samples

static t_class *waveplayer_tilde_class;

typedef struct _waveplayer_tilde {
    t_object  x_obj;
    t_outlet *x_out;
    t_int x_loop_start;
    t_int x_loop_end;           
    double x_pos;               // position in file
    t_int x_current_buf;        // buffer currently being played
    uint8_t x_buf[BUFSIZE * 2]; // 2 bytes per sample
    FILE *x_fh;

} t_waveplayer_tilde;

// get buf from file 
static void readbuf(t_waveplayer_tilde *x){
    fseek(x->x_fh, x->x_current_buf * BUFSIZE * 2, SEEK_SET);
    fread(x->x_buf, 1, BUFSIZE * 2, x->x_fh);
}

t_int *waveplayer_tilde_perform(t_int *w)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)(w[1]);
    t_sample *out = (t_sample *)(w[2]);
    
    int n = (int)(w[3]);
    int i, bufnum, bufi;
    int16_t tmp = 0;
    
    for (i = 0; i<n; i++){
        
        //x->x_pos += 1;
        //if (x->x_pos > x->x_loop_end) x->x_pos = x->x_loop_start;
        x->x_pos -= 1;
        if (x->x_pos < x->x_loop_start) x->x_pos = x->x_loop_end;
        
        bufnum = (uint32_t)x->x_pos / BUFSIZE;
        bufi = (uint32_t)x->x_pos % BUFSIZE;

        if (bufnum != x->x_current_buf) {
            x->x_current_buf = bufnum;
            readbuf(x);
        }

        tmp = 0;
        tmp = x->x_buf[bufi * 2 + 1] << 8;
        tmp |= x->x_buf[bufi * 2];
        *out++ = (t_float)tmp / 32768;
    }
    return (w+4);
}

void waveplayer_tilde_dsp(t_waveplayer_tilde *x, t_signal **sp)
{
    dsp_add(waveplayer_tilde_perform, 3, x, sp[0]->s_vec, (t_int)sp[0]->s_n);
}

void waveplayer_tilde_free(t_waveplayer_tilde *x)
{
    outlet_free(x->x_out);
}

void *waveplayer_tilde_new(t_floatarg f)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)pd_new(waveplayer_tilde_class);

    x->x_out=outlet_new(&x->x_obj, &s_signal);

    x->x_loop_start = 1100;
    x->x_loop_end = 200000;
    //x->x_pos = x->x_loop_start;
    x->x_pos = x->x_loop_end;
    x->x_current_buf = -1;

    x->x_fh = fopen("test.wav","r");
    if( x->x_fh == NULL)
    {
        perror("Unable to read file\n");
        //return(1);
    }

    return (void *)x;
}

void waveplayer_tilde_setup(void) {
  waveplayer_tilde_class = class_new(gensym("waveplayer~"),
        (t_newmethod)waveplayer_tilde_new,
        0, sizeof(t_waveplayer_tilde),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);

  class_addmethod(waveplayer_tilde_class,
        (t_method)waveplayer_tilde_dsp, gensym("dsp"), A_CANT, 0);
}
