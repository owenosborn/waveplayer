#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "m_pd.h"

#define DISK_BUFSIZE 1024       // to read from disk, in samples
#define SHARED_BUFSIZE 1024     // shared between child and parent

static t_class *waveplayer_tilde_class;

typedef struct _waveplayer_tilde {
    t_object  x_obj;
    t_outlet *x_out;
    t_int x_loop_start;
    t_int x_loop_end;         
    t_float x_speed;
    char *x_filename;                       // name of file (with path)
    double x_pos;                           // sample position in file
    t_int x_current_buf_num;                // buffer position in file
    int16_t x_buf[DISK_BUFSIZE];            // from disk (assuming 16 bit wave format)
    int16_t x_buf_last3[3];                 // for stashing last 3 values of previous buffer, needed for 4 point interp
    t_float x_shared_buf[SHARED_BUFSIZE];   // child fills this and parent plays it
    t_int x_cindex;                         // the child index of shared buf
    t_int x_pindex;                         // parent index of shared buf
    t_int x_openfile;                       // flag to signal new open message
    t_int x_closefile;                      // flag to close 
    t_int x_exitchild;                      // flag to exit child thread when freeing 
    pthread_t x_childthread;
    pthread_mutex_t x_mutex;

    FILE *x_fh;                             // the sound file

} t_waveplayer_tilde;


