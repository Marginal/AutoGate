/*
 * AutoGate sound
 *
 * (c) Jonathan Harris 2013
 *
 * Initialisation and WAV loading code taken from http://www.xsquawkbox.net/xpsdk/mediawiki/OpenAL_Shared_Example
 * Remainder licensed under GNU LGPL v2.1.
 */

#undef TESTSOUND

#include "autogate.h"

#if APL
#  include <CoreFoundation/CFString.h>
#  include <CoreFoundation/CFURL.h>
#endif

/* Globals */
#ifdef MAKECONTEXT	/* We have to make our own device and context on Windows under X-Plane <10.2 */
static ALCdevice  *my_dev = NULL;
static ALCcontext *my_ctx = NULL;
#endif
ALuint snd_src = 0;			// Sample source and buffer - this is the one "sound" we play.
static ALuint snd_buffer = 0;
static const ALfloat zero[3] = { 0 };

XPLMDataRef ref_audio, ref_paused, ref_view_external;

#ifdef TESTSOUND
# ifndef DEBUG
#  error "You don't want to make a release with TESTSOUND enabled!"
# endif
XPLMDataRef ref_test_x, ref_test_y, ref_test_z;
#endif


/**************************************************************************************************************
 * WAVE FILE LOADING
 **************************************************************************************************************/

// You can just use alutCreateBufferFromFile to load a wave file, but there seems to be a lot of problems with 
// alut not beign available, being deprecated, etc.  So...here's a stupid routine to load a wave file.  I have
// tested this only on x86 machines, so if you find a bug on PPC please let me know.

// Macros to swap endian-values.

#define SWAP_16(value)                                  \
    (((((unsigned short)value)<<8) & 0xFF00)   |        \
     ((((unsigned short)value)>>8) & 0x00FF))

#define SWAP_32(value)                                  \
    (((((unsigned int)value)<<24) & 0xFF000000)  |      \
     ((((unsigned int)value)<< 8) & 0x00FF0000)  |      \
     ((((unsigned int)value)>> 8) & 0x0000FF00)  |      \
     ((((unsigned int)value)>>24) & 0x000000FF))

// Wave files are RIFF files, which are "chunky" - each section has an ID and a length.  This lets us skip
// things we can't understand to find the parts we want.  This header is common to all RIFF chunks.
typedef struct
{
    int		id;
    int		size;
} chunk_header;

// WAVE file format info.  We pass this through to OpenAL so we can support mono/stereo, 8/16/bit, etc.
typedef struct
{
    short	format;				// PCM = 1, not sure what other values are legal.
    short	num_channels;
    int		sample_rate;
    int		byte_rate;
    short	block_align;
    short	bits_per_sample;
} format_info;

// This utility returns the start of data for a chunk given a range of bytes it might be within.  Pass 1 for
// swapped if the machine is not the same endian as the file.
static char *find_chunk(char *file_begin, char *file_end, int desired_id, int swapped)
{
    if (swapped) desired_id = SWAP_32(desired_id);

    while (file_begin < file_end)
    {
        chunk_header *h = (chunk_header *) file_begin;
        int chunk_size;
        char *next;

        if(h->id == desired_id)
            return file_begin+sizeof(chunk_header);
        chunk_size = swapped ? SWAP_32(h->size) : h->size;
        next = file_begin + chunk_size + sizeof(chunk_header);
        if (next > file_end || next <= file_begin)
            return NULL;
        file_begin = next;
    }
    return NULL;
}

// Given a chunk, find its end by going back to the header.
static char *chunk_end(char * chunk_start, int swapped)
{
    chunk_header *h = (chunk_header *) (chunk_start - sizeof(chunk_header));
    return chunk_start + (swapped ? SWAP_32(h->size) : h->size);
}

#define RIFF_ID 0x46464952			// 'RIFF'
#define FMT_ID  0x20746D66			// 'fmt '
#define DATA_ID 0x61746164			// 'data'

#define FAIL(msg) { free(mem); return xplog(msg); }

