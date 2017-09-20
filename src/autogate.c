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
static XPLMDataRef ref_status, ref_icao, ref_id1, ref_id2, ref_id3, ref_id4, ref_lr, ref_track;
static XPLMDataRef ref_azimuth, ref_distance, ref_distance2;

/* Published DataRef values */
float lat, vert, moving;
static int status, id1, id2, id3, id4, lr, track;
static int icao[4];
static float azimuth, distance, distance2;

/* Internal state */
static float last_gate_x, last_gate_y, last_gate_z;	/* last gate object examined */
static float last_gate_update = 0;		/* and the time we examined it */
float gate_x, gate_y, gate_z, gate_h;		/* active gate */
static float gate_update=0;			/* and the time we examined it */
int gate_autogate;				/* active gate is an AutoGate, not a standalone dummy */

static float last_dgs_x, last_dgs_y, last_dgs_z;	/* last dgs object examined */
static float last_dgs_update = 0;		/* and the time we examined it */
static float dgs_x, dgs_y, dgs_z;		/* active DGS */


/* In this file */
static float newplanecallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
static void newplane(void);
static void resetidle(void);

static XPLMDataRef floatref(char*, XPLMGetDataf_f, float*);
static XPLMDataRef intref(char*, XPLMGetDatai_f, int*);
static XPLMDataRef intarrayref(char *, XPLMGetDatavi_f, int*);
static float getgatefloat(XPLMDataRef);
static int getdgs(void);
static float getdgsfloat(XPLMDataRef);
static int getdgsint(XPLMDataRef);
static int getdgsicao(XPLMDataRef, int*, int, int);

static int localpos(float, float, float, float, float *, float *, float *);
static void updaterefs(float, float, float, float);

#ifdef DEBUG
static void drawdebug(XPLMWindowID, void *);
#endif

/* Known plane descriptions */
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

/* Known plane ICAOs */
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

/* Canonical ICAOs for known planes */
static char canonical[16][5] = {
    "A300",
    "A310",
    "A320",
    "A330",
    "A340",
    "A350",
    "A380",
    "B717",
    "B737",
    "B747",
    "B757",
    "B767",
    "B777",
    "B787",
    "AVRO",
    "APRH",
};

/* Autogate X-Plane Menu*/

int g_menu_container_idx; // The index of our menu item in the Plugins menu
int g_automenu;
int g_menu_connect;
int g_automaticBehavior;
XPLMMenuID g_menu_id; // The menu container we'll append all our menu items to

XPLMCommandRef ToggleConnectionCmd = NULL;	//  Our two custom commands
XPLMCommandRef AutomaticAutoGateCmd = NULL;

PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
    char buffer[PATH_MAX], *c;

    sprintf(outName, "%s v%.2f", pluginName, VERSION);
    strcpy(outSig,  pluginSig);
    strcpy(outDesc, pluginDesc);

    /* Refuse to initialise if Fat plugin has been moved out of its folder */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);			/* Get paths in posix format under X-Plane 10+ */
    XPLMGetPluginInfo(XPLMGetMyID(), NULL, buffer, NULL, NULL);
    posixify(buffer);
    if ((c = strrchr(buffer, '/')))
    {
        if (!strcmp(c-3, "/32") || !strcmp(c-3, "/64"))
            *(c-3) = '\0';		/* plugins one level down on some builds, so go up */
        else
            *c = '\0';			/* strip .xpl file */
        if ((c = strrchr(buffer, '/')))
            *c = '\0';			/* strip Fat plugin folder */
    }
    if (!c ||
        !(c = strrchr(buffer, '/')) ||
        strcasecmp(c, "/plugins"))
    {
        XPLMDebugString("AutoGate: Can't initialise - plugin has been moved out of its folder!\n");
        return 0;
    }

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
    ref_vert     =floatref("marginal.org.uk/autogate/vert", getgatefloat, &vert);
    ref_lat      =floatref("marginal.org.uk/autogate/lat",  getgatefloat, &lat);
    ref_moving   =floatref("marginal.org.uk/autogate/moving", getgatefloat, &moving);

    ref_status   =  intref("marginal.org.uk/dgs/status",    getdgsint, &status);
    ref_icao  =intarrayref("marginal.org.uk/dgs/icao",      getdgsicao, icao);
    ref_id1      =  intref("marginal.org.uk/dgs/id1",       getdgsint, &id1);
    ref_id2      =  intref("marginal.org.uk/dgs/id2",       getdgsint, &id2);
    ref_id3      =  intref("marginal.org.uk/dgs/id3",       getdgsint, &id3);
    ref_id4      =  intref("marginal.org.uk/dgs/id4",       getdgsint, &id4);
    ref_lr       =  intref("marginal.org.uk/dgs/lr",        getdgsint, &lr);
    ref_track    =  intref("marginal.org.uk/dgs/track",     getdgsint, &track);
    ref_azimuth  =floatref("marginal.org.uk/dgs/azimuth",   getdgsfloat, &azimuth);
    ref_distance =floatref("marginal.org.uk/dgs/distance",  getdgsfloat, &distance);
    ref_distance2=floatref("marginal.org.uk/dgs/distance2", getdgsfloat, &distance2);

