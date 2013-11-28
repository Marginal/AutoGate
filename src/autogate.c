/*
 * AutoGate
 * 
 * (c) Jonathan Harris 2006-2013
 * 
 * Licensed under GNU LGPL v2.1.
 */

#include "autogate.h"

#if IBM
#  include <windows.h>
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason, LPVOID lpReserved)
{ return TRUE; }
#endif


/* Globals */
static const char pluginName[]="AutoGate";
static const char pluginSig[] ="Marginal.AutoGate";
static const char pluginDesc[]="Manages boarding bridges and DGSs";

static XPLMWindowID windowId = NULL;
static state_t state = DISABLED;
static float timestamp;
static int plane_type;
static float door_x, door_y, door_z;		/* door offset relative to ref point */

/* Datarefs */
static XPLMDataRef ref_plane_x, ref_plane_y, ref_plane_z, ref_plane_psi;
static XPLMDataRef ref_ENGN_running, ref_parkingbrake;
static XPLMDataRef ref_draw_object_x, ref_draw_object_y, ref_draw_object_z, ref_draw_object_psi;
static XPLMDataRef ref_acf_descrip, ref_acf_icao;
static XPLMDataRef ref_acf_cg_y, ref_acf_cg_z;
static XPLMDataRef ref_acf_door_x, ref_acf_door_y, ref_acf_door_z;
static XPLMDataRef ref_total_running_time_sec;

/* Published DataRefs */
static XPLMDataRef ref_vert, ref_lat, ref_moving;
static XPLMDataRef ref_status, ref_id1, ref_id2, ref_id3, ref_id4, ref_lr, ref_track;
static XPLMDataRef ref_azimuth, ref_distance, ref_distance2;

/* Published DataRef values */
float lat, vert, moving;
static float status, id1, id2, id3, id4, lr, track;
static float azimuth, distance, distance2;

/* Internal state */
static float last_x, last_y, last_z;		/* last object examined */
static float last_update=0;			/* and the time we examined it */
float gate_x, gate_y, gate_z, gate_h;		/* active gate */
static float dgs_x, dgs_y, dgs_z;		/* active DGS */


/* In this file */
static float newplanecallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
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

/* Known planes */
static const db_t planedb[]={/* lng   lat  vert  type */
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

/* Known planes */
static const icao_t icaodb[]={
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


PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    sprintf(outName, "%s v%.2f", pluginName, VERSION);
    strcpy(outSig,  pluginSig);
    strcpy(outDesc, pluginDesc);

    /* Datarefs */
    ref_plane_x        =XPLMFindDataRef("sim/flightmodel/position/local_x");
    ref_plane_y        =XPLMFindDataRef("sim/flightmodel/position/local_y");
    ref_plane_z        =XPLMFindDataRef("sim/flightmodel/position/local_z");
    ref_plane_psi      =XPLMFindDataRef("sim/flightmodel/position/psi");
    ref_ENGN_running   =XPLMFindDataRef("sim/flightmodel/engine/ENGN_running");
    ref_parkingbrake   =XPLMFindDataRef("sim/flightmodel/controls/parkbrake");
    ref_draw_object_x  =XPLMFindDataRef("sim/graphics/animation/draw_object_x");
    ref_draw_object_y  =XPLMFindDataRef("sim/graphics/animation/draw_object_y");
    ref_draw_object_z  =XPLMFindDataRef("sim/graphics/animation/draw_object_z");
    ref_draw_object_psi=XPLMFindDataRef("sim/graphics/animation/draw_object_psi");
    ref_acf_descrip    =XPLMFindDataRef("sim/aircraft/view/acf_descrip");
    ref_acf_icao       =XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
    ref_acf_cg_y       =XPLMFindDataRef("sim/aircraft/weight/acf_cgY_original");
    ref_acf_cg_z       =XPLMFindDataRef("sim/aircraft/weight/acf_cgZ_original");
    ref_acf_door_x     =XPLMFindDataRef("sim/aircraft/view/acf_door_x");
    ref_acf_door_y     =XPLMFindDataRef("sim/aircraft/view/acf_door_y");
    ref_acf_door_z     =XPLMFindDataRef("sim/aircraft/view/acf_door_z");
    ref_total_running_time_sec=XPLMFindDataRef("sim/time/total_running_time_sec");
    ref_audio          =XPLMFindDataRef("sim/operation/sound/sound_on");
    ref_paused         =XPLMFindDataRef("sim/time/paused");
    ref_view_external  =XPLMFindDataRef("sim/graphics/view/view_is_external");

    /* Published Datarefs */
    ref_vert     =floatref("marginal.org.uk/autogate/vert", getgate, &vert);
    ref_lat      =floatref("marginal.org.uk/autogate/lat",  getgate, &lat);
    ref_moving   =floatref("marginal.org.uk/autogate/moving", getgate, &moving);

    ref_status   =floatref("marginal.org.uk/dgs/status",    getdgs, &status);
    ref_id1      =floatref("marginal.org.uk/dgs/id1",       getdgs, &id1);
    ref_id2      =floatref("marginal.org.uk/dgs/id2",       getdgs, &id2);
    ref_id3      =floatref("marginal.org.uk/dgs/id3",       getdgs, &id3);
    ref_id4      =floatref("marginal.org.uk/dgs/id4",       getdgs, &id4);
    ref_lr       =floatref("marginal.org.uk/dgs/lr",        getdgs, &lr);
    ref_track    =floatref("marginal.org.uk/dgs/track",     getdgs, &track);
    ref_azimuth  =floatref("marginal.org.uk/dgs/azimuth",   getdgs, &azimuth);
    ref_distance =floatref("marginal.org.uk/dgs/distance",  getdgs, &distance);
    ref_distance2=floatref("marginal.org.uk/dgs/distance2", getdgs, &distance2);

#ifdef DEBUG
    windowId = XPLMCreateWindow(10, 750, 310, 650, 1, drawdebug, NULL, NULL, NULL);
#endif
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);			/* Get paths in posix format under X-Plane 10+ */
    XPLMRegisterFlightLoopCallback(initsoundcallback, -1, NULL);	/* Deferred initialisation */
    XPLMRegisterFlightLoopCallback(alertcallback, 0, NULL);
    XPLMRegisterFlightLoopCallback(newplanecallback, 0, NULL);		/* For checking gate alignment on new location */

    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    if (windowId) XPLMDestroyWindow(windowId);
    XPLMUnregisterFlightLoopCallback(newplanecallback, NULL);
    XPLMUnregisterFlightLoopCallback(alertcallback, NULL);
    XPLMUnregisterFlightLoopCallback(initsoundcallback, NULL);
    closesound();
}

