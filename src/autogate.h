/*
 * AutoGate
 * 
 * (c) Jonathan Harris 2006
 * 
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"


const char pluginName[]="AutoGate v1.10";
const char pluginSig[] ="Marginal.AutoGate";
const char pluginDesc[]="Manages boarding bridges and DGSs";

/* Constants */
const float F2M=0.3048;	/* 1 ft [m] */
const float D2R=3.14159265358979323846/180.0;

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
void newplane(void);
void resetidle(void);

XPLMDataRef floatref(char*, XPLMGetDataf_f, float*);
float getgate(XPLMDataRef);
float getdgs(XPLMDataRef);

int updategate(void);
int updatedgs(void);
void updaterefs(void);

void drawdebug(XPLMWindowID, void *);


/* types */
typedef enum { DISABLED=0, IDFAIL, IDLE, TRACK, GOOD, BAD,
	       ENGAGE, DOCKED, DISENGAGE, DISENGAGED }
	state_t;

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
	{"A320-200",	16.7, -6.0, -1.5,  2},	/* xpfw */
	{"A320",	18.3, -6.0, -1.4,  2},
	{"A321-200",	 2.5, -6.0, -1.5,  2},	/* xpfw */
	{"A321",	17.5, -6.0, -1.4,  2},
	{"A330",	   0,    0,    0,  3},
	{"A340",	19.6, -8.0, -1.2,  4},
	{"A350",	   0,    0,    0,  5},
	{"A380",	23.0, -9.7, -6.0,  6}, /* first door */
/*	{"A380",	56.5, -11,  -6.0,  6}, /* second door */
	{"717",		   0,    0,    0,  7},
	{"737-700",	15.0, -6.0, -1.2,  8},
	{"737 800",	16.8, -5.2, -1.5,  8},	/* xpfw b26*/
	{"737-800",	17.4, -6.0, -1.4,  8},
	{"738", 	17.4, -6.0, -1.4,  8},
	{"737", 	17.4, -6.0, -1.4,  8},
	{"747 400", 	30.9, -9.6, -2.2,  9},	/* xpfw */
	{"747", 	31.8, -9.4, -3.8,  9},	/* XP840b6 first door */
	{"757",		   0,    0,    0, 10},
	{"767", 	18.2, -7.5, -1.5, 11},
	{"777 200",	39.8, -9.0, -1.4, 12},	/* xpfw 777-200 ER & LR - note nose is at 18.39 */
	{"777", 	21.7, -9.0, -2.4, 12},
	{"787",		   0,    0,    0, 13},
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
	{"A300", 0},
	{"A306", 0},
	{"A30B", 0},
	{"A3ST", 0},
	{"A310", 1},
	{"A318", 2},
	{"A319", 2},
	{"A320", 2},
	{"A321", 2},
	{"A330", 3},
	{"A332", 3},
	{"A333", 3},
	{"A340", 4},
	{"A342", 4},
	{"A343", 4},
	{"A345", 4},
	{"A346", 4},
	{"A350", 5},
	{"A358", 5},
	{"A359", 5},
	{"A380", 6},
	{"A388", 6},
	{"B717", 7},
	{"B712", 7},
	{"MD87", 7},
	{"MD95", 7},
	{"B737", 8},
	{"B731", 8},
	{"B732", 8},
	{"B733", 8},
	{"B734", 8},
	{"B735", 8},
	{"B736", 8},
	{"B738", 8},
	{"B739", 8},
	{"E737", 8},
	{"B747", 9},
	{"B741", 9},
	{"B742", 9},
	{"B743", 9},
	{"B744", 9},
	{"B74D", 9},
	{"B74S", 9},
	{"B74R", 9},
	{"BSCA", 9},
	{"B757", 10},
	{"B752", 10},
	{"B753", 10},
	{"B767", 11},
	{"B762", 11},
	{"B763", 11},
	{"B764", 11},
	{"E767", 11},
	{"B777", 12},
	{"B772", 12},
	{"B773", 12},
	{"B787", 13},
	{"B783", 13},
	{"B788", 13},
	{"B789", 13},
	{"RJ70", 14},
	{"RJ85", 14},
	{"RJ1H", 14},
	{"B461", 14},
	{"B462", 14},
	{"B463", 14},
};