#ifdef DEBUG
    windowId = XPLMCreateWindow(10, 750, 310, 610, 1, drawdebug, NULL, NULL, NULL);
#endif
    XPLMRegisterFlightLoopCallback(initsoundcallback, -1, NULL);	/* Deferred initialisation */
    XPLMRegisterFlightLoopCallback(alertcallback, 0, NULL);
    XPLMRegisterFlightLoopCallback(newplanecallback, 0, NULL);		/* For checking gate alignment on new location */


   	/* Add menu */

	g_menu_container_idx = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "AutoGate", 0, 0);
	g_menu_id = XPLMCreateMenu("AutoGate", XPLMFindPluginsMenu(), g_menu_container_idx, menu_handler, NULL);
	g_menu_connect = XPLMAppendMenuItem(g_menu_id, "Connect/Disconnect Gate", (void *)"Menu Item 1", 1);
	XPLMAppendMenuSeparator(g_menu_id);
	g_automenu = XPLMAppendMenuItem(g_menu_id, "Automatic Autogate", (void *)"Menu Item 2", 1);

	// load previous state
	load_settings_from_file();
	XPLMMenuCheck check = xplm_Menu_Checked;
	int enableCon = 0;
	if (!g_automaticBehavior)
	{
		check = xplm_Menu_Unchecked;
		enableCon = 1;
	}
	
	XPLMCheckMenuItem(g_menu_id, g_automenu, check);
	XPLMEnableMenuItem(g_menu_id, g_menu_connect, enableCon);
	

	AutomaticAutoGateCmd = XPLMCreateCommand("AutoGate/toggle_automatic", "Toggle automatic behavior");
	ToggleConnectionCmd = XPLMCreateCommand("AutoGate/connect_disconnect", "Connect/Disconnect jetway");

	XPLMRegisterCommandHandler(AutomaticAutoGateCmd, automatic_behavior_handler, 1, NULL);
	XPLMRegisterCommandHandler(ToggleConnectionCmd, connection_handler, 1, NULL); 

    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    if (windowId) XPLMDestroyWindow(windowId);
    XPLMUnregisterFlightLoopCallback(newplanecallback, NULL);
    XPLMUnregisterFlightLoopCallback(alertcallback, NULL);
    XPLMUnregisterFlightLoopCallback(initsoundcallback, NULL);
    closesound();
    
	XPLMDestroyMenu(g_menu_id);
    
    XPLMUnregisterCommandHandler(AutomaticAutoGateCmd, automatic_behavior_handler, 1, NULL);
    XPLMUnregisterCommandHandler(ToggleConnectionCmd, connection_handler, 1, NULL);

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

    if ((!door_x || plane_type==15) && XPLMGetDatab(ref_acf_descrip, acf_descrip, 0, 128))
        /* Try descriptions */
        for (i=0; i<sizeof(planedb)/sizeof(db_t); i++)
            if (strstr(acf_descrip, planedb[i].key) && (door_x || planedb[i].lat))
            {
                if (plane_type==15)
                    plane_type=planedb[i].type;
                if (!door_x)
                {
                    door_x = F2M * planedb[i].lat;
                    door_y = F2M * (planedb[i].vert - XPLMGetDataf(ref_acf_cg_y));	/* Adjust relative to static cog */
                    door_z = F2M * (planedb[i].lng  - XPLMGetDataf(ref_acf_cg_z));	/* Adjust relative to static cog */
                }
                break;
            }

    if (!door_x)
    {
        state = IDLE;
        icao[0]=icao[1]=icao[2]=icao[3]=0;
    }
    else
    {
        int i;
        state = NEWPLANE;	/* Check for alignment at a gate during next frame */

        if (isupper(acf_icao[0]) || isdigit(acf_icao[0]))
            /* DGS objects fall back to using id1-4 datarefs if first character of ICAO field is null */
            for (i=0; i<4; i++)
                icao[i] = (isupper(acf_icao[i]) || isdigit(acf_icao[i])) ? acf_icao[i] : ' ';
        else
            /* Display canonical ICAO type */
            for (i=0; i<4; i++)
                icao[i] = canonical[plane_type][i];
    }
}