PLUGIN_API int XPluginEnable(void)
{
    newplane();
    return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    resetidle();
    state=DISABLED;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho, int inMessage, void *inParam)
{
    if (state!=DISABLED && inMessage==XPLM_MSG_AIRPORT_LOADED)
        newplane();
}

/* Reset new plane state after drawing */
static float newplanecallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    if (state == NEWPLANE) state = IDLE;
    return 0;	/* Don't call again */
}

static void newplane(void)
{
    char acf_descrip[129];
    char acf_icao[41];
    int i;

    resetidle();
    acf_descrip[128]=0;		/* Not sure if XPLMGetDatab NULL terminates */
    acf_icao[40]=0;		/* Not sure if XPLMGetDatab NULL terminates */

    /* Find ICAO code */
    plane_type=15;	/* unknown */
    if (ref_acf_icao!=NULL && XPLMGetDatab(ref_acf_icao, acf_icao, 0, 40))
        for (i=0; i<sizeof(icaodb)/sizeof(icao_t); i++)
            if (!strncmp(acf_icao, icaodb[i].key, strlen(icaodb[i].key)))
            {
                plane_type=icaodb[i].type;
                break;
            }

    if (ref_acf_door_x!=NULL)
    {
        door_x=XPLMGetDataf(ref_acf_door_x);	/* 0 if unset */
        door_y=XPLMGetDataf(ref_acf_door_y);
        door_z=XPLMGetDataf(ref_acf_door_z);
    }
    else
        door_x=door_y=door_z=0;

    if ((!door_x || plane_type==15) &&
        XPLMGetDatab(ref_acf_descrip, acf_descrip, 0, 128))
        /* Try table */
        for (i=0; i<sizeof(planedb)/sizeof(db_t); i++)
            if (strstr(acf_descrip, planedb[i].key) && (door_x || planedb[i].lat))
            {
                plane_type=planedb[i].type;
                if (!door_x)
                {
                    door_x = F2M * planedb[i].lat;
                    door_y = F2M * (planedb[i].vert - XPLMGetDataf(ref_acf_cg_y));	/* Adjust relative to static cog */
                    door_z = F2M * (planedb[i].lng  - XPLMGetDataf(ref_acf_cg_z));	/* Adjust relative to static cog */
                }
                break;
            }

    /* If have data, check for alignment at a gate during next frame */
    state = door_x ? NEWPLANE : IDFAIL;
}

