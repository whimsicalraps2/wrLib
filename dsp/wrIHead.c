#include "wrIHead.h"

#include <stdlib.h>
#include <stdio.h>

#include "wrInterpolate.h"
#include "wrBufMap.h" // TODO who should own this file?

// necessary to keep read & write interpolation regions separate
#define REC_OFFSET (-2) // write head trails read head


//////////////////////////////
// private declarations
static float* push_input( ihead_t* self, float* buf, float in );
static int write( ihead_t* self, float rate, float input );


/////////////////////////////////
// setup

ihead_t* ihead_init( Buf_Map_Type_t map_type )
{
    ihead_t* self = malloc( sizeof( ihead_t ) );
    if( !self ){ printf("ihead_init failed malloc\n"); return NULL; }

// WRITE / ERASE HEAD
    for( int i=0; i<4; i++ ){ self->in_buf[i] = 0.0; }
    self->in_buf_ix = 0;

    for( int i=0; i<OUT_BUF_LEN; i++ ){ self->out_buf[i] = 0.0; }
    self->out_phase = 0.0; // [0,1)

    self->write_ix = 0; // phase into the buffer_t

    ihead_recording( self, false );

    switch(map_type){
        case Buf_Map_Gain:   self->map = buf_map_gain_init(); break;
        case Buf_Map_Filter: self->map = buf_map_filter_init(); break;
//        default:             self->map = buf_map_none_init(); break;
        default: self->map = NULL; break; // force old mac_v handling
    }
    ihead_rec_level( self, 1.0 );
    ihead_pre_level( self, 0.0 );

// READ HEAD
    self->rphase = phase_new( self->write_ix - REC_OFFSET, 0.0 );

    return self;
}

void ihead_deinit( ihead_t* self )
{
    // TODO cleanup the buf_map_*()
    free(self); self = NULL;
}


/////////////////////////////////////////
// params: setters

void ihead_recording( ihead_t* self, bool is_recording ){
    self->recording = is_recording;
}
void ihead_rec_level( ihead_t* self, float level ){
    self->rec_level = level;
}
void ihead_pre_level( ihead_t* self, float level ){
    self->pre_level = level; // TODO deprecated. only used for query.
    //buf_map_gain_pre_level( self->map, level );
    if( self->map ){ // custom map
        switch( self->map->type ){
            case Buf_Map_Gain:
                buf_map_gain_pre_level( self->map, level );
                break;
            case Buf_Map_Filter:
                buf_map_filter_pre_level( self->map, level );
                break;
            default: break;
        }
    }
}
void ihead_pre_filter( ihead_t* self, float coeff ){
    if( self->map ){ // custom map
        switch( self->map->type ){
            case Buf_Map_Filter:
                buf_map_filter_coeff( self->map, coeff );
                break;
            default: break;
        }
    }
}
void ihead_pre_filter_set( ihead_t* self, float set ){
    if( self->map ){ // custom map
        switch( self->map->type ){
            case Buf_Map_Filter:
                buf_map_filter_set( self->map, set );
                break;
            default: break;
        }
    }
}
void ihead_jumpto( ihead_t* self, buffer_t* buf, int phase, bool is_forward ){
    self->write_ix = (is_forward) ? phase + REC_OFFSET : phase - REC_OFFSET;
    self->rphase = phase_new(phase, 0.0);
}
void ihead_align( ihead_t* self, bool is_forward ){
    self->write_ix = (is_forward) ? self->rphase.i + REC_OFFSET
                                  : self->rphase.i - REC_OFFSET;
}


//////////////////////////////////
// params: getters

bool ihead_is_recording( ihead_t* self ){ return self->recording; }
float ihead_get_rec_level( ihead_t* self ){ return self->rec_level; }
float ihead_get_pre_level( ihead_t* self ){ return self->pre_level; }
phase_t ihead_get_location( ihead_t* self ){ return self->rphase; }


///////////////////////////////////////
// signals