static void resetidle(void)
{
    state=IDLE;
    gate_x=gate_y=gate_z=gate_h=gate_update=0;
    gate_autogate=0;
    dgs_x=dgs_y=dgs_z=0;
    last_gate_x = last_gate_y = last_gate_z = last_gate_update = 0;
    last_dgs_x = last_dgs_y = last_dgs_z = last_dgs_update = 0;
    vert=lat=moving=0;
    status=id1=id2=id3=id4=lr=track=0;
    azimuth=distance=distance2=0;
    stopalert();
}

static float getgatefloat(XPLMDataRef inRefcon)
{
    float now;
    float plane_x, plane_z;
    float object_x, object_y, object_z, object_h;
    float local_x, local_y, local_z;
	
    now = XPLMGetDataf(ref_total_running_time_sec);
    object_x = XPLMGetDataf(ref_draw_object_x);
    object_y = XPLMGetDataf(ref_draw_object_y);
    object_z = XPLMGetDataf(ref_draw_object_z);

    if (last_gate_update==now && last_gate_x==object_x && last_gate_y==object_y && last_gate_z==object_z)
    {
        /* Same frame and object as last calculation */
        return (gate_x==object_x && gate_y==object_y && gate_z==object_z) ? *(float*)inRefcon : 0;
    }
    else
    {
        last_gate_update = now;
        last_gate_x = object_x;
        last_gate_y = object_y;
        last_gate_z = object_z;
    }

    if (state>IDLE && (gate_x!=object_x || gate_y!=object_y || gate_z!=object_z))
    {
        /* We're tracking and it's not by this gate */

        if (now-gate_update > POLLTIME)
        {
            /* We haven't seen our gate in a while - check we're still in range in case we've been moved across the airport */
            localpos(gate_x, gate_y, gate_z, gate_h, &local_x, &local_y, &local_z);
            if (fabsf(local_x)>CAP_X || local_z<DGS_Z || local_z>CAP_Z)
                resetidle();	/* Just gone out of range of the tracking gate */
                /* Fall through */
            else
                return 0;
        }
        return 0;
    }

    plane_x=XPLMGetDataf(ref_plane_x);
    plane_z=XPLMGetDataf(ref_plane_z);
    object_h=XPLMGetDataf(ref_draw_object_psi) * D2R;

    if (((object_x-plane_x) * (object_x-plane_x) + (object_z-plane_z) * (object_z-plane_z)) > (CAP_Z * CAP_Z) ||
        localpos(object_x, object_y, object_z, object_h, &local_x, &local_y, &local_z) ||
        fabsf(local_x)>CAP_X || local_z<DGS_Z || local_z>CAP_Z)
    {
        /* Not in range of this gate */

        if (gate_x==object_x && gate_y==object_y && gate_z==object_z)
            resetidle();	/* Just gone out of range of the tracking gate */

        if (state == NEWPLANE)
            XPLMSetFlightLoopCallbackInterval(newplanecallback, -1, 1, NULL);	/* Reset newplane state before next frame */

        return 0;
    }

    /* In range of this gate */

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
            XPLMSetDataf(ref_plane_x, plane_x + local_z * object_hsin - local_x * object_hcos);
            XPLMSetDataf(ref_plane_z, plane_z - local_z * object_hcos - local_x * object_hsin);
            localpos(object_x, object_y, object_z, object_h, &local_x, &local_y, &local_z);	/* recalc */

            XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);
            running |= (XPLMGetDataf(ref_parkingbrake) < 0.5f);
            state = running ? TRACK : DOCKED;
        }
        else if (!door_x)
        {
            state = IDFAIL;
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

        if ((float*)inRefcon == &lat)	/* Standalone DGS dummy gate only uses vert */
            gate_autogate = -1;		/* Relies on lat occurring in the .obj file before vert */
    }

    gate_update = now;
    updaterefs(now, local_x, local_y, local_z);
    return *(float*)inRefcon;
}


