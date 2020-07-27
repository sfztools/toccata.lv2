/*
  Toccata LV2 plugin

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

enum {
    INPUT_PORT = 0,
    LEFT_BUFFER,
    RIGHT_BUFFER,
    FREEWHEEL_PORT,
    BOURDON16_PORT,
    BOURDON8_PORT,
    MONTRE8_PORT,
    SALICIONAL8_PORT,
    OCTAVE4_PORT,
    FLUTE4_PORT,
    DOUBLETTE2_PORT,
    NAZARD_PORT,
    TIERCE_PORT,
    FOURNITURE_PORT,
    CORNET_PORT,
    TROMPETTE8_PORT
};

typedef struct
{
    // Features
    LV2_URID_Map* map;
    LV2_URID_Unmap* unmap;
    LV2_Log_Log* log;

    // Ports
    const LV2_Atom_Sequence* input_port;
    float *output_buffers[2];
    const float *freewheel_port;
    const float *bourdon16_port;
    const float *bourdon8_port;
    const float *montre8_port;
    const float *salicional8_port;
    const float *octave4_port;
    const float *flute4_port;
    const float *doublette2_port;
    const float *nazard_port;
    const float *tierce_port;
    const float *fourniture_port;
    const float *cornet_port;
    const float *trompette8_port;

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

    bool activated;
    int max_block_size;
    double sample_rate;
    // Sfizz related data
    sfizz_synth_t *synth;
} toccata_plugin_t;

static float clamp_gain(float gain)
{
    gain = fminf(1.0f, gain);
    gain = fmaxf(0.0f, gain);
    return gain;
}

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
    self->atom_bool_uri = map->map(map->handle, LV2_ATOM__Bool);
    self->atom_string_uri = map->map(map->handle, LV2_ATOM__String);
    self->atom_urid_uri = map->map(map->handle, LV2_ATOM__URID);
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
    case LEFT_BUFFER:
        self->output_buffers[0] = (float *)data;
        break;
    case RIGHT_BUFFER:
        self->output_buffers[1] = (float *)data;
        break;
    case FREEWHEEL_PORT:
        self->freewheel_port = (const float *)data;
        break;
    case BOURDON16_PORT:
        self->bourdon16_port = (const float*)data;
        break;
    case BOURDON8_PORT:
        self->bourdon8_port = (const float*)data;
        break;
    case MONTRE8_PORT:
        self->montre8_port = (const float*)data;
        break;
    case SALICIONAL8_PORT:
        self->salicional8_port = (const float*)data;
        break;
    case OCTAVE4_PORT:
        self->octave4_port = (const float*)data;
        break;
    case FLUTE4_PORT:
        self->flute4_port = (const float*)data;
        break;
    case DOUBLETTE2_PORT:
        self->doublette2_port = (const float*)data;
        break;
    case NAZARD_PORT:
        self->nazard_port = (const float*)data;
        break;
    case TIERCE_PORT:
        self->tierce_port = (const float*)data;
        break;
    case FOURNITURE_PORT:
        self->fourniture_port = (const float*)data;
        break;
    case CORNET_PORT:
        self->cornet_port = (const float*)data;
        break;
    case TROMPETTE8_PORT:
        self->trompette8_port = (const float*)data;
        break;
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
    LV2_Options_Option* options = NULL;
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
    self->bourdon16_gain = 0.0f;
    self->bourdon8_gain = 0.0f;
    self->montre8_gain = 0.0f;
    self->salicional8_gain = 0.0f;
    self->octave4_gain = 0.0f;
    self->flute4_gain = 0.0f;
    self->doublette2_gain = 0.0f;
    self->nazard_gain = 0.0f;
    self->tierce_gain = 0.0f;
    self->fourniture_gain = 0.0f;
    self->cornet_gain = 0.0f;
    self->trompette8_gain = 0.0f;

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
    bool file_loaded = sfizz_load_file(self->synth, full_path);
    free(full_path);

    if (!file_loaded)
        return NULL;


    lv2_log_note(&self->logger, "Plugin instantiated\n");
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
    lv2_log_note(&self->logger, "Plugin active\n");
    self->activated = true;
}

static void
deactivate(LV2_Handle instance)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    lv2_log_note(&self->logger, "Plugin desactivated\n");
    self->activated = false;
}

static void
process_midi_event(toccata_plugin_t* self, const LV2_Atom_Event* ev)
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
send_cc_if_necessary(toccata_plugin_t* self, const float* port, float* value, int cc)
{
    if (*port != *value) {
        *value = clamp_gain(*port);
        sfizz_send_hdcc(self->synth, 0, cc, *value);
    }
}

static void
run(LV2_Handle instance, uint32_t sample_count)
{
    toccata_plugin_t* self = (toccata_plugin_t*)instance;
    if (!self->activated)
        return;

    if (!self->input_port)
        return;

    LV2_ATOM_SEQUENCE_FOREACH(self->input_port, ev)
    {
        // If the received atom is an object/patch message
        if (ev->body.type == self->atom_object_uri) {
            const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
            lv2_log_warning(&self->logger, "Got an Object atom but it was not supported.\n");
            if (self->unmap)
                lv2_log_warning(&self->logger,
                    "Object URI: %s\n",
                    self->unmap->unmap(self->unmap->handle, obj->body.otype));
            continue;
            // Got an atom that is a MIDI event
        } else if (ev->body.type == self->midi_event_uri) {
            process_midi_event(self, ev);
        }
    }

    send_cc_if_necessary(self, self->bourdon16_port, &self->bourdon16_gain, TOCCATA_BOURDON16_CC);
    send_cc_if_necessary(self, self->bourdon8_port, &self->bourdon8_gain, TOCCATA_BOURDON8_CC);
    send_cc_if_necessary(self, self->montre8_port, &self->montre8_gain, TOCCATA_MONTRE8_CC);
    send_cc_if_necessary(self, self->salicional8_port, &self->salicional8_gain, TOCCATA_SALICIONAL8_CC);
    send_cc_if_necessary(self, self->octave4_port, &self->octave4_gain, TOCCATA_OCTAVE4_CC);
    send_cc_if_necessary(self, self->flute4_port, &self->flute4_gain, TOCCATA_FLUTE4_CC);
    send_cc_if_necessary(self, self->doublette2_port, &self->doublette2_gain, TOCCATA_DOUBLETTE2_CC);
    send_cc_if_necessary(self, self->nazard_port, &self->nazard_gain, TOCCATA_NAZARD_CC);
    send_cc_if_necessary(self, self->tierce_port, &self->tierce_gain, TOCCATA_TIERCE_CC);
    send_cc_if_necessary(self, self->fourniture_port, &self->fourniture_gain, TOCCATA_FOURNITURE_CC);
    send_cc_if_necessary(self, self->cornet_port, &self->cornet_gain, TOCCATA_CORNET_CC);
    send_cc_if_necessary(self, self->trompette8_port, &self->trompette8_gain, TOCCATA_TROMPETTE8_CC);

    check_freewheeling(self);
    sfizz_render_block(self->synth, self->output_buffers, 2, (int)sample_count);
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

static const void*
extension_data(const char* uri)
{
    static const LV2_Options_Interface options = { lv2_get_options, lv2_set_options };
    // Advertise the extensions we support
    if (!strcmp(uri, LV2_OPTIONS__interface))
        return &options;

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