static void resetidle(void)
{
    state=IDLE;
    gate_x=gate_y=gate_z=gate_h=0;
    dgs_x=dgs_y=dgs_z=0;
    vert=lat=moving=0;
    status=id1=id2=id3=id4=lr=track=0;
    azimuth=distance=distance2=0;
    stopalert();
}

static float getgate(XPLMDataRef inRefcon)
{
    float now, object_x, object_y, object_z, object_h;
    float local_x, local_y, local_z;
	
    if (state <= IDFAIL) return 0;

    object_x=XPLMGetDataf(ref_draw_object_x);
    object_y=XPLMGetDataf(ref_draw_object_y);
    object_z=XPLMGetDataf(ref_draw_object_z);
    if (state>IDLE && (gate_x!=object_x || gate_y!=object_y || gate_z!=object_z))
        /* We're tracking and it's not by this gate */
        return 0;

    now=XPLMGetDataf(ref_total_running_time_sec);
    if (last_update==now && last_x==object_x && last_y==object_y && last_z==object_z)
        /* Same rendering pass and object as last calculation */
        return *(float*)inRefcon;
    else
    {
        last_update=now;
        last_x=object_x;
        last_y=object_y;
        last_z=object_z;
    }

    object_h=XPLMGetDataf(ref_draw_object_psi) * D2R;
    localpos(object_x, object_y, object_z, object_h, &local_x, &local_y, &local_z);

    if (fabsf(local_x)>CAP_X || local_z<DGS_Z || local_z>CAP_Z)
    {
        /* Not in range of this gate */
        if (gate_x==object_x && gate_y==object_y && gate_z==object_z)
            resetidle();	/* Just gone out of range of the tracking gate */

        else if (state == NEWPLANE)
            XPLMSetFlightLoopCallbackInterval(newplanecallback, -1, 1, NULL);	/* Reset newplane state before next frame */

        return 0;
    }
	    
    if (gate_x!=object_x || gate_y!=object_y || gate_z!=object_z)
    {
        /* Just come into range */
        if (state == NEWPLANE && fabsf(local_z) < NEW_Z)
        {
            /* Fudge plane's position to line up with this gate */
            float object_hcos, object_hsin;
            int running;

            object_hcos = cosf(object_h);
            object_hsin = sinf(object_h);
            XPLMSetDataf(ref_plane_x, XPLMGetDataf(ref_plane_x) + local_z * object_hsin - local_x * object_hcos);
            XPLMSetDataf(ref_plane_z, XPLMGetDataf(ref_plane_z) - local_z * object_hcos - local_x * object_hsin);
            localpos(object_x, object_y, object_z, object_h, &local_x, &local_y, &local_z);	/* recalc */

            XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);
            running |= (XPLMGetDataf(ref_parkingbrake) < 0.5f);
            state = running ? TRACK : DOCKED;
        }
        else
        {
            /* Approaching gate */
            state = TRACK;
        }
        gate_x=object_x;
        gate_y=object_y;
        gate_z=object_z;
        gate_h=object_h;
    }
	
    updaterefs(now, local_x, local_y, local_z);
    return *(float*)inRefcon;
}


static float getdgs(XPLMDataRef inRefcon)
{
    float now, object_x, object_y, object_z;
    float local_x, local_y, local_z;

    if (state <= IDLE) return 0;	/* Only interested in DGSs if we're in range of a gate */

    object_x=XPLMGetDataf(ref_draw_object_x);
    object_y=XPLMGetDataf(ref_draw_object_y);
    object_z=XPLMGetDataf(ref_draw_object_z);

    if (!(dgs_x || dgs_y || dgs_z))
    {
        /* Haven't yet identified the active dgs */
        float x, z;
        float gate_hcos, gate_hsin;
		
        /* Location of this dgs in the active gate's space */
        gate_hcos = cosf(gate_h);
        gate_hsin = sinf(gate_h);
        x=gate_hcos*(object_x-gate_x) + gate_hsin*(object_z-gate_z);
        z=gate_hcos*(object_z-gate_z) - gate_hsin*(object_x-gate_x);
        if (fabsf(x)<=DGS_X && z<=0 && z>=DGS_Z)
        {
            dgs_x=object_x;
            dgs_y=object_y;
            dgs_z=object_z;
        }
        else
            return 0;
    }
    else if (dgs_x!=object_x || dgs_y!=object_y || dgs_z!=object_z)
        /*  Have identified the active dgs and this isn't it */
        return 0;

    now=XPLMGetDataf(ref_total_running_time_sec);
    if (last_update==now && last_x==object_x && last_y==object_y && last_z==object_z)
        /* Same rendering pass and object as last calculation */
        return *(float*)inRefcon;
    else
    {
        last_update=now;
        last_x=object_x;
        last_y=object_y;
        last_z=object_z;
    }
	
    /* Re-calculate plane location - can't rely on values from getgate() since that will not be being called if gate no longer in view */
    localpos(gate_x, gate_y, gate_z, gate_h, &local_x, &local_y, &local_z);

    updaterefs(now, local_x, local_y, local_z);
    return *(float*)inRefcon;
}