ALuint load_wave(const char *file_name)
{
    FILE *h;
    char *mem, *mem_end, *riff, *data;
    int file_size, swapped = 0;
    format_info *fmt;
    int sample_size, data_bytes, data_samples;
    ALuint buf_id = 0;

    // First: we open the file and copy it into a single large memory buffer for processing.
    if (!(h = fopen(file_name, "rb"))) return xplog("Can't open WAV file.");
    fseek(h, 0, SEEK_END);
    file_size = ftell(h);
    fseek(h, 0, SEEK_SET);
    if (!(mem = malloc(file_size)))
    {
        fclose(h);
        return xplog("Out of memory!");
    }
    if (fread(mem, 1, file_size, h) != file_size)
    {
        fclose(h);
        FAIL("Can't read WAV file.");
    }
    fclose(h);
    mem_end = mem + file_size;

    // Second: find the RIFF chunk.  Note that by searching for RIFF both normal
    // and reversed, we can automatically determine the endian swap situation for
    // this file regardless of what machine we are on.
    if (!(riff = find_chunk(mem, mem_end, RIFF_ID, 0)))
    {
        if (!(riff = find_chunk(mem, mem_end, RIFF_ID, 1)))
            FAIL("Can't find RIFF chunk in WAV file.")
        else
            swapped = 1;
    }

    // The wave chunk isn't really a chunk at all. :-(  It's just a "WAVE" tag
    // followed by more chunks.  This strikes me as totally inconsistent, but
    // anyway, confirm the WAVE ID and move on.
    if (memcmp(riff, "WAVE", 4)) FAIL("Can't find WAVE signature in WAV file.");

    // Find the format chunk, and swap the values if needed.  This gives us our real format.
    if (!(fmt = (format_info *) find_chunk(riff+4, chunk_end(riff,swapped), FMT_ID, swapped)))
        FAIL("Can't find FMT chunk in WAV file.");
    if (swapped)
    {
        fmt->format = SWAP_16(fmt->format);
        fmt->num_channels = SWAP_16(fmt->num_channels);
        fmt->sample_rate = SWAP_32(fmt->sample_rate);
        fmt->byte_rate = SWAP_32(fmt->byte_rate);
        fmt->block_align = SWAP_16(fmt->block_align);
        fmt->bits_per_sample = SWAP_16(fmt->bits_per_sample);
    }

    // Reject things we don't understand...expand this code to support weirder audio formats.
    if(fmt->format != 1) FAIL("WAV file is not PCM format.");
    if(fmt->num_channels != 1 && fmt->num_channels != 2) FAIL("WAV file is neither mono nor stereo.");
    if(fmt->bits_per_sample != 8 && fmt->bits_per_sample != 16) FAIL("WAV file is neither 8 nor 16 bit.");
    if (!(data = find_chunk(riff+4, chunk_end(riff,swapped), DATA_ID, swapped))) FAIL("Can't find the DATA chunk in WAV file.");
    sample_size = fmt->num_channels * fmt->bits_per_sample / 8;
    data_bytes = chunk_end(data,swapped) - data;
    data_samples = data_bytes / sample_size;

    // If the file is swapped and we have 16-bit audio, we need to endian-swap the audio too or we'll
    // get something that sounds just astoundingly bad!
    if (fmt->bits_per_sample == 16 && swapped)
    {
        short *ptr = (short *) data;
        int words = data_samples * fmt->num_channels;
        while (words--)
        {
            *ptr = SWAP_16(*ptr);
            ptr++;
        }
    }

    // Finally, the OpenAL crud.  Build a new OpenAL buffer and send the data to OpenAL, passing in
    // OpenAL format enums based on the format chunk.
    alGenBuffers(1, &buf_id);
    if (!buf_id) FAIL("Can't create sound buffer.");
    alBufferData(buf_id, fmt->bits_per_sample == 16 ?
                 (fmt->num_channels == 2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16) :
                 (fmt->num_channels == 2 ? AL_FORMAT_STEREO8 : AL_FORMAT_MONO8),
                 data, data_bytes, fmt->sample_rate);
    free(mem);
    return buf_id;
}

#undef FAIL


/* Convert path to posix style in-place */
void posixify(char *path)
{
#if APL
    if (*path!='/')
    {
        /* X-Plane 9 - screw around with HFS paths FFS */
        int isfolder = (path[strlen(path)-1]==':');
        CFStringRef hfspath = CFStringCreateWithCString(NULL, path, kCFStringEncodingMacRoman);
        CFURLRef url = CFURLCreateWithFileSystemPath(NULL, hfspath, kCFURLHFSPathStyle, 0);
        CFStringRef posixpath = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
        CFStringGetCString(posixpath, path, PATH_MAX, kCFStringEncodingUTF8);
        CFRelease(hfspath);
        CFRelease(url);
        CFRelease(posixpath);
        if (isfolder && path[strlen(path)-1]!='/') { strcat(path, "/"); }	/* converting from HFS loses trailing separator */
    }
#elif IBM
    char *c;
    for (c=path; *c; c++) if (*c=='\\') *c='/';
#endif
}


#define CHECKERR(msg) { ALuint e = alGetError(); if (e != AL_NO_ERROR) return xplog(msg); }