static int getdgs(void)
{
    float now, object_x, object_y, object_z;
    float local_x, local_y, local_z;

    if (state <= IDLE) return 0;	/* Only interested in DGSs if we're in range of a gate */

    now = XPLMGetDataf(ref_total_running_time_sec);
    object_x = XPLMGetDataf(ref_draw_object_x);
    object_y = XPLMGetDataf(ref_draw_object_y);
    object_z = XPLMGetDataf(ref_draw_object_z);

    if (last_dgs_update==now && last_dgs_x==object_x && last_dgs_y==object_y && last_dgs_z==object_z)
    {
        /* Same frame and object as last calculation */
        return (dgs_x==object_x && dgs_y==object_y && dgs_z==object_z);
    }
    else
    {
        last_dgs_update = now;
        last_dgs_x = object_x;
        last_dgs_y = object_y;
        last_dgs_z = object_z;
    }

    if (!(dgs_x || dgs_y || dgs_z))
    {
        /* Haven't yet identified the active dgs */
        float x, z;
        float gate_hcos, gate_hsin;

        float object_h = XPLMGetDataf(ref_draw_object_psi) * D2R;

        /* Check DGS is pointing in the same direction as the gate, and within desired radius */
        if (fabsf(object_h - gate_h) <= DGS_H &&
            ((object_x-gate_x) * (object_x-gate_x) + (object_z-gate_z) * (object_z-gate_z)) <= (DGS_Z * DGS_Z))
        {
            /* Location of this DGS in the active gate's space */
            gate_hcos = cosf(gate_h);
            gate_hsin = sinf(gate_h);
            x = gate_hcos*(object_x-gate_x) + gate_hsin*(object_z-gate_z);
            z = gate_hcos*(object_z-gate_z) - gate_hsin*(object_x-gate_x);

            if (fabsf(x)<=DGS_X && z<=0 && z>=DGS_Z)
            {
                dgs_x=object_x;
                dgs_y=object_y;
                dgs_z=object_z;
            }
            else
                return 0;
        }
        else
            return 0;
    }
    else if (dgs_x!=object_x || dgs_y!=object_y || dgs_z!=object_z)
        /*  Have identified the active dgs and this isn't it */
        return 0;

    /* Re-calculate plane location - can't rely on values from getgate() since that will not be being called if gate no longer in view */
    localpos(gate_x, gate_y, gate_z, gate_h, &local_x, &local_y, &local_z);

    updaterefs(now, local_x, local_y, local_z);
    return -1;
}

