/*
  Batteur LV2 plugin

  Copyright 2020, Paul Ferrand <paul@ferrand.cc>

  This file was based on skeleton and example code from the LV2 plugin 
  distribution available at http://lv2plug.in/

  The LV2 sample plugins have the following copyright and notice, which are 
  extended to the current work:
  Copyright 2011-2016 David Robillard <d@drobilla.net>
  Copyright 2011 Gabriel M. Beddingfield <gabriel@teuton.org>
  Copyright 2011 James Morris <jwm.art.net@gmail.com>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "lv2/atom/forge.h"
#include "lv2/atom/util.h"
#include "lv2/buf-size/buf-size.h"
#include "lv2/core/lv2.h"
#include "lv2/core/lv2_util.h"
#include "lv2/midi/midi.h"
#include "lv2/options/options.h"
#include "lv2/parameters/parameters.h"
#include "lv2/patch/patch.h"
#include "lv2/state/state.h"
#include "lv2/urid/urid.h"
#include "lv2/worker/worker.h"
#include "lv2/time/time.h"
#include "lv2/log/logger.h"
#include "lv2/log/log.h"

#include <math.h>
#include <sfizz.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_SFZ_FILE ""
#define TOCCATA_URI "https://github.com/sfztools/toccata"
#define TOCCATA_SFZ_PATH "instrument/organ.sfz"
#define CHANNEL_MASK 0x0F
#define NOTE_ON 0x90
#define NOTE_OFF 0x80
#define MIDI_CHANNEL(byte) (byte & CHANNEL_MASK)
#define MIDI_STATUS(byte) (byte & ~CHANNEL_MASK)
#define MAX_BLOCK_SIZE 8192
#define MAX_PATH_SIZE 1024
#define NUM_VOICES 256
#define UNUSED(x) (void)(x)

#define TOCCATA_BOURDON16_CC 200
#define TOCCATA_BOURDON8_CC 201
#define TOCCATA_MONTRE8_CC 202
#define TOCCATA_SALICIONAL8_CC 203
#define TOCCATA_OCTAVE4_CC 204
#define TOCCATA_FLUTE4_CC 205
#define TOCCATA_DOUBLETTE2_CC 206
#define TOCCATA_NAZARD_CC 207
#define TOCCATA_TIERCE_CC 208
#define TOCCATA_FOURNITURE_CC 209
#define TOCCATA_CORNET_CC 210
#define TOCCATA_TROMPETTE8_CC 211

#define TOCCATA_BOURDON16_DEFAULT 0.0f
#define TOCCATA_BOURDON8_DEFAULT 1.0f
#define TOCCATA_MONTRE8_DEFAULT 0.0f
#define TOCCATA_SALICIONAL8_DEFAULT 0.0f
#define TOCCATA_OCTAVE4_DEFAULT 0.0f
#define TOCCATA_FLUTE4_DEFAULT 0.0f
#define TOCCATA_DOUBLETTE2_DEFAULT 0.0f
#define TOCCATA_NAZARD_DEFAULT 0.0f
#define TOCCATA_TIERCE_DEFAULT 0.0f
#define TOCCATA_FOURNITURE_DEFAULT 0.0f
#define TOCCATA_CORNET_DEFAULT 0.0f
#define TOCCATA_TROMPETTE8_DEFAULT 0.0f

#define TOCCATA_BOURDON16_URI TOCCATA_URI ":" "bourdon16"
#define TOCCATA_BOURDON8_URI TOCCATA_URI ":" "bourdon8"
#define TOCCATA_MONTRE8_URI TOCCATA_URI ":" "montre8"
#define TOCCATA_SALICIONAL8_URI TOCCATA_URI ":" "salicional8"
#define TOCCATA_OCTAVE4_URI TOCCATA_URI ":" "octave4"
#define TOCCATA_FLUTE4_URI TOCCATA_URI ":" "flute4"
#define TOCCATA_DOUBLETTE2_URI TOCCATA_URI ":" "doublette2"
#define TOCCATA_NAZARD_URI TOCCATA_URI ":" "nazard"
#define TOCCATA_TIERCE_URI TOCCATA_URI ":" "tierce"
#define TOCCATA_FOURNITURE_URI TOCCATA_URI ":" "fourniture"
#define TOCCATA_CORNET_URI TOCCATA_URI ":" "cornet"
#define TOCCATA_TROMPETTE8_URI TOCCATA_URI ":" "trompette8"

typedef struct
{
    // Features
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2_Log_Log* log;

    // Ports
    const LV2_Atom_Sequence* input_port;
    LV2_Atom_Sequence* output_port;
    float *output_buffers[2];
    const float *freewheel_port;

    // Atom forge
    LV2_Atom_Forge forge; ///< Forge for writing atoms in run thread
    LV2_Atom_Forge_Frame notify_frame; ///< Cached for worker replies

    // Logger
    LV2_Log_Logger logger;

    // URIs
    LV2_URID midi_event_uri;
    LV2_URID options_interface_uri;
    LV2_URID max_block_length_uri;
    LV2_URID nominal_block_length_uri;
    LV2_URID sample_rate_uri;
    LV2_URID atom_object_uri;
    LV2_URID atom_float_uri;
    LV2_URID atom_int_uri;
    LV2_URID atom_urid_uri;
    LV2_URID atom_string_uri;
    LV2_URID atom_bool_uri;
    LV2_URID atom_path_uri;
    LV2_URID patch_set_uri;
    LV2_URID patch_get_uri;
    LV2_URID patch_put_uri;
    LV2_URID patch_property_uri;
    LV2_URID patch_value_uri;
    LV2_URID patch_body_uri;
    LV2_URID bourdon16_uri;
    LV2_URID bourdon8_uri;
    LV2_URID montre8_uri;
    LV2_URID salicional8_uri;
    LV2_URID octave4_uri;
    LV2_URID flute4_uri;
    LV2_URID doublette2_uri;
    LV2_URID nazard_uri;
    LV2_URID tierce_uri;
    LV2_URID fourniture_uri;
    LV2_URID cornet_uri;
    LV2_URID trompette8_uri;

    bool activated;
    bool should_send_state;
    int max_block_size;
    double sample_rate;
    float bourdon16_gain;
    float bourdon8_gain;
    float montre8_gain;
    float salicional8_gain;
    float octave4_gain;
    float flute4_gain;
    float doublette2_gain;
    float nazard_gain;
    float tierce_gain;
    float fourniture_gain;
    float cornet_gain;
    float trompette8_gain;
    // Sfizz related data
    sfizz_synth_t *synth;
} toccata_plugin_t;

enum {
    INPUT_PORT = 0,
    OUTPUT_PORT,
    LEFT_BUFFER,
    RIGHT_BUFFER,
    FREEWHEEL_PORT
};

static void
toccata_map_required_uris(toccata_plugin_t* self)
{
    LV2_URID_Map* map = self->map;
    self->midi_event_uri = map->map(map->handle, LV2_MIDI__MidiEvent);
    self->max_block_length_uri = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
    self->nominal_block_length_uri = map->map(map->handle, LV2_BUF_SIZE__nominalBlockLength);
    self->sample_rate_uri = map->map(map->handle, LV2_PARAMETERS__sampleRate);
    self->atom_float_uri = map->map(map->handle, LV2_ATOM__Float);
    self->atom_int_uri = map->map(map->handle, LV2_ATOM__Int);
    self->atom_path_uri = map->map(map->handle, LV2_ATOM__Path);
    self->atom_bool_uri = map->map(map->handle, LV2_ATOM__Bool);
    self->atom_string_uri = map->map(map->handle, LV2_ATOM__String);
    self->atom_urid_uri = map->map(map->handle, LV2_ATOM__URID);
    self->atom_object_uri = map->map(map->handle, LV2_ATOM__Object);
    self->patch_set_uri = map->map(map->handle, LV2_PATCH__Set);
    self->patch_get_uri = map->map(map->handle, LV2_PATCH__Get);
    self->patch_put_uri = map->map(map->handle, LV2_PATCH__Put);
    self->patch_body_uri = map->map(map->handle, LV2_PATCH__body);
    self->patch_property_uri = map->map(map->handle, LV2_PATCH__property);
    self->patch_value_uri = map->map(map->handle, LV2_PATCH__value);
    self->bourdon16_uri = map->map(map->handle, TOCCATA_BOURDON16_URI);
    self->bourdon8_uri = map->map(map->handle, TOCCATA_BOURDON8_URI);
    self->montre8_uri = map->map(map->handle, TOCCATA_MONTRE8_URI);
    self->salicional8_uri = map->map(map->handle, TOCCATA_SALICIONAL8_URI);
    self->octave4_uri = map->map(map->handle, TOCCATA_OCTAVE4_URI);
    self->flute4_uri = map->map(map->handle, TOCCATA_FLUTE4_URI);
    self->doublette2_uri = map->map(map->handle, TOCCATA_DOUBLETTE2_URI);
    self->nazard_uri = map->map(map->handle, TOCCATA_NAZARD_URI);
    self->tierce_uri = map->map(map->handle, TOCCATA_TIERCE_URI);
    self->fourniture_uri = map->map(map->handle, TOCCATA_FOURNITURE_URI);
    self->cornet_uri = map->map(map->handle, TOCCATA_CORNET_URI);
    self->trompette8_uri = map->map(map->handle, TOCCATA_TROMPETTE8_URI);
}

static void
connect_port(LV2_Handle instance,
    uint32_t port,
    void* data)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    // lv2_log_note(&self->logger, "[connect_port] Called for index %d on address %p\n", port, data);
    switch (port) {
    case INPUT_PORT:
        self->input_port = (const LV2_Atom_Sequence*)data;
        break;
    case OUTPUT_PORT:
        self->output_port = (LV2_Atom_Sequence*)data;
        break;
    case LEFT_BUFFER:
        self->output_buffers[0] = (float *)data;
        break;
    case RIGHT_BUFFER:
        self->output_buffers[1] = (float *)data;
        break;
    case FREEWHEEL_PORT:
        self->freewheel_port = (const float *)data;
    default:
        break;
    }
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor,
    double rate,
    const char* path,
    const LV2_Feature* const* features)
{
    UNUSED(descriptor);
    LV2_Options_Option* options;
    bool supports_bounded_block_size = false;
    bool options_has_block_size = false;
    bool supports_fixed_block_size = false;
    char* full_path;

    // Allocate and initialise instance structure.
    toccata_plugin_t* self = (toccata_plugin_t*)calloc(1, sizeof(toccata_plugin_t));
    if (!self)
        return NULL;

    // Set defaults
    self->max_block_size = MAX_BLOCK_SIZE;
    self->sample_rate = rate;
    self->activated = false;
    self->should_send_state = false;
    self->bourdon16_gain = TOCCATA_BOURDON16_DEFAULT;
    self->bourdon8_gain = TOCCATA_BOURDON8_DEFAULT;
    self->montre8_gain = TOCCATA_MONTRE8_DEFAULT;
    self->salicional8_gain = TOCCATA_SALICIONAL8_DEFAULT;
    self->octave4_gain = TOCCATA_OCTAVE4_DEFAULT;
    self->flute4_gain = TOCCATA_FLUTE4_DEFAULT;
    self->doublette2_gain = TOCCATA_DOUBLETTE2_DEFAULT;
    self->nazard_gain = TOCCATA_NAZARD_DEFAULT;
    self->tierce_gain = TOCCATA_TIERCE_DEFAULT;
    self->fourniture_gain = TOCCATA_FOURNITURE_DEFAULT;
    self->cornet_gain = TOCCATA_CORNET_DEFAULT;
    self->trompette8_gain = TOCCATA_TROMPETTE8_DEFAULT;

    // Get the features from the host and populate the structure
    for (const LV2_Feature* const* f = features; *f; f++) {
        // lv2_log_note(&self->logger, "Feature URI: %s\n", (**f).URI);

        if (!strcmp((**f).URI, LV2_URID__map))
            self->map = (**f).data;

        if (!strcmp((**f).URI, LV2_URID__unmap))
            self->unmap = (**f).data;

        if (!strcmp((**f).URI, LV2_BUF_SIZE__boundedBlockLength))
            supports_bounded_block_size = true;

        if (!strcmp((**f).URI, LV2_BUF_SIZE__fixedBlockLength))
            supports_fixed_block_size = true;

        if (!strcmp((**f).URI, LV2_OPTIONS__options))
            options = (**f).data;

        if (!strcmp((**f).URI, LV2_LOG__log))
            self->log = (**f).data;
    }

    // Setup the loggers
    lv2_log_logger_init(&self->logger, self->map, self->log);

    // The map feature is required
    if (!self->map) {
        lv2_log_error(&self->logger, "Map feature not found, aborting...\n");
        free(self);
        return NULL;
    }

    // Map the URIs we will need
    toccata_map_required_uris(self);

    // Initialize the forge
    lv2_atom_forge_init(&self->forge, self->map);

    // Check the options for the block size and sample rate parameters
    if (options) {
        for (const LV2_Options_Option* opt = options; opt->value || opt->key; ++opt) {
            if (opt->key == self->sample_rate_uri) {
                if (opt->type != self->atom_float_uri) {
                    lv2_log_warning(&self->logger, "Got a sample rate but the type was wrong\n");
                    continue;
                }
                self->sample_rate = *(float*)opt->value;
            } else if (opt->key == self->max_block_length_uri) {
                if (opt->type != self->atom_int_uri) {
                    lv2_log_warning(&self->logger, "Got a max block size but the type was wrong\n");
                    continue;
                }
                self->max_block_size = *(int*)opt->value;
                options_has_block_size = true;
            } else if (opt->key == self->nominal_block_length_uri) {
                if (opt->type != self->atom_int_uri) {
                    lv2_log_warning(&self->logger, "Got a nominal block size but the type was wrong\n");
                    continue;
                }
                self->max_block_size = *(int*)opt->value;
                options_has_block_size = true;
            }
        }
    } else {
        lv2_log_warning(&self->logger,
            "No option array was given upon instantiation; will use default values\n.");
    }

    // We need _some_ information on the block size
    if (!supports_bounded_block_size && !supports_fixed_block_size && !options_has_block_size) {
        lv2_log_error(&self->logger,
            "Bounded block size not supported and options gave no block size, aborting...\n");
        free(self);
        return NULL;
    }

    self->synth = sfizz_create_synth();
    sfizz_set_num_voices(self->synth, NUM_VOICES);
    sfizz_set_sample_rate(self->synth, self->sample_rate);
    sfizz_set_samples_per_block(self->synth, self->max_block_size);
    full_path = calloc(1, strlen(path) + strlen(TOCCATA_SFZ_PATH) + 1);
    strcpy(full_path, path);
    strcat(full_path, TOCCATA_SFZ_PATH);
    if (!sfizz_load_file(self->synth, full_path))
        return NULL;

    return (LV2_Handle)self;
}

static void
cleanup(LV2_Handle instance)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    sfizz_free(self->synth);
    free(self);
}

static void
activate(LV2_Handle instance)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    self->activated = true;
    sfizz_send_hdcc(self->synth, 0, TOCCATA_BOURDON16_CC, self->bourdon16_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_BOURDON8_CC, self->bourdon8_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_MONTRE8_CC, self->montre8_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_SALICIONAL8_CC, self->salicional8_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_OCTAVE4_CC, self->octave4_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_FLUTE4_CC, self->flute4_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_DOUBLETTE2_CC, self->doublette2_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_NAZARD_CC, self->nazard_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_TIERCE_CC, self->tierce_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_FOURNITURE_CC, self->fourniture_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_CORNET_CC, self->cornet_gain);
    sfizz_send_hdcc(self->synth, 0, TOCCATA_TROMPETTE8_CC, self->trompette8_gain);
}

static void
deactivate(LV2_Handle instance)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    self->activated = false;
}

static void
toccata_process_midi_event(toccata_plugin_t* self, const LV2_Atom_Event* ev)
{
    const uint8_t* const msg = (const uint8_t*)(ev + 1);
    switch (lv2_midi_message_type(msg)) {
    case LV2_MIDI_MSG_NOTE_ON:
        if (msg[2] == 0)
            goto noteoff; // 0 velocity note-ons should be forbidden but just in case...
        sfizz_send_note_on(self->synth,
                           (int)ev->time.frames,
                           (int)msg[1],
                           msg[2]);
        break;
    case LV2_MIDI_MSG_NOTE_OFF: noteoff:
        sfizz_send_note_off(self->synth,
                            (int)ev->time.frames,
                            (int)msg[1],
                            msg[2]);
        break;
    case LV2_MIDI_MSG_CONTROLLER:
        sfizz_send_cc(self->synth,
                      (int)ev->time.frames,
                      (int)msg[1],
                      msg[2]);
        break;
    default:
        break;
    }
}

static float
extract_gain_from_atom(const LV2_Atom* atom)
{
    const float atom_gain = ((LV2_Atom_Float*)atom)->body;
    if (atom_gain > 1.0f)
        return 1.0f;
    
    if (atom_gain < 0.0f)
        return 0.0f;
    
    return atom_gain;
}

static void
handle_patch_set(toccata_plugin_t* self, const LV2_Atom_Object* obj, int64_t frame)
{
    const LV2_Atom* property = NULL;
    lv2_atom_object_get(obj, self->patch_property_uri, &property, 0);
    if (!property) {
        lv2_log_error(&self->logger, "Could not get the property from the patch object, aborting.\n");
        return;
    }

    if (property->type != self->atom_urid_uri) {
        lv2_log_error(&self->logger, "Atom type was not a URID, aborting.\n");
        return;
    }

    const uint32_t key = ((const LV2_Atom_URID*)property)->body;
    const LV2_Atom* atom = NULL;
    lv2_atom_object_get(obj, self->patch_value_uri, &atom, 0);
    if (!atom) {
        lv2_log_error(&self->logger, "[handle_patch_set] Error retrieving the atom, aborting.\n");
        if (self->unmap)
            lv2_log_warning(&self->logger,
                "Atom URI: %s\n",
                self->unmap->unmap(self->unmap->handle, key));
        return;
    }

    if (key == self->bourdon16_uri) {
        self->bourdon16_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_BOURDON16_CC, self->bourdon16_gain);
    } else if (key == self->bourdon8_uri) {
        self->bourdon8_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_BOURDON8_CC, self->bourdon8_gain);
    } else if (key == self->montre8_uri) {
        self->montre8_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_MONTRE8_CC, self->montre8_gain);
    } else if (key == self->salicional8_uri) {
        self->salicional8_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_SALICIONAL8_CC, self->salicional8_gain);
    } else if (key == self->octave4_uri) {
        self->octave4_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_OCTAVE4_CC, self->octave4_gain);
    } else if (key == self->flute4_uri) {
        self->flute4_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_FLUTE4_CC, self->flute4_gain);
    } else if (key == self->doublette2_uri) {
        self->doublette2_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_DOUBLETTE2_CC, self->doublette2_gain);
    } else if (key == self->nazard_uri) {
        self->nazard_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_NAZARD_CC, self->nazard_gain);
    } else if (key == self->tierce_uri) {
        self->tierce_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_TIERCE_CC, self->tierce_gain);
    } else if (key == self->fourniture_uri) {
        self->fourniture_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_FOURNITURE_CC, self->fourniture_gain);
    } else if (key == self->cornet_uri) {
        self->cornet_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_CORNET_CC, self->cornet_gain);
    } else if (key == self->trompette8_uri) {
        self->trompette8_gain = extract_gain_from_atom(atom);
        sfizz_send_hdcc(self->synth, frame, TOCCATA_TROMPETTE8_CC, self->trompette8_gain);
    } else {
        lv2_log_warning(&self->logger, "[handle_patch_set] Unknown or unsupported object.\n");
        if (self->unmap)
            lv2_log_warning(&self->logger,
                "Object URI: %s\n",
                self->unmap->unmap(self->unmap->handle, key));
        return;
    }
}

static void
send_rank_state(toccata_plugin_t* self, float gain, LV2_URID uri)
{
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&self->forge, 0);
    lv2_atom_forge_object(&self->forge, &frame, 0, self->patch_set_uri);
    lv2_atom_forge_key(&self->forge, self->patch_property_uri);
    lv2_atom_forge_urid(&self->forge, uri);
    lv2_atom_forge_key(&self->forge, self->patch_value_uri);
    lv2_atom_forge_float(&self->forge, gain);
    lv2_atom_forge_pop(&self->forge, &frame);
}

static void
send_all_rank_states(toccata_plugin_t* self)
{
    send_rank_state(self, self->bourdon16_gain, self->bourdon16_uri);
    send_rank_state(self, self->bourdon8_gain, self->bourdon8_uri);
    send_rank_state(self, self->montre8_gain, self->montre8_uri);
    send_rank_state(self, self->salicional8_gain, self->salicional8_uri);
    send_rank_state(self, self->octave4_gain, self->octave4_uri);
    send_rank_state(self, self->flute4_gain, self->flute4_uri);
    send_rank_state(self, self->doublette2_gain, self->doublette2_uri);
    send_rank_state(self, self->nazard_gain, self->nazard_uri);
    send_rank_state(self, self->tierce_gain, self->tierce_uri);
    send_rank_state(self, self->fourniture_gain, self->fourniture_uri);
    send_rank_state(self, self->cornet_gain, self->cornet_uri);
    send_rank_state(self, self->trompette8_gain, self->trompette8_uri);
}

static void
handle_patch_get(toccata_plugin_t* self, const LV2_Atom_Object* obj, int64_t frame)
{
    UNUSED(frame);
    const LV2_Atom_URID* property = NULL;
    lv2_atom_object_get(obj, self->patch_property_uri, &property, 0);
    if (!property) // Send the full state
    {
        lv2_log_warning(&self->logger, "Got an Patch GET with no body.\n");
        send_all_rank_states(self);
        return;
    } else if (property->body == self->bourdon16_uri) {
        send_rank_state(self, self->bourdon16_gain, self->bourdon16_uri);
    } else if (property->body == self->bourdon8_uri) {
        send_rank_state(self, self->bourdon8_gain, self->bourdon8_uri);
    } else if (property->body == self->montre8_uri) {
        send_rank_state(self, self->montre8_gain, self->montre8_uri);
    } else if (property->body == self->salicional8_uri) {
        send_rank_state(self, self->salicional8_gain, self->salicional8_uri);
    } else if (property->body == self->octave4_uri) {
        send_rank_state(self, self->octave4_gain, self->octave4_uri);
    } else if (property->body == self->flute4_uri) {
        send_rank_state(self, self->flute4_gain, self->flute4_uri);
    } else if (property->body == self->doublette2_uri) {
        send_rank_state(self, self->doublette2_gain, self->doublette2_uri);
    } else if (property->body == self->nazard_uri) {
        send_rank_state(self, self->nazard_gain, self->nazard_uri);
    } else if (property->body == self->tierce_uri) {
        send_rank_state(self, self->tierce_gain, self->tierce_uri);
    } else if (property->body == self->fourniture_uri) {
        send_rank_state(self, self->fourniture_gain, self->fourniture_uri);
    } else if (property->body == self->cornet_uri) {
        send_rank_state(self, self->cornet_gain, self->cornet_uri);
    } else if (property->body == self->trompette8_uri) {
        send_rank_state(self, self->trompette8_gain, self->trompette8_uri);
    } else {
        lv2_log_warning(&self->logger, "[handle_patch_set] Unknown or unsupported object.\n");
        if (self->unmap)
            lv2_log_warning(&self->logger,
                "Object URI: %s\n",
                self->unmap->unmap(self->unmap->handle, property->body));
        return;
    }
}

static void
check_freewheeling(toccata_plugin_t* self)
{
    if (*(self->freewheel_port) > 0)
    {
        sfizz_enable_freewheeling(self->synth);
    }
    else
    {
        sfizz_disable_freewheeling(self->synth);
    }
}

static void
run(LV2_Handle instance, uint32_t sample_count)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    if (!self->activated)
        return;

    if (!self->input_port || !self->output_port)
        return;

    // Set up forge to write directly to notify output port.
    const size_t notify_capacity = self->output_port->atom.size;
    lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->output_port, notify_capacity);

    // Start a sequence in the notify output port.
    lv2_atom_forge_sequence_head(&self->forge, &self->notify_frame, 0);

    LV2_ATOM_SEQUENCE_FOREACH(self->input_port, ev)
    {
        // If the received atom is an object/patch message
        if (ev->body.type == self->atom_object_uri) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
            if (obj->body.otype == self->patch_set_uri) {
                handle_patch_set(self, obj, ev->time.frames);
            } else if (obj->body.otype == self->patch_get_uri) {
                handle_patch_get(self, obj, ev->time.frames);
            } else {
                lv2_log_warning(&self->logger, "Got an Object atom but it was not supported.\n");
                if (self->unmap)
                    lv2_log_warning(&self->logger,
                        "Object URI: %s\n",
                        self->unmap->unmap(self->unmap->handle, obj->body.otype));
                continue;
            }
            // Got an atom that is a MIDI event
        } else if (ev->body.type == self->midi_event_uri) {
            toccata_process_midi_event(self, ev);
        }
    }

    check_freewheeling(self);
    sfizz_render_block(self->synth, self->output_buffers, 2, (int)sample_count);
    
    if (self->should_send_state) {
        send_all_rank_states(self);
    }

    lv2_atom_forge_pop(&self->forge, &self->notify_frame);
}

static uint32_t
lv2_get_options(LV2_Handle instance, LV2_Options_Option* options)
{
    UNUSED(instance);
    UNUSED(options);
    // We have no options
    return LV2_OPTIONS_ERR_UNKNOWN;
}

static uint32_t
lv2_set_options(LV2_Handle instance, const LV2_Options_Option* options)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;

    // Update the block size and sample rate as needed
    for (const LV2_Options_Option* opt = options; opt->value || opt->key; ++opt) {
        if (opt->key == self->sample_rate_uri) {
            if (opt->type != self->atom_float_uri) {
                lv2_log_warning(&self->logger, "Got a sample rate but the type was wrong\n");
                continue;
            }
            self->sample_rate = *(float*)opt->value;
            sfizz_set_sample_rate(self->synth, self->sample_rate);
        } else if (opt->key == self->nominal_block_length_uri) {
            if (opt->type != self->atom_int_uri) {
                lv2_log_warning(&self->logger, "Got a nominal block size but the type was wrong\n");
                continue;
            }
            self->max_block_size = *(int*)opt->value;
            sfizz_set_samples_per_block(self->synth, self->max_block_size);
        }
    }
    return LV2_OPTIONS_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle instance,
    LV2_State_Retrieve_Function retrieve,
    LV2_State_Handle handle,
    uint32_t flags,
    const LV2_Feature* const* features)
{
    UNUSED(flags);
    UNUSED(features);
    toccata_plugin_t* self = (toccata_plugin_t*)instance;

    // Fetch back the saved file path, if any
    size_t size;
    uint32_t type;
    uint32_t val_flags;
    const void* value;
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status
save(LV2_Handle instance,
    LV2_State_Store_Function store,
    LV2_State_Handle handle,
    uint32_t flags,
    const LV2_Feature* const* features)
{
    UNUSED(flags);
    UNUSED(features);
    toccata_plugin_t* self = (toccata_plugin_t*)instance;

    return LV2_STATE_SUCCESS;
}

static const void*
extension_data(const char* uri)
{
    static const LV2_Options_Interface options = { lv2_get_options, lv2_set_options };
    static const LV2_State_Interface state = { save, restore };

    // Advertise the extensions we support
    if (!strcmp(uri, LV2_OPTIONS__interface))
        return &options;
    else if (!strcmp(uri, LV2_STATE__interface))
        return &state;

    return NULL;
}

static const LV2_Descriptor descriptor = {
    TOCCATA_URI,
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
    switch (index) {
    case 0:
        return &descriptor;
    default:
        return NULL;
    }
}