// child thread does file I/O and varispeed playback
static void *waveplayer_child(void *zz) {
    t_waveplayer_tilde *x = zz;

    int i, bufnum, bufi;
    int16_t tmp = 0;
    char *fn;
    uint32_t file_length;
   
    pthread_mutex_lock(&x->x_mutex);
    
    while (1) {

        // wake up every 5 ms 
        // should be using pthread cond...
        pthread_mutex_unlock(&x->x_mutex);
        usleep(5000);
        pthread_mutex_lock(&x->x_mutex);

        // check flag for new open
        if (x->x_openfile) {
            x->x_openfile = 0;
                
            // zero bufs
            memset(x->x_buf, 0, sizeof(x->x_buf));
            memset(x->x_buf_last3, 0, sizeof(x->x_buf_last3));
            memset(x->x_shared_buf, 0, sizeof(x->x_shared_buf));

            fn = x->x_filename;

            pthread_mutex_unlock(&x->x_mutex);    // unlock during open
            if (x->x_fh != NULL) {
                fclose(x->x_fh); // close the file if one is open
                x->x_fh = NULL;
            }
            x->x_fh = fopen(fn,"r");
            if( x->x_fh == NULL) {
                // posting this message can cause issues in gui??
                // pd_error(x, "Unable to open file %s", fn);
                file_length = 44100;              // just a dummy length cause we need something
            } 
            else {
                fseek(x->x_fh, 0, SEEK_END);
                file_length = ftell(x->x_fh);
                //printf("loaded file len: %d samples\n", (file_length - 44) / 2);
                //printf("that is: %f secs\n", (float)(file_length - 44) / 2 / 44100);
            }
            pthread_mutex_lock(&x->x_mutex);

            x->x_loop_end = (file_length / 2);
            x->x_loop_start = 44;           // skip wave header
            x->x_pos = x->x_loop_start + 1;
            x->x_current_buf_num = -1;     // force read 
            x->x_pindex = 0;
            x->x_cindex = 0;
        }
        
        // check flag for close
        if (x->x_closefile) {
            x->x_closefile = 0;
            
            // zero bufs
            memset(x->x_buf, 0, sizeof(x->x_buf));
            memset(x->x_buf_last3, 0, sizeof(x->x_buf_last3));
            memset(x->x_shared_buf, 0, sizeof(x->x_shared_buf));

            pthread_mutex_unlock(&x->x_mutex); 
            if (x->x_fh != NULL) {
                fclose(x->x_fh); 
                x->x_fh = NULL;
            }
            pthread_mutex_lock(&x->x_mutex);
        }

        while (x->x_cindex != x->x_pindex) {        
            x->x_pos += x->x_speed;

            // check loop, include padding for interpolation window
            // go to loop end for reverse play
            if (x->x_speed >= 0) {
                if (x->x_pos >= (x->x_loop_end - 2)) x->x_pos = x->x_loop_start + 1;
            }
            else {
                if (x->x_pos <= (x->x_loop_start + 1)) x->x_pos = x->x_loop_end - 2;
            }

            // bufnum is 2 samples ahead pos playing forward, or 1 sample behind in reverse
            if (x->x_speed >= 0) bufnum = ((uint32_t)x->x_pos + 2) / DISK_BUFSIZE;
            else bufnum = ((uint32_t)x->x_pos - 1) / DISK_BUFSIZE;
            bufi = (uint32_t)x->x_pos % DISK_BUFSIZE;

            // check if we need new buf
            if (bufnum != x->x_current_buf_num) {
                x->x_current_buf_num = bufnum;
                        
                // stash last 3 (or first 3 for reverse play)
                if (x->x_speed >= 0) {
                    x->x_buf_last3[0] = x->x_buf[DISK_BUFSIZE - 3];
                    x->x_buf_last3[1] = x->x_buf[DISK_BUFSIZE - 2];
                    x->x_buf_last3[2] = x->x_buf[DISK_BUFSIZE - 1];
                } 
                else {
                    x->x_buf_last3[0] = x->x_buf[0];
                    x->x_buf_last3[1] = x->x_buf[1];
                    x->x_buf_last3[2] = x->x_buf[2];
                }

                if (x->x_fh != NULL) {
                    pthread_mutex_unlock(&x->x_mutex);    // unlock during read
                    fseek(x->x_fh, x->x_current_buf_num * DISK_BUFSIZE * 2, SEEK_SET);
                    fread(x->x_buf, 2, DISK_BUFSIZE, x->x_fh);
                    pthread_mutex_lock(&x->x_mutex);    
                }
                else {
                    // fill with 0 if no file
                    memset(x->x_buf, 0, sizeof(x->x_buf));
                    memset(x->x_buf_last3, 0, sizeof(x->x_buf_last3));
                }
            }

            t_sample frac,  a,  b,  c,  d, cminusb;
            frac = x->x_pos - (uint32_t)x->x_pos;
            
            // get the 4 values for interpolation
            // fwd
            if (x->x_speed >= 0) {
                if (bufi == DISK_BUFSIZE - 2) {
                    a = x->x_buf_last3[0];
                    b = x->x_buf_last3[1];
                    c = x->x_buf_last3[2];
                    d = x->x_buf[0];
                }
                else if (bufi == DISK_BUFSIZE - 1) {
                    a = x->x_buf_last3[1];
                    b = x->x_buf_last3[2];
                    c = x->x_buf[0];
                    d = x->x_buf[1];
                }
                else if (bufi == 0) {
                    a = x->x_buf_last3[2];
                    b = x->x_buf[0];
                    c = x->x_buf[1];
                    d = x->x_buf[2];
                }
                else {
                    a = x->x_buf[bufi-1];
                    b = x->x_buf[bufi];
                    c = x->x_buf[bufi+1];
                    d = x->x_buf[bufi+2];
                }
            }
            // reverse
            else {
                if (bufi == 0) {
                    a = x->x_buf[DISK_BUFSIZE - 1];
                    b = x->x_buf_last3[0];
                    c = x->x_buf_last3[1];
                    d = x->x_buf_last3[2];
                }
                else if (bufi == DISK_BUFSIZE - 1) {
                    a = x->x_buf[DISK_BUFSIZE - 2];
                    b = x->x_buf[DISK_BUFSIZE - 1];
                    c = x->x_buf_last3[0];
                    d = x->x_buf_last3[1];
                }
                else if (bufi == DISK_BUFSIZE - 2) {
                    a = x->x_buf[DISK_BUFSIZE - 3];
                    b = x->x_buf[DISK_BUFSIZE - 2];
                    c = x->x_buf[DISK_BUFSIZE - 1];
                    d = x->x_buf_last3[0];
                }
                else {
                    a = x->x_buf[bufi-1];
                    b = x->x_buf[bufi];
                    c = x->x_buf[bufi+1];
                    d = x->x_buf[bufi+2];
                }
            }
            
            a /= 32768;
            b /= 32768;
            c /= 32768;
            d /= 32768;

            // 4 point polynomial interpolation from tabread4~ 
            cminusb = c-b;
            x->x_shared_buf[x->x_cindex] = b + frac * (
                cminusb - 0.1666667f * (1.-frac) * (
                    (d - a - 3.0f * cminusb) * frac + (d + 2.0f*a - 3.0f*b)
                )
            );
            x->x_cindex++;
            x->x_cindex %= SHARED_BUFSIZE;
        }
        
        // check for exit
        if (x->x_exitchild) {
           break;
        }
    }
    pthread_mutex_unlock(&x->x_mutex);
    // clean up
    if (x->x_fh != NULL) {
        fclose(x->x_fh); 
        x->x_fh = NULL;
    }
    //puts("exit waveplayer io thread");
    return 0;
}

