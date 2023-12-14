// TODO: Armoda stuff

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "SoundDecoder.h"

#include "SoundDecoder_Internal.h"
#include "SDL_endian_minimal.h"
#include "ALmixer_RWops.h"

#include "mixer_software.h"
#include "osutil.h"
#include "tracker.h"

#define ERR_IO_ERROR "I/O error"

static int Armoda_init(void);
static void Armoda_quit(void);
static int Armoda_open(Sound_Sample *sample, const char *ext);
static void Armoda_close(Sound_Sample *sample);
static uint32_t Armoda_read(Sound_Sample *sample);
static int Armoda_rewind(Sound_Sample *sample);
static int Armoda_seek(Sound_Sample *sample, uint32_t ms);

static const char *extensions_armoda[] = { "MOD", "S3M", "DSM", NULL };
const Sound_DecoderFunctions __Sound_DecoderFunctions_Armoda =
{
    {
        extensions_armoda,
        "DESCRIPTIONS",
        "AUTHORS",
        "URL"
    },

    Armoda_init,       /*   init() method */
    Armoda_quit,       /*   quit() method */
    Armoda_open,       /*   open() method */
    Armoda_close,      /*  close() method */
    Armoda_read,       /*   read() method */
    Armoda_rewind,     /* rewind() method */
    Armoda_seek        /*   seek() method */
};

static Mixer* s_ArmodaMixer;

typedef struct SoundDecoder_Armoda
{
    ARM_Module mod;
    ARM_Tracker player;
} SoundDecoder_Armoda;

static int Armoda_init(void)
{
    s_ArmodaMixer = MXR_InitSoftMixer(44100, NULL);

    return(1);  /* always succeeds. */
} /* Armoda_init */


static void Armoda_quit(void)
{
    free(s_ArmodaMixer->private);
    free(s_ArmodaMixer);
} /* Armoda_quit */

static int Armoda_open(Sound_Sample *sample, const char *ext)
{
    SoundDecoder_Armoda *armoda;
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    
    armoda = (SoundDecoder_Armoda *) malloc(sizeof (SoundDecoder_Armoda));
    BAIL_IF_MACRO(armoda == NULL, ERR_OUT_OF_MEMORY, 0);
	
    if (ARM_LoadModule(&armoda->mod, internal->rw, ext) < 0)
    {
        free(armoda);
        
        BAIL_MACRO("Error loading module.", 0);
    }

    sample->flags = SOUND_SAMPLEFLAG_NONE;
    sample->actual.rate = 44100; // TODO?
    sample->actual.channels = 2;
    sample->actual.format = AUDIO_S16LSB;

	ARM_InitTracker(&armoda->player, &armoda->mod, s_ArmodaMixer, sample->actual.rate);

    internal->decoder_private = armoda;
    internal->total_time = -1;

    return(1);
} /* Armoda_open */


static void Armoda_close(Sound_Sample *sample)
{
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    SoundDecoder_Armoda *armoda = (SoundDecoder_Armoda *) internal->decoder_private;

	ARM_FreeTrackerData(&armoda->player);
	ARM_FreeModuleData(&armoda->mod);
} /* Armoda_close */

static void Write(ALshort* out, const float* from, int n)
{
    int i;
    for (i = 0; i < n; i++) out[i] = (ALshort)(32767.0f * from[i]);
}

static uint32_t Armoda_read(Sound_Sample *sample)
{
	Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    SoundDecoder_Armoda *armoda = (SoundDecoder_Armoda *) internal->decoder_private;

    ALshort* buffer = (ALshort *) internal->buffer;
    int offset = 0, size = (int)internal->buffer_size / sizeof(ALshort);

    if (armoda->player.done)
        return 0;

    while (offset < size)
    {
        int n = size - offset, done;
        float* data = ARM_GetRenderResults(&armoda->player, &n, &done);

        if (data)
        {
            Write(&buffer[offset], data, n);

            offset += n;
        }

        if (done)
        {
            if (data)
                ARM_AdvancePosition(&armoda->player);

            if (armoda->player.done)
            {
                sample->flags |= SOUND_SAMPLEFLAG_EOF;
                break;
            }
            else
                ARM_RenderOneTick(&armoda->player, (float)armoda->player.num_channels);
        }
    }

    return ((uint32_t) offset * sizeof(ALshort));
} /* Armoda_read */


static int Armoda_rewind(Sound_Sample *sample)
{
	Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    SoundDecoder_Armoda *armoda = (SoundDecoder_Armoda *) internal->decoder_private;

	ARM_FreeTrackerData(&armoda->player);
	ARM_InitTracker(&armoda->player, &armoda->mod, s_ArmodaMixer, sample->actual.rate);

    return(1);
} /* Armoda_rewind */


static int Armoda_seek(Sound_Sample *sample, uint32_t ms)
{
    // TODO!?
    BAIL_IF_MACRO(-1, ERR_IO_ERROR, 0);
    return(1);
} /* Armoda_seek */

