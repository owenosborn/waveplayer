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
    t_float x_speed;
    double x_pos;               // position in file
    t_int x_current_buf;        // buffer currently being played, pos / bufsize
    //uint8_t x_buf[BUFSIZE * 2]; // 2 bytes per sample
    int16_t x_buf[BUFSIZE]; // 2 bytes per sample
    int16_t x_last3[3];         // for stashing last 3 values of previous buffer needed for 4 point interp
    FILE *x_fh;

} t_waveplayer_tilde;

// get buf from file 
static void readbuf(t_waveplayer_tilde *x){

    // stash last 3
    x->x_last3[0] = x->x_buf[BUFSIZE - 3];
    x->x_last3[1] = x->x_buf[BUFSIZE - 2];
    x->x_last3[2] = x->x_buf[BUFSIZE - 1];

    fseek(x->x_fh, x->x_current_buf * BUFSIZE * 2, SEEK_SET);
    fread(x->x_buf, 2, BUFSIZE, x->x_fh);
}

t_int *waveplayer_tilde_perform(t_int *w)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)(w[1]);
    t_sample *out = (t_sample *)(w[2]);
    
    int n = (int)(w[3]);
    int i, bufnum, bufi;
    int16_t tmp = 0;
    
    for (i = 0; i<n; i++){
        
        x->x_pos += x->x_speed;

        // check loop, padding for interpolation window 
        if (x->x_pos > (x->x_loop_end - 2)) x->x_pos = x->x_loop_start + 1;

        // bufnum is 2 samples ahead of pos cause we need x+2 for interp
        bufnum = ((uint32_t)x->x_pos + 2) / BUFSIZE;
        bufi = (uint32_t)x->x_pos % BUFSIZE;

        // check if we need new buf
        if (bufnum != x->x_current_buf) {
            x->x_current_buf = bufnum;
            readbuf(x);
        }

        t_sample frac,  a,  b,  c,  d, cminusb;
        frac = x->x_pos - (uint32_t)x->x_pos;
        
        // get the 4 values for interp
        if (bufi == BUFSIZE - 2) {
            a = (t_sample)x->x_last3[0] / 32768;
            b = (t_sample)x->x_last3[1] / 32768;
            c = (t_sample)x->x_last3[2] / 32768;
            d = (t_sample)x->x_buf[0] / 32768;
        }
        else if (bufi == BUFSIZE - 1) {
            a = (t_sample)x->x_last3[1] / 32768;
            b = (t_sample)x->x_last3[2] / 32768;
            c = (t_sample)x->x_buf[0] / 32768;
            d = (t_sample)x->x_buf[1] / 32768;
        }
        else if (bufi == 0) {
            a = (t_sample)x->x_last3[2] / 32768;
            b = (t_sample)x->x_buf[0] / 32768;
            c = (t_sample)x->x_buf[1] / 32768;
            d = (t_sample)x->x_buf[2] / 32768;
        }
        else {
            a = (t_sample)x->x_buf[bufi-1] / 32768;
            b = (t_sample)x->x_buf[bufi] / 32768;
            c = (t_sample)x->x_buf[bufi+1] / 32768;
            d = (t_sample)x->x_buf[bufi+2] / 32768;
        }
        
//        *out++ = d;
        // 4 point polynomial interpolation from tabread4~ 
        cminusb = c-b;
        *out++ = b + frac * (
            cminusb - 0.1666667f * (1.-frac) * (
                (d - a - 3.0f * cminusb) * frac + (d + 2.0f*a - 3.0f*b)
            )
        );
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

static void waveplayer_set_speed(t_waveplayer_tilde *x, t_float f){
    if (f > 3) f = 3;
    if (f < 0) f = 0;
    x->x_speed = f;
}

void *waveplayer_tilde_new(t_floatarg f)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)pd_new(waveplayer_tilde_class);

    x->x_out=outlet_new(&x->x_obj, &s_signal);

    x->x_loop_start = 1100;
    x->x_loop_end = 300000;
    x->x_pos = x->x_loop_start + 1;
    x->x_current_buf = -1;
    x->x_speed = 1;
    x->x_last3[0] = 0;
    x->x_last3[1] = 0;
    x->x_last3[2] = 0;


    x->x_fh = fopen("test.wav","r");
    if( x->x_fh == NULL)
    {
    	pd_error(x, "Unable to open file");
    }

    return (void *)x;
}

void waveplayer_tilde_setup(void) {
    waveplayer_tilde_class = class_new(gensym("waveplayer~"),
        (t_newmethod)waveplayer_tilde_new,
        0, sizeof(t_waveplayer_tilde),
        CLASS_DEFAULT,
        A_DEFFLOAT, 0);
    
    class_addfloat(waveplayer_tilde_class, (t_method)waveplayer_set_speed);

    class_addmethod(waveplayer_tilde_class,
        (t_method)waveplayer_tilde_dsp, gensym("dsp"), A_CANT, 0);

}