/* Sound initialisation */
float initsoundcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    char buffer[PATH_MAX], *c;
    ALCcontext *old_ctx;

    if (!(old_ctx = alcGetCurrentContext()))
    {
#ifdef MAKECONTEXT	/* We have to make our own device and context on Windows under X-Plane <10.2 */
        if (!(my_dev = alcOpenDevice(NULL))) return xplog("Can't open the default OpenAL device.");
        if (!(my_ctx = alcCreateContext(my_dev, NULL)))
        {
            alcCloseDevice(my_dev);
            my_dev = NULL;
            return xplog("Can't create a sound context.");
        }
        alcMakeContextCurrent(my_ctx);
        CHECKERR("Can't set the sound context.");
        /* Leave context current so other plugins can share it */

# ifdef DEBUG
        {
            ALCint major_version, minor_version;
            const char * al_hw=alcGetString(my_dev,ALC_DEVICE_SPECIFIER);
            const char * al_ex=alcGetString(my_dev,ALC_EXTENSIONS);
            alcGetIntegerv(my_dev, ALC_MAJOR_VERSION, sizeof(major_version), &major_version);
            alcGetIntegerv(my_dev, ALC_MINOR_VERSION, sizeof(minor_version), &minor_version);
            xplog("Created own sound context");
            sprintf(buffer, "OpenAL version   : %d.%d\n", major_version, minor_version); XPLMDebugString(buffer);
            sprintf(buffer, "OpenAL hardware  : %s\n", (al_hw ? al_hw : "(none)")); XPLMDebugString(buffer);
            sprintf(buffer, "OpenAL extensions: %s\n", (al_ex ? al_ex : "(none)")); XPLMDebugString(buffer);
        }
# endif
#else	/* We always expect a sound context on Mac and Linux, and on Windows under X-Plane >=10.2 */
        return xplog("Can't open the sound context.");
#endif
    }

#ifdef DEBUG
    {
        ALfloat v[6];
        xplog("Listener state:");
        alGetListenerf(AL_GAIN, v);
        sprintf(buffer, "gain    : %f\n", v[0]); XPLMDebugString(buffer);
        alGetListenerfv(AL_POSITION, v);
        sprintf(buffer, "position: %f,%f,%f\n", v[0], v[1], v[2]); XPLMDebugString(buffer);
        alGetListenerfv(AL_VELOCITY, v);
        sprintf(buffer, "velocity: %f,%f,%f\n", v[0], v[1], v[2]); XPLMDebugString(buffer);
        alGetListenerfv(AL_ORIENTATION, v);
        sprintf(buffer, "at      : %f,%f,%f\n", v[0], v[1], v[2]); XPLMDebugString(buffer);
        sprintf(buffer, "up      : %f,%f,%f\n", v[3], v[4], v[5]); XPLMDebugString(buffer);
    }
#endif

#if MAC
    {
        /* X-Plane <= 10.2 sets up NULL "at" vector which breaks positional audio on Mac */
        ALfloat current[6];
        const ALfloat def[6] = { 0, 0, -1, 0, 1, 0 };

        alGetListenerfv(AL_ORIENTATION, current);
        if (!current[0] && !current[1] && !current[2])		/* Is X-Plane's listener orientation broken? */
        {
            alListenerfv(AL_ORIENTATION, def);			/* Reset it to OpenAL default values */
            CHECKERR("Can't set the listener orientation.");
        }
    }
#endif

    /* Locate alert sound */
    XPLMGetPluginInfo(XPLMGetMyID(), NULL, buffer, NULL, NULL);
    posixify(buffer);
    if (!(c=strrchr(buffer, '/'))) return xplog("Can't find my plugin");
    *(c+1)='\0';
    if (!strcmp(c-3, "/32/") || !strcmp(c-3, "/64/")) { *(c-2)='\0'; }	/* plugins one level down on some builds, so go up */
    strcat(buffer, "alert.wav");
    if (!(snd_buffer = load_wave(buffer))) return 0;
    CHECKERR("Can't buffer sound data.");

    // Basic initializtion code to play a sound: specify the buffer the source is playing, as well as some
    // sound parameters. This doesn't play the sound - it's just one-time initialization.
    alGenSources(1, &snd_src);
    CHECKERR("Can't create sound source.");
    alSourcei(snd_src, AL_BUFFER, snd_buffer);
    alSourcef(snd_src, AL_PITCH, 1.0f);
    alSourcef(snd_src, AL_GAIN, 1.0f);
    alSourcei(snd_src, AL_SOURCE_RELATIVE, AL_TRUE);	/* Because we're not allowed to manipulate the listener */
    alSourcei(snd_src, AL_LOOPING, AL_TRUE);
    alSourcefv(snd_src, AL_POSITION, zero);
    alSourcefv(snd_src, AL_VELOCITY, zero);
    CHECKERR("Can't set sound source.");

