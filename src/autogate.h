/*
 * AutoGate
 * 
 * (c) Jonathan Harris 2006,2008,2012
 * 
 */

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#  define _CRT_SECURE_NO_DEPRECATE
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"


const char pluginName[]="AutoGate";
const char pluginSig[] ="Marginal.AutoGate";
const char pluginDesc[]="Manages boarding bridges and DGSs";

/* Constants */
const float F2M=0.3048;	/* 1 ft [m] */
const float D2R=M_PI/180.0;

/* Rest location [m] of bridge door floor in AutoGate.obj */
const float OBJ_X= -7.5;
const float OBJ_Y=  4;

/* Capture distances [m] (to door location, not ref point) */
const float CAP_X = 10;
const float CAP_Z = 70;	/* (50-80 in Safedock2 flier) */
const float GOOD_Z= 0.5;

/* DGS distances [m]     (to door location, not ref point) */
const float AZI_X = 5;	/* Azimuth guidance */
const float AZI_Z = 50;	/* Azimuth guidance */
const float REM_Z = 12;	/* Distance remaining */

/* Permissable distance [m] of DGS from gate origin */
const float DGS_X = 10;
const float DGS_Z =-30;	/* (6-50 in Safedock2 flier) */

const float WAITTIME=1;	/* Time to wait before engaging */
const float DURATION=5;	/* Time to engage/disengage */


/* prototypes */
static void newplane(void);
static void resetidle(void);

static XPLMDataRef floatref(char*, XPLMGetDataf_f, float*);
static float getgate(XPLMDataRef);
static float getdgs(XPLMDataRef);

static void localpos(float, float, float, float, float *, float *, float *);
static void updaterefs(float, float, float, float);

#ifdef DEBUG
static void drawdebug(XPLMWindowID, void *);
#endif


/* types */
typedef enum
{
    DISABLED=0, IDFAIL, IDLE, TRACK, GOOD, BAD, ENGAGE, DOCKED, DISENGAGE, DISENGAGED
} state_t;

typedef struct {
    const char *key;
    const float lng, lat, vert;
    const int type;
} db_t;

/* Known planes */
const db_t planedb[]={/* lng   lat  vert  type */   
    {"A300",	21.0, -8.0, -1.0,  0},
    {"A310",	18.0, -8.0, -1.0,  1},
    {"A318",	16.7, -6.0, -1.5,  2},
    {"A319",	21.7, -6.0, -1.5,  2},	/* xpfw */
    {"A320-200",16.7, -6.0, -1.5,  2},	/* xpfw */
    {"A320",	18.3, -6.0, -1.4,  2},
    {"A321-200", 2.5, -6.0, -1.5,  2},	/* xpfw */
    {"A321",	17.5, -6.0, -1.4,  2},
    {"A330",	   0,    0,    0,  3},
    {"A340",	19.6, -8.0, -1.2,  4},
    {"A350",	   0,    0,    0,  5},
    {"A380",	23.0, -9.7, -6.0,  6}, /* first door */
    //	{"A380",	56.5, -11,  -6.0,  6}, /* second door */
    {"717",	   0,    0,    0,  7},
    {"737-700",	15.0, -6.0, -1.2,  8},
    {"737 800",	16.8, -5.2, -1.5,  8},	/* xpfw b26*/
    {"737-800",	17.4, -6.0, -1.4,  8},
    {"738", 	17.4, -6.0, -1.4,  8},
    {"737", 	17.4, -6.0, -1.4,  8},
    {"747 400", 30.9, -9.6, -2.2,  9},	/* xpfw */
    {"747", 	31.8, -9.4, -3.8,  9},	/* XP840b6 first door */
    {"757",	   0,    0,    0, 10},
    {"767", 	18.2, -7.5, -1.5, 11},
    {"777 200",	39.8, -9.0, -1.4, 12},	/* xpfw 777-200 ER & LR - note nose is at 18.39 */
    {"777", 	21.7, -9.0, -2.4, 12},
    {"787",	   0,    0,    0, 13},
    {"RJ70",	 6.6, -5.3, -3.0, 14},
    {"RJ 70",	 6.6, -5.3, -3.0, 14},
    {"RJ85",	 5.3, -5.7, -2.0, 14},
    {"RJ 85",	 5.3, -5.7, -2.0, 14},
    {"RJ100",	 0.9, -5.7, -2.0, 14},
    {"RJ 100",	 0.9, -5.7, -2.0, 14},
    {"md-11",	15.9, -7.6, -1.6, 15},
    {"MD11",	17.0, -7.7, -1.4, 15},
};


typedef struct {
    const char *key;
    const int type;
} icao_t;

/* Known planes */
const icao_t icaodb[]={
    {"A30",  0},
    {"A3ST", 0},
    {"A318", 2},
    {"A319", 2},
    {"A32",  2},
    {"A310", 1},	/* Note after A318/A319 */
    {"A33",  3},
    {"A34",  4},
    {"A35",  5},
    {"A38",  6},
    {"B71",  7},
    {"MD8",  7},
    {"MD9",  7},
    {"B73",  8},
    {"E737", 8},
    {"B74",  9},
    {"BSCA", 9},
    {"B75",  10},
    {"B76",  11},
    {"E767", 11},
    {"B77",  12},
    {"B78",  13},
    {"RJ",   14},
    {"B46",  14},
};