/* Calculate location of plane's centreline opposite door in this object's space */
static void localpos(float object_x, float object_y, float object_z, float object_h, float *local_x, float *local_y, float *local_z)
{
    float plane_x, plane_y, plane_z, plane_h;
    float x, y, z;
    float object_hcos, object_hsin;

    plane_x=XPLMGetDataf(ref_plane_x);
    plane_y=XPLMGetDataf(ref_plane_y);
    plane_z=XPLMGetDataf(ref_plane_z);
    plane_h=XPLMGetDataf(ref_plane_psi) * D2R;

    /* Location of plane's centreline opposite door */
    /* Calculation assumes plane is horizontal */
    x=plane_x-door_z*sinf(plane_h);
    y=plane_y+door_y;
    z=plane_z+door_z*cosf(plane_h);

    /* Location of centreline opposite door in this gate's space */
    object_hcos = cosf(object_h);
    object_hsin = sinf(object_h);
    *local_x=object_hcos*(x-object_x)+object_hsin*(z-object_z);
    *local_y=y-object_y;
    *local_z=object_hcos*(z-object_z)-object_hsin*(x-object_x);
}


/* Update published data used by gate and dgs */
static void updaterefs(float now, float local_x, float local_y, float local_z)
{
    int running;
    int locgood=(fabsf(local_x)<=AZI_X && fabsf(local_z)<=GOOD_Z);
	
    XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);
    running |= (XPLMGetDataf(ref_parkingbrake) < 0.5f);

    status=id1=id2=id3=id4=lr=track=0;
    azimuth=distance=distance2=0;

    switch (state)
    {
    case TRACK:
        if (locgood)
        {
            state=GOOD;
            timestamp=now;
        }
        else if (local_z<-GOOD_Z)
            state=BAD;
        else
        {
            status=1;	/* plane id */
            if (plane_type<4)
                id1=plane_type+1;
            else if (plane_type<8)
                id2=plane_type-3;
            else if (plane_type<12)
                id3=plane_type-7;
            else
                id4=plane_type-11;
            if (local_z-GOOD_Z > AZI_Z ||
                fabsf(local_x) > AZI_X)
                track=1;	/* lead-in only */
            else
            {
                distance=((float)((int)((local_z - GOOD_Z)*2))) / 2;
                azimuth=((float)((int)(local_x*2))) / 2;
                if (azimuth>4)	azimuth=4;
                if (azimuth<-4) azimuth=-4;
                if (azimuth<=-0.5f)
                    lr=1;
                else if (azimuth>=0.5f)
                    lr=2;
                else
                    lr=0;
                if (local_z-GOOD_Z <= REM_Z/2)
                {
                    track=3;
                    distance2=distance;
                }
                else
                {
                    if (local_z-GOOD_Z > REM_Z)
                        /* azimuth only */
                        distance=REM_Z;
                    track=2;
                    distance2=distance - REM_Z/2;
                }
            }
        }
        break;

    case GOOD:
        if (!locgood)
            state=TRACK;
        else if (running)
        {
            /* Stop */
            lr=3;
            status=2;
        }
        else
        {
            state=ENGAGE;
            timestamp=now;
        }
        break;

    case BAD:
        if (local_z>=-GOOD_Z)
            state=TRACK;
        else
        {
            /* Too far */
            lr=3;
            status=4;
        }
        break;

    case ENGAGE:
        lr=3;
        if (running)
        {
            /* abort - reverse animation */
            state=DISENGAGE;
            timestamp=now-(timestamp+WAITTIME+DURATION-now);
        }
        else if (now>timestamp+WAITTIME+DURATION)
            state=DOCKED;
        else if (now>timestamp+WAITTIME)
        {
            float ratio=(now-(timestamp+WAITTIME))/DURATION;
            status=3;	/* OK */
            lat =(door_x-OBJ_X) * ratio;
            vert=(local_y-OBJ_Y) * ratio;
            if (!moving) playalert();
            moving=1;
        }
        else
            status=2;	/* Stop */
        break;

    case DOCKED:
        /* Blank */
        if (running)
        {
            state=DISENGAGE;
            timestamp=now;
        }
        else
        {
            lat =door_x-OBJ_X;
            vert=local_y-OBJ_Y;
            moving=0;
            stopalert();
        }
        break;

    case DISENGAGE:
        /* Blank */
        if (now>timestamp+DURATION)
        {
            state=DISENGAGED;
            lat=vert=moving=0;
            stopalert();
        }
        else
        {
            float ratio=1 - (now-timestamp)/DURATION;
            lat =(door_x-OBJ_X) * ratio;
            vert=(local_y-OBJ_Y) * ratio;
            if (!moving) playalert();
            moving=1;
        }
        break;

    case DISENGAGED:
        /* Blank */
        if (local_z-GOOD_Z > AZI_Z || fabsf(local_x) > AZI_X)
            /* Go back to lead-in */
            state=TRACK;
        break;

    default:
        /* Shouldn't be here if state<=IDLE */
        assert(0);
    }
}