void ihead_poke( ihead_t* self, buffer_t* buf
                              , float     speed
                              , float     input )
{
    if( speed == 0.0 ){ return; }

    int nframes = write( self, speed, input * self->rec_level );
    if( nframes == 0 ){ return; } // nothing to do

    int nframes_dir = (speed >= 0.0) ? nframes : -nframes;
// TODO can factor both of these per block in poke_v
    if( self->recording ){
        if( self->map ){ // custom map
            buffer_map_v( buf
                        , self->out_buf
                        , self->write_ix
                        , nframes_dir
                        , self->map );
        } else { // no custom map map fn
            buffer_mac_v( buf
                        , self->out_buf // the data to be written to the buffer_t
                        , self->write_ix
                        , nframes_dir
                        , self->pre_level );
        }
    }
    self->write_ix += nframes_dir;
}

void ihead_poke_v( ihead_t* self, buffer_t* buf
                                , float*    motion
                                , float*    io
                                , int       size )
{
    // WIP
    // would write() benefit from vectorization?
    // can buffer_mac_v operate over a wider window of samples?
    // can we pass a meaningful vector of pre_level to mac_v?
        // perhaps peek_v & poke_v can be resurrected?
        // if they are *much* faster than before, might allow per-sample slews?
    for( int i=0; i<size; i++ ){ // WIP
        ihead_poke( self, buf, motion[i], io[i] );
    }
}

float ihead_peek( ihead_t* self, buffer_t* buf, float speed )
{
    float samps[4];
    buffer_peek_v( buf, samps, (self->rphase.i)-1, 4 );
    float out = interp_hermite_4pt( self->rphase.f, &samps[1] );

    // update phase
    phase_t* p = &(self->rphase);
    p->f += speed;
    while( p->f >= 1.0 ){ p->i++; p->f -= 1.0; }
    while( p->f <  0.0 ){ p->i--; p->f += 1.0; }

    return out;
}

float* ihead_peek_v( ihead_t* self, float* io, buffer_t* buf, float* motion, int size )
{
    float pf[size];
    int pi[size];

    phase_t* p = &(self->rphase);
    int pi_max = p->i;
    int pi_min = p->i;
    for( int i=0; i<size; i++ ){
        pf[i] = p->f;
        pi[i] = p->i;

        p->f += motion[i];
        while( p->f >= 1.0 ){
            p->i++;
            p->f -= 1.0;
            pi_max = p->i;
        }
        while( p->f < 0.0 ){
            p->i--;
            p->f += 1.0;
            pi_min = p->i;
        }
    }

    int span = pi_max-pi_min;
    float samps[span + 4];
    buffer_peek_v( buf, samps, pi_min-1, span+4 );

    for( int i=0; i<size; i++ ){
        float* buf4 = &samps[(pi[i] - pi_min)+1];
        io[i] = interp_hermite_4pt( pf[i], buf4 );
    }
    return io;
}


//////////////////////////////
// private funcs

static float* push_input( ihead_t* self, float* buf, float in )
{
    const int IN_BUF_MASK = 3; // ie IN_BUF_LEN - 1
    self->in_buf_ix = (self->in_buf_ix + 1) & IN_BUF_MASK;
    self->in_buf[self->in_buf_ix] = in;

    int i0 = (self->in_buf_ix + 1) & IN_BUF_MASK;
    int i1 = (self->in_buf_ix + 2) & IN_BUF_MASK;
    int i2 = (self->in_buf_ix + 3) & IN_BUF_MASK;
    int i3 =  self->in_buf_ix;

    buf[0] = self->in_buf[i0];
    buf[1] = self->in_buf[i1];
    buf[2] = self->in_buf[i2];
    buf[3] = self->in_buf[i3];

    return buf;
}

static int write( ihead_t* self, float rate, float input )
{
    float pushed_buf[4];
    push_input( self, pushed_buf, input );

    rate = (rate < 0.0) ? -rate : rate; // handle negative speeds

    float phi = 1.0/rate;
    float new_phase = self->out_phase + rate;
    int new_frames = (int)new_phase;
    // we want to track fractional output phase for interpolation
    // this is normalized to the distance between input frames
    // so: the distance to the first output frame boundary:
    if( new_frames ){
        float f = 1.0 - self->out_phase;
        f *= phi; // normalized (divided by rate);

        int i=0;
        while(i < new_frames) {
            // distance between output frames in this normalized space is 1/rate
            self->out_buf[i] = interp_hermite_4pt( f, &(pushed_buf[1]) );
            f += phi;
            i++;
        }
    }
    // store the remainder of the updated, un-normalized output phase
    self->out_phase = new_phase - (float)new_frames;
    return new_frames; // count of complete frames written
}
