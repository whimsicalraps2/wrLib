#include "wrIPlayer.h"

#include <stdlib.h>
#include <stdio.h>


///////////////////////////////
// private declarations

static float tape_clamp( player_t* self, float location );
bool player_is_going( player_t* self );
void queue_goto( player_t* self, int sample );
int get_queued_goto( player_t* self );


///////////////////////////////
// setup

player_t* player_init( buffer_t* buffer )
{
    player_t* self = malloc( sizeof( player_t ) );
    if( !self ){ printf("player malloc failed.\n"); return NULL; }

    self->head = ihead_fade_init();
    if( !self->head ){ printf("player head failed.\n"); return NULL; }

    self->transport = transport_init();
    if( !self->transport ){ printf("player transport failed.\n"); return NULL; }

    player_speed( self, 0.0 );
    self->motion = 0.0;
    player_load( self, buffer );
    player_playing( self, false );
    player_head_order( self, false );
    player_rec_level( self, 0.0 );
    player_pre_level( self, 1.0 );
    player_loop(self, true);
    self->going = false;
    return self;
}

void player_deinit( player_t* self )
{
    transport_deinit( self->transport );
    ihead_fade_deinit( self->head );
    free(self); self = NULL;
}


/////////////////////////////
// params

player_t* player_load( player_t* self, buffer_t* buffer )
{
    self->buf = buffer;
    if(self->buf){
        self->tape_end = buffer->len;
        player_goto( self, 0 );
        player_loop_start( self, 0.0 );
        player_loop_end( self, self->tape_end );
    }
    return self;
}

void player_playing( player_t* self, bool is_play )
{
    self->playing = is_play;
    transport_active( self->transport, is_play, transport_motor_standard );
}

void player_goto( player_t* self, int sample )
{
    if( self->buf ){
        if( buffer_request( self->buf, sample ) ){
            ihead_fade_jumpto( self->head
                             , self->buf
                             , sample
                             , (self->motion >= 0.0)
                             );
            self->queued_location = -1;
        } else {
            self->going = true;
            queue_goto( self, sample );
        }
    }
    self->going = false;
}

void player_speed( player_t* self, float speed )
{
    self->speed = speed;
    transport_speed_active( self->transport, speed );
}

void player_nudge( player_t* self, float amount )
{
    transport_nudge( self->transport, amount );
}

void player_recording( player_t* self, bool is_record )
{
    ihead_fade_recording( self->head, is_record );
}

void player_rec_level( player_t* self, float rec_level )
{
    ihead_fade_rec_level( self->head, rec_level );
}

void player_pre_level( player_t* self, float pre_level )
{
    ihead_fade_pre_level( self->head, pre_level );
}

void player_head_order( player_t* self, bool play_before_erase )
{
    self->play_before_erase = play_before_erase;
}

void player_loop( player_t* self, bool is_looping )
{
    self->loop = is_looping;
}

void player_loop_start( player_t* self, float location )
{
    self->loop_start = tape_clamp( self, location );
}

void player_loop_end( player_t* self, float location )
{
    self->loop_end = tape_clamp( self, location );
}


///////////////////////////////////
// param getters

bool player_is_playing( player_t* self )
{
    return self->playing;
}

float player_get_goto( player_t* self )
{
    return (float)ihead_fade_get_location( self->head );
}

float player_get_speed( player_t* self )
{
    return self->speed;
}

float player_get_speed_live( player_t* self )
{
    return self->motion;
}

bool player_is_recording( player_t* self )
{
    return ihead_fade_is_recording( self->head );
}

float player_get_rec_level( player_t* self )
{
    return ihead_fade_get_rec_level( self->head );
}

float player_get_pre_level( player_t* self )
{
    return ihead_fade_get_pre_level( self->head );
}

bool player_is_head_order( player_t* self )
{
    return self->play_before_erase;
}

bool player_is_looping( player_t* self )
{
    return self->loop;
}

float player_get_loop_start( player_t* self )
{
    return self->loop_start;
}

float player_get_loop_end( player_t* self )
{
    return self->loop_end;
}


/////////////////////////////////////
// signals

// this is an abstraction of a 'tape head'
// TODO rename!
//#define LEAD_IN ((float)64.0)
#define LEAD_IN ((float)0.0)
float player_step( player_t* self, float in )
{
    if( !self->buf ){ return 0.0; } // no buffer available

    int goto_dest = get_queued_goto( self );
    if( goto_dest != -1 ){ // check if a queued goto is ready
        //printf("try queued\n");
        player_goto( self, goto_dest );
    }

    bool fwd = self->motion >= 0;
    self->motion = transport_speed_step( self->transport );
    if( fwd != (self->motion >= 0) ){ // sign change in speed
        player_goto( self, player_get_goto(self) ); // reset head offset
    }

    float out = ihead_fade_peek( self->head, self->buf );
    ihead_fade_poke( self->head
                   , self->buf
                   , self->motion
                   , in
                   );
    float new_phase = ihead_fade_update_phase( self->head, self->motion );

    if( !player_is_going( self ) ){ // only edge check if there isn't a queued jump
        float jumpto = -1.0;
        if( self->loop ){ // apply loop brace
            if( new_phase >= self->loop_end ){ jumpto = self->loop_start; }
            else if( new_phase <  self->loop_start ){ jumpto = self->loop_end; }
        } else { // no loop brace, so just loop the whole buffer
            // TODO this check could be *always* run, but result in a STOP (tape edge safety)
            if( new_phase >= (self->tape_end - LEAD_IN) ){ jumpto = LEAD_IN; }
            else if( new_phase < LEAD_IN ){ jumpto = self->tape_end - LEAD_IN; }
        }
        if( jumpto >= 0.0 ){ // if there's a new jump, request it
            player_goto( self, jumpto );
        }
    }

    // head order TODO might need slew or transition handling
    if( self->play_before_erase && ihead_fade_is_recording( self->head ) ){
        out *= ihead_fade_get_pre_level( self->head );
    }
    return out;
}

bool player_is_going( player_t* self )
{
    return self->going;
}

float* player_step_v( player_t* self, float* io, int size )
{
    float* b = io;
    for( int i=0; i<size; i++ ){
        *b = player_step( self, *b );
        b++;
    }
    transport_unnudge( self->transport );
    return io;
}


//////////////////////////////////
// private funcs

static float tape_clamp( player_t* self, float location )
{
    if( location < LEAD_IN ){ location = LEAD_IN; }
    if( location > (self->tape_end - LEAD_IN) ){ location = self->tape_end - LEAD_IN; }
    return location;
}

void queue_goto( player_t* self, int sample )
{
    if( sample != self->queued_location ){
        printf("TODO queue a request until it becomes available %i\n", sample);
        self->queued_location = sample;
    }
}

int get_queued_goto( player_t* self )
{
    return self->queued_location;
}
