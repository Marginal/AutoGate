/*
 * AutoGate
 * 
 * (c) Jonathan Harris 2006-2013
 * 
 * Licensed under GNU LGPL v2.1.
 */

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#  define _CRT_SECURE_NO_DEPRECATE
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <assert.h>

#ifdef _MSC_VER
#  define PATH_MAX MAX_PATH
#  define strcasecmp(s1, s2) _stricmp(s1, s2)
#endif

#if APL
#  include <OpenAL/al.h>
#  include <OpenAL/alc.h>
#else
#  include <AL/al.h>
#  include <AL/alc.h>
#endif

#define XPLM200	/* Requires X-Plane 9.0 or later */
#include "XPLMCamera.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"


/* Constants */
static const float F2M=0.3048;	/* 1 ft [m] */
static const float D2R=M_PI/180.0;

/* Rest location [m] of bridge door floor in AutoGate.obj */
static const float OBJ_X= -7.5;
static const float OBJ_Y=  4;

/* Capture distances [m] (to door location, not ref point) */
static const float CAP_X = 10;
static const float CAP_Z = 70;	/* (50-80 in Safedock2 flier) */
static const float GOOD_Z= 0.5;
static const float NEW_Z = 20;	/* Max distance to fudge new Ramp Start */

/* DGS distances [m]     (to door location, not ref point) */
static const float AZI_X = 5;	/* Azimuth guidance */
static const float AZI_Z = 50;	/* Azimuth guidance */
static const float REM_Z = 12;	/* Distance remaining */

/* Permissable distance [m] of DGS from gate origin */
static const float DGS_X = 10;
static const float DGS_Z =-50;		/* s2.2 of Safedock Manual says 50m from nose */
static const float DGS_H = 0.2f;	/* ~11 degrees. s2.3 of Safedock Manual says 9 or 12 degrees */

static const float WAITTIME=1;	/* Time to wait before engaging */
static const float DURATION=15;	/* Time to engage/disengage */

static const float POLLTIME=5;	/* How often to check we're still in range of our gate */

/* Alert sound pitch */
static const float GAIN_EXTERNAL = 1.0f;
static const float GAIN_INTERNAL = 0.5f;	/* Quieter in internal views */


/* prototypes */
int xplog(char *msg);
float initsoundcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
float alertcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
void closesound();
void playalert();
void stopalert();
void posixify(char *path);

extern float gate_x, gate_y, gate_z, gate_h;		/* active gate */
extern float lat, vert, moving;
extern XPLMDataRef ref_audio, ref_paused, ref_view_external;
#ifdef DEBUG
extern ALuint snd_src;	/* for drawdebug() */
#endif

/* types */
typedef enum
{
    DISABLED=0, NEWPLANE, IDLE, IDFAIL, TRACK, GOOD, BAD, ENGAGE, DOCKED, DISENGAGE, DISENGAGED
} state_t;

typedef struct {
    const char *key;
    const float lng, lat, vert;
    const int type;
} db_t;

typedef struct {
    const char *key;
    const int type;
} icao_t;