static XPLMDataRef floatref(char *inDataName, XPLMGetDataf_f inReadFloat, float *inRefcon)
{
    return XPLMRegisterDataAccessor(inDataName, xplmType_Float, 0, NULL, NULL, inReadFloat, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, inRefcon, 0);
}
	

/* Log to Log.txt. Returns 0 because that's a failure return code from most XP entry points */
int xplog(char *msg)
{
    XPLMDebugString("AutoGate: ");
    XPLMDebugString(msg);
    XPLMDebugString("\n");
    return 0;
}


#ifdef DEBUG
static void drawdebug(XPLMWindowID inWindowID, void *inRefcon)
{
    char buf[128];
    int left, top, right, bottom;
    float color[] = { 1.0, 1.0, 1.0 };	/* RGB White */
    float pos[3], gain;
    int running;
    XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);
    running |= (XPLMGetDataf(ref_parkingbrake) < 0.5);

    XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
    XPLMDrawTranslucentDarkBox(left, top, right, bottom);

    sprintf(buf, "State: %d %d %d", state, plane_type, running);
    XPLMDrawString(color, left + 5, top - 10, buf, 0, xplmFont_Basic);
    sprintf(buf, "Door : %10.3f %10.3f %10.3f",       XPLMGetDataf(ref_acf_door_x), XPLMGetDataf(ref_acf_door_y), XPLMGetDataf(ref_acf_door_z));
    XPLMDrawString(color, left + 5, top - 30, buf, 0, xplmFont_Basic);
    sprintf(buf, "Plane: %10.3f %10.3f %10.3f %6.2f", XPLMGetDataf(ref_plane_x), XPLMGetDataf(ref_plane_y), XPLMGetDataf(ref_plane_z), XPLMGetDataf(ref_plane_psi));
    XPLMDrawString(color, left + 5, top - 40, buf, 0, xplmFont_Basic);
    sprintf(buf, "Gate : %10.3f %10.3f %10.3f %6.2f", gate_x, gate_y, gate_z, gate_h/D2R);
    XPLMDrawString(color, left + 5, top - 50, buf, 0, xplmFont_Basic);
    sprintf(buf, "DGS  : %10.3f %10.3f %10.3f", dgs_x, dgs_y, dgs_z);
    XPLMDrawString(color, left + 5, top - 60, buf, 0, xplmFont_Basic);
    sprintf(buf, "Time : %10.3f", timestamp);
    XPLMDrawString(color, left + 5, top - 70, buf, 0, xplmFont_Basic);
    sprintf(buf, "Data : %6.3f %6.3f %1.0f %1.0f %1.0f %1.0f %1.0f %1.0f %1.0f %4.1f %4.1f %4.1f",
            lat, vert,
            status, id1, id2, id3, id4,
            lr, track,
            azimuth, distance, distance2);
    XPLMDrawString(color, left + 5, top - 80, buf, 0, xplmFont_Basic);
    alGetSourcefv(snd_src, AL_POSITION, pos);
    alGetSourcefv(snd_src, AL_GAIN, &gain);
    sprintf(buf, "Sound: %10.3f %10.3f %10.3f %6.2f", pos[0], pos[1], pos[2], gain);
    XPLMDrawString(color, left + 5, top - 90, buf, 0, xplmFont_Basic);
}				    
#endif