static t_int *waveplayer_tilde_perform(t_int *w)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)(w[1]);
    t_sample *in = (t_sample *)(w[2]);
    t_sample *out = (t_sample *)(w[3]);
    
    int n = (int)(w[4]);

    // copy a vec from the the shared buf
    pthread_mutex_lock(&x->x_mutex);

    // grab the first speed value in vec
    x->x_speed = *in;
    if (x->x_speed > 4) x->x_speed = 4;
    if (x->x_speed < -4) x->x_speed = -4;

    while (n--) {
        *out++ = x->x_shared_buf[x->x_pindex];
        x->x_pindex++;
        x->x_pindex %= SHARED_BUFSIZE;    
    }
    pthread_mutex_unlock(&x->x_mutex);
    return (w+5);
}

static void waveplayer_tilde_dsp(t_waveplayer_tilde *x, t_signal **sp)
{
    //dsp_add(waveplayer_tilde_perform, 3, x, sp[0]->s_vec, (t_int)sp[0]->s_n);
    dsp_add(waveplayer_tilde_perform, 4, x, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}

static void waveplayer_tilde_free(t_waveplayer_tilde *x)
{
    void *threadrtn;
    outlet_free(x->x_out);
    
    // clean up thread
    pthread_mutex_lock(&x->x_mutex);
    x->x_exitchild = 1;
    pthread_mutex_unlock(&x->x_mutex);

    // wait until it finish
    pthread_join(x->x_childthread, &threadrtn);
    pthread_mutex_destroy(&x->x_mutex);
}

static void waveplayer_set_speed(t_waveplayer_tilde *x, t_floatarg f){
    if (f > 4) f = 4;
    if (f < -4) f = -4;

    pthread_mutex_lock(&x->x_mutex);
    x->x_speed = f;
    pthread_mutex_unlock(&x->x_mutex);
}

static void waveplayer_open(t_waveplayer_tilde *x, t_symbol *s, int argc, t_atom *argv){
    t_symbol *filesym = atom_getsymbolarg(0, argc, argv);
    
    pthread_mutex_lock(&x->x_mutex);
    x->x_filename = filesym->s_name;
    x->x_openfile = 1;
    pthread_mutex_unlock(&x->x_mutex);
}

static void waveplayer_close(t_waveplayer_tilde *x, t_symbol *s, int argc, t_atom *argv){
    pthread_mutex_lock(&x->x_mutex);
    x->x_closefile = 1;
    pthread_mutex_unlock(&x->x_mutex);
}

static void *waveplayer_tilde_new(t_floatarg f)
{
    t_waveplayer_tilde *x = (t_waveplayer_tilde *)pd_new(waveplayer_tilde_class);

    x->x_out=outlet_new(&x->x_obj, &s_signal);

    x->x_loop_start = 44;   // 44 wave header
    x->x_loop_end = 44100;
    x->x_pos = x->x_loop_start + 1;
    x->x_current_buf_num = -1; // force read 
    x->x_speed = 1;
    x->x_pindex = 0;
    x->x_cindex = 0;
    x->x_openfile = 0;
    x->x_closefile = 0;
    x->x_exitchild = 0;
    x->x_fh = NULL;

    // zero bufs
    memset(x->x_buf, 0, sizeof(x->x_buf));
    memset(x->x_buf_last3, 0, sizeof(x->x_buf_last3));
    memset(x->x_shared_buf, 0, sizeof(x->x_shared_buf));
    
    // start child
    pthread_mutex_init(&x->x_mutex, 0);
    pthread_create(&x->x_childthread, 0, waveplayer_child, x);

    return (void *)x;
}

void waveplayer_tilde_setup(void) {
    waveplayer_tilde_class = class_new(gensym("waveplayer~"),
        (t_newmethod)waveplayer_tilde_new,
        (t_method)waveplayer_tilde_free, sizeof(t_waveplayer_tilde),
        CLASS_DEFAULT,
        A_GIMME, 0);
    
    CLASS_MAINSIGNALIN(waveplayer_tilde_class, t_waveplayer_tilde, x_speed);
    class_addfloat(waveplayer_tilde_class, (t_method)waveplayer_set_speed);
    class_addmethod(waveplayer_tilde_class, (t_method)waveplayer_open, gensym("open"), A_GIMME, 0);
    class_addmethod(waveplayer_tilde_class, (t_method)waveplayer_close, gensym("close"), A_GIMME, 0);
    class_addmethod(waveplayer_tilde_class, (t_method)waveplayer_tilde_dsp, gensym("dsp"), A_CANT, 0);
}