static float getdgsfloat(XPLMDataRef inRefcon)
{
    return getdgs() ? *(float*)inRefcon : 0;
}

static int getdgsint(XPLMDataRef inRefcon)
{
    return getdgs() ? *(int*)inRefcon : 0;
}

static int getdgsicao(XPLMDataRef inRefcon, int *outValues, int inOffset, int inMax)
{
    int i;

    if (outValues==NULL)
        return 4;
    else if (inMax<=0 || inOffset<0 || inOffset>=4)
        return 0;

    if (inMax+inOffset > 4)
        inMax = 4-inOffset;

    /* We get called a lot, so don't bother checking for updated values */
    if (state != TRACK)	/* Only report when active and tracking */
        for (i=0; i<inMax; outValues[i++] = 0);
    else
        for (i=0; i<inMax; i++) outValues[i] = icao[inOffset+i];

    return inMax;
}


/* Calculate location of plane's centreline opposite door in this object's space */
static int localpos(float object_x, float object_y, float object_z, float object_h, float *local_x, float *local_y, float *local_z)
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

    return 0;	/* Return value has no meaning */
}


/* Update published data used by gate and dgs */
static void updaterefs(float now, float local_x, float local_y, float local_z)
{
    int running;
    int locgood=(fabsf(local_x)<=AZI_X && fabsf(local_z)<=GOOD_Z);
	
    XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);
    int pbrake = (XPLMGetDataf(ref_parkingbrake) < 0.5f);
    if (g_automaticBehavior)
    running |= pbrake;

    status=id1=id2=id3=id4=lr=track=0;
    azimuth=distance=distance2=0;

    switch (state)
    {
    case IDFAIL:
        lr=3;	/* Stop */
        status=5;
        break;

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
			if (g_automaticBehavior)
			{
				state = ENGAGE;
				timestamp = now;
			}
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
            if (!moving && gate_autogate) playalert();
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
            if (!moving && gate_autogate) playalert();
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

static XPLMDataRef intref(char *inDataName, XPLMGetDatai_f inReadInt, int *inRefcon)
{
    return XPLMRegisterDataAccessor(inDataName, xplmType_Int, 0, inReadInt, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, inRefcon, 0);
}

static XPLMDataRef intarrayref(char *inDataName, XPLMGetDatavi_f inReadIntArray, int *inRefcon)
{
    return XPLMRegisterDataAccessor(inDataName, xplmType_IntArray, 0, NULL, NULL, NULL, NULL, NULL, NULL, inReadIntArray, NULL, NULL, NULL, NULL, NULL, inRefcon, 0);
}
	

/* Log to Log.txt. Returns 0 because that's a failure return code from most XP entry points */
int xplog(char *msg)
{
    XPLMDebugString("AutoGate: ");
    XPLMDebugString(msg);
    XPLMDebugString("\n");
    return 0;
}

void menu_handler(void * in_menu_ref, void * in_item_ref)
{
	if (!strcmp((const char *)in_item_ref, "Menu Item 1"))
	{
		process_connection();
	}
	else if (!strcmp((const char *)in_item_ref, "Menu Item 2"))
	{
		process_automatic_behavior();
	}
}

void process_connection()
{
	if (g_automaticBehavior)
		return;

	// do stuff
	float now = XPLMGetDataf(ref_total_running_time_sec);
	if (state == GOOD || state == DISENGAGED)
	{

		timestamp = now;
		state = ENGAGE;
	}
	else if (state == DOCKED)
	{
		timestamp = now;
		state = DISENGAGE;
	}
	// TODO: ADD Message box 
}

void process_automatic_behavior()
{
	XPLMMenuCheck state;
	XPLMCheckMenuItemState(g_menu_id, g_automenu, &state);
	if (state == xplm_Menu_Unchecked)
	{
		XPLMEnableMenuItem(g_menu_id, g_menu_connect, 0);
		XPLMCheckMenuItem(g_menu_id, g_automenu, xplm_Menu_Checked);
		g_automaticBehavior = 1;
	}
	else if (state == xplm_Menu_Checked)
	{
		XPLMEnableMenuItem(g_menu_id, g_menu_connect, 1);
		XPLMCheckMenuItem(g_menu_id, g_automenu, xplm_Menu_Unchecked);
		g_automaticBehavior = 0;
	}

	// Save settings
	save_settings_to_file();
}

int automatic_behavior_handler(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon)
{
	process_automatic_behavior();
	return 0;
}

int connection_handler(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon)
{
	process_connection();
	return 0;
}

int save_settings_to_file()
{
	
	char buffer[PATH_MAX], *c;
	XPLMGetPluginInfo(XPLMGetMyID(), NULL, buffer, NULL, NULL);
	posixify(buffer);
	if (!(c = strrchr(buffer, '/'))) return xplog("Can't find my plugin");
	*(c + 1) = '\0';
	if (!strcmp(c - 3, "/32/") || !strcmp(c - 3, "/64/")) { *(c - 2) = '\0'; }	/* plugins one level down on some builds, so go up */
	strcat(buffer, "settings.txt");
	FILE *fp = fopen(buffer, "w+");
	if (!fp) return xplog("Can't open setting file.");
	// writeback settings
	fprintf(fp, "automatic:%i", g_automaticBehavior);
	fclose(fp);
	return 0;
}

int load_settings_from_file()
{
	/* Locate settings file */
	char buffer[PATH_MAX], *c;
	XPLMGetPluginInfo(XPLMGetMyID(), NULL, buffer, NULL, NULL);
	posixify(buffer);
	if (!(c = strrchr(buffer, '/'))) return xplog("Can't find my plugin");
	*(c + 1) = '\0';
	if (!strcmp(c - 3, "/32/") || !strcmp(c - 3, "/64/")) { *(c - 2) = '\0'; }	/* plugins one level down on some builds, so go up */
	strcat(buffer, "settings.txt");
	FILE *fp = fopen(buffer, "r");
	if (!fp)
	{
		g_automaticBehavior = 1;
		return xplog("Can't open setting file.");
	}

	char buff[12]; // automatic:0
	fscanf(fp, "%s", buff);
	xplog("loading settings");
	xplog(buff);
	if (!strcmp(buff, "automatic:0"))
	{
		g_automaticBehavior = 0;
		xplog("automatic behavior disabled");
	}
	else
	{
		g_automaticBehavior = 1;
		xplog("automatic behavior enabled");
	}
	fclose(fp);

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

    char *statestr[] = { "Disabled", "NewPlane", "Idle", "IDFail", "Track", "Good", "Bad", "Engage", "Docked", "Disengage", "Disengaged" };
    sprintf(buf, "State: %s %s %s", statestr[state], plane_type==15 ? "Unknown" : canonical[plane_type], running ? "Running" : "Parked");
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
    sprintf(buf, "ID   : %1d %1d %1d %1d %c%c%c%c", id1, id2, id3, id4, icao[0], icao[1], icao[2], icao[3]);
    XPLMDrawString(color, left + 5, top - 90, buf, 0, xplmFont_Basic);
    sprintf(buf, "Gate : lat=%6.3f vert=%6.3f moving=%1.0f", lat, vert, moving);
    XPLMDrawString(color, left + 5, top -100, buf, 0, xplmFont_Basic);
    sprintf(buf, "DGS  : status=%1d track=%1d lr=%1d %4.1f %4.1f %4.1f", status, track, lr, azimuth, distance, distance2);
    XPLMDrawString(color, left + 5, top -110, buf, 0, xplmFont_Basic);
    alGetSourcefv(snd_src, AL_POSITION, pos);
    alGetSourcefv(snd_src, AL_GAIN, &gain);
    sprintf(buf, "Sound: %10.3f %10.3f %10.3f %6.2f", pos[0], pos[1], pos[2], gain);
    XPLMDrawString(color, left + 5, top -130, buf, 0, xplmFont_Basic);
}				    
#endif