#ifdef TESTSOUND
    ref_test_x = XPLMFindDataRef("sim/aircraft/overflow/jett_X");
    ref_test_y = XPLMFindDataRef("sim/aircraft/overflow/jett_Y");
    ref_test_z = XPLMFindDataRef("sim/aircraft/overflow/jett_Z");
    playalert();
#endif

    return 0;	/* Don't call again */
}

#undef CHECKERR


void closesound()
{
#ifdef MAKECONTEXT
    ALCcontext *old_ctx;
    if (my_ctx)
    {
        old_ctx = alcGetCurrentContext();
        alcMakeContextCurrent(my_ctx);
    }
#endif
    if (alcGetCurrentContext())
    {
        if (snd_src)
        {
            alSourceStop(snd_src);
            alDeleteSources(1, &snd_src);
        }
        if (snd_buffer)
            alDeleteBuffers(1, &snd_buffer);
    }
#ifdef MAKECONTEXT
    if (my_ctx)
    {
        alcMakeContextCurrent(old_ctx);
        alcDestroyContext(my_ctx);
    }
    if (my_dev)
        alcCloseDevice(my_dev);
#endif
}


/* Play our alert sound */
void playalert()
{
    if (!snd_src || !XPLMGetDatai(ref_audio)) return;
    XPLMSetFlightLoopCallbackInterval(alertcallback, -1, 1, NULL);	/* for updating sound position */
    alertcallback(0, 0, 0, NULL);		/* Call it now before we start */
#ifdef MAKECONTEXT
    if (my_ctx)
    {
        ALCcontext *old_ctx = alcGetCurrentContext();
        alcMakeContextCurrent(my_ctx);
        alSourcePlay(snd_src);
        alcMakeContextCurrent(old_ctx);
        return;
    }
#endif
    /* Re-use existing context */
    alSourcePlay(snd_src);
}

void stopalert()
{
    if (!snd_src) return;
    XPLMSetFlightLoopCallbackInterval(alertcallback, 0, 1, NULL);	/* stop */
#ifdef MAKECONTEXT
    if (my_ctx)
    {
        ALCcontext *old_ctx = alcGetCurrentContext();
        alcMakeContextCurrent(my_ctx);
        alSourceStop(snd_src);
        alcMakeContextCurrent(old_ctx);
        return;
    }
#endif
    /* Re-use existing context */
    alSourceStop(snd_src);
}

/* Update sound location relative to viewer */
float alertcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    static int paused = 0;
    float pos_x, pos_y, pos_z;		/* Alert sound's location in OpenGL space */
    XPLMCameraPosition_t camera;	/* X-Plane's camera position and orientation in OpenGL space */
    ALfloat snd_rel[3];			/* Alert sound's position relative to that */
    float x, z, cos_h, sin_h;

#ifdef MAKECONTEXT
    ALCcontext *old_ctx;
    if (my_ctx)
    {
        old_ctx = alcGetCurrentContext();
        alcMakeContextCurrent(my_ctx);
    }
#endif

#ifdef TESTSOUND
    alSource3f(snd_src, AL_POSITION, XPLMGetDataf(ref_test_x), XPLMGetDataf(ref_test_y), XPLMGetDataf(ref_test_z));
# ifdef MAKECONTEXT
    if (my_ctx) alcMakeContextCurrent(old_ctx);
# endif
    return -1;
#endif

    /* Pause sound while sim is paused */
    if (XPLMGetDatai(ref_paused))
    {
        alSourcePause(snd_src);
        paused = 1;
    }
    else if (paused)
    {
        alSourcePlay(snd_src);
        paused = 0;
    }

    /* Use values of current gate from last getgate() call */
    pos_x = gate_x + cosf(gate_h) * (lat + OBJ_X) ;
    pos_y = gate_y + vert + OBJ_Y;
    pos_z = gate_z + sinf(gate_h) * (lat + OBJ_X) ;

    /* Calculate relative location ignoring tilt */
    /* +ve x = source is to the right, +ve z = source is ahead */
    XPLMReadCameraPosition(&camera);
    camera.heading *= D2R;
    cos_h = cosf(camera.heading);
    sin_h = sinf(camera.heading);
    x = pos_x - camera.x;
    z = pos_z - camera.z;
    snd_rel[0] = cos_h * x + sin_h * z;
    snd_rel[1] = pos_y - camera.y;
    snd_rel[2] = sin_h * x - cos_h * z;
    alSourcefv(snd_src, AL_POSITION, snd_rel);

    alSourcef(snd_src, AL_GAIN, XPLMGetDatai(ref_view_external) ? GAIN_EXTERNAL : GAIN_INTERNAL);

#ifdef MAKECONTEXT
    if (my_ctx) alcMakeContextCurrent(old_ctx);
#endif
    return -2;	/* Every other frame should be plenty */
}
