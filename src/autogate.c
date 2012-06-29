/*
 * AutoGate
 * 
 * (c) Jonathan Harris 2006,2008
 * 
 */

#if IBM
#  include <windows.h>
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason, LPVOID lpReserved)
{ return TRUE; }
#endif

#include "autogate.h"


/* Globals */
static XPLMWindowID windowId = NULL;
static state_t state = DISABLED;
static float timestamp;
static int plane_type;
static float door_x, door_y, door_z;		/* door offset relative to ref point */

/* Datarefs */
static XPLMDataRef ref_plane_x, ref_plane_y, ref_plane_z, ref_plane_psi;
static XPLMDataRef ref_ENGN_running;
static XPLMDataRef ref_draw_object_x, ref_draw_object_y, ref_draw_object_z,
  ref_draw_object_psi;
static XPLMDataRef ref_acf_descrip, ref_acf_icao;
static XPLMDataRef ref_acf_tow_hook_Y, ref_acf_tow_hook_Z;	/* for finding cg */
static XPLMDataRef ref_acf_door_x, ref_acf_door_y, ref_acf_door_z;
static XPLMDataRef ref_total_running_time_sec;

/* Published Datarefs */
static XPLMDataRef ref_vert, ref_lat;
static XPLMDataRef ref_status, ref_id1, ref_id2, ref_id3, ref_id4, ref_lr, ref_track;
static XPLMDataRef ref_azimuth, ref_distance, ref_distance2;

/* loc of plane's centreline opposite door in gate's space rel to stop */
static float local_x, local_y, local_z;	

static float gate_x, gate_y, gate_z, gate_h;	/* active gate */
static float dgs_x, dgs_y, dgs_z, dgs_h;	/* active DGS */

/* Published dataref values */
static float lat, vert;
static float status, id1, id2, id3, id4, lr, track;
static float azimuth, distance, distance2;


PLUGIN_API int XPluginStart(char *outName, char *outSig, char *outDesc)
{
	strcpy(outName, pluginName);
	strcpy(outSig,	pluginSig);
	strcpy(outDesc, pluginDesc);

	/* Datarefs */
	ref_plane_x =XPLMFindDataRef("sim/flightmodel/position/local_x");
	ref_plane_y =XPLMFindDataRef("sim/flightmodel/position/local_y");
	ref_plane_z =XPLMFindDataRef("sim/flightmodel/position/local_z");
	ref_plane_psi   =XPLMFindDataRef("sim/flightmodel/position/psi");
	ref_ENGN_running=XPLMFindDataRef("sim/flightmodel/engine/ENGN_running");
	ref_draw_object_x  =XPLMFindDataRef("sim/graphics/animation/draw_object_x");
	ref_draw_object_y  =XPLMFindDataRef("sim/graphics/animation/draw_object_y");
	ref_draw_object_z  =XPLMFindDataRef("sim/graphics/animation/draw_object_z");
	ref_draw_object_psi=XPLMFindDataRef("sim/graphics/animation/draw_object_psi");
	ref_acf_descrip    =XPLMFindDataRef("sim/aircraft/view/acf_descrip");
	ref_acf_icao       =XPLMFindDataRef("sim/aircraft/view/acf_ICAO");
	ref_acf_tow_hook_Y =XPLMFindDataRef("sim/aircraft/overflow/acf_tow_hook_Y");
	ref_acf_tow_hook_Z =XPLMFindDataRef("sim/aircraft/overflow/acf_tow_hook_Z");
	ref_acf_door_x     =XPLMFindDataRef("sim/aircraft/view/acf_door_x");
	ref_acf_door_y     =XPLMFindDataRef("sim/aircraft/view/acf_door_y");
	ref_acf_door_z     =XPLMFindDataRef("sim/aircraft/view/acf_door_z");
	ref_total_running_time_sec=XPLMFindDataRef("sim/time/total_running_time_sec");

	/* Published Datarefs */
	ref_vert  =floatref("marginal.org.uk/autogate/vert", getgate, &vert);
	ref_lat	  =floatref("marginal.org.uk/autogate/lat",  getgate, &lat);

	ref_status=floatref("marginal.org.uk/dgs/status", getdgs, &status);
	ref_id1	  =floatref("marginal.org.uk/dgs/id1",	  getdgs, &id1);
	ref_id2	  =floatref("marginal.org.uk/dgs/id2",	  getdgs, &id2);
	ref_id3	  =floatref("marginal.org.uk/dgs/id3",	  getdgs, &id3);
	ref_id4	  =floatref("marginal.org.uk/dgs/id4",	  getdgs, &id4);
	ref_lr	  =floatref("marginal.org.uk/dgs/lr",	  getdgs, &lr);
	ref_track =floatref("marginal.org.uk/dgs/track",  getdgs, &track);
	ref_azimuth  =floatref("marginal.org.uk/dgs/azimuth",
			       getdgs, &azimuth);
	ref_distance =floatref("marginal.org.uk/dgs/distance",
			       getdgs, &distance);
	ref_distance2=floatref("marginal.org.uk/dgs/distance2",
			       getdgs, &distance2);
#ifdef DEBUG
	windowId = XPLMCreateWindow(10, 750, 310, 650, 1,
				    drawdebug, NULL, NULL, NULL);
#endif
	return 1;
}

PLUGIN_API void	XPluginStop(void)
{
	if (windowId)
		XPLMDestroyWindow(windowId);
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

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFromWho,
				      long inMessage, void *inParam)
{
	if (state!=DISABLED && inParam==0 &&
	    (inMessage==XPLM_MSG_PLANE_LOADED ||	/* Under v9 CoG not calculated yet */
	     inMessage==XPLM_MSG_PLANE_CRASHED ||
	     inMessage==XPLM_MSG_AIRPORT_LOADED))
		newplane();
}

static void newplane(void)
{
	char acf_descrip[129];
	char acf_icao[41];
	int i;
	
	resetidle();
	acf_descrip[128]=0;	/* Not sure if XPLMGetDatab NULL terminates */
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

	if (door_x && (plane_type!=15))
		return;	/* Have ID and data */

	/* Try table */
	if (XPLMGetDatab(ref_acf_descrip, acf_descrip, 0, 128))
		for (i=0; i<sizeof(planedb)/sizeof(db_t); i++)
			if (strstr(acf_descrip, planedb[i].key) &&
				(door_x || planedb[i].lat))
			{
				plane_type=planedb[i].type;
				if (!door_x)
				{
					door_x=F2M*planedb[i].lat;
					door_y=F2M*planedb[i].vert +
						XPLMGetDataf(ref_acf_tow_hook_Y);
					door_z=F2M*planedb[i].lng +
						XPLMGetDataf(ref_acf_tow_hook_Z);
				}
				return;
			}
	
	if (!door_x)
		state=IDFAIL;	/* No data */
}

static void resetidle(void)
{
	state=IDLE;
	local_x=local_y=local_z=0;
	gate_x=gate_y=gate_z=gate_h=0;
	dgs_x=dgs_y=dgs_z=dgs_h=0;
	vert=lat=0;
	status=id1=id2=id3=id4=lr=track=0;
	azimuth=distance=distance2=0;
}

static float getgate(XPLMDataRef inRefcon)
{
	/* loc of plane's cg when remaining vars were last calculated */
	static float cache_x, cache_y, cache_z;

	float plane_x, plane_y, plane_z, plane_hrad;
	float object_x, object_y, object_z;
	float object_h, object_hrad, object_hcos, object_hsin;
	float x, y, z;
	float loc_x, loc_y, loc_z;
	
	if (state<IDLE)
		return 0;

	object_x=XPLMGetDataf(ref_draw_object_x);
	object_y=XPLMGetDataf(ref_draw_object_y);
	object_z=XPLMGetDataf(ref_draw_object_z);
	if (state>IDLE &&
	    (gate_x!=object_x || gate_y!=object_y || gate_z!=object_z))
		/* We're tracking and it's not by this gate */
		return 0;

	plane_x=XPLMGetDatad(ref_plane_x);
	plane_y=XPLMGetDatad(ref_plane_y);
	plane_z=XPLMGetDatad(ref_plane_z);
	if (plane_x==cache_x && plane_y==cache_y && plane_z==cache_z)
		/* We haven't moved since last calculation */
		return *(float*)inRefcon;
	
	plane_hrad=XPLMGetDataf(ref_plane_psi) * D2R;
	
	/* Location of plane's centreline opposite door */
	/* Calculation assumes plane is horizontal */
	x=plane_x-door_z*sin(plane_hrad);
	y=plane_y+door_y;
	z=plane_z+door_z*cos(plane_hrad);

	/* Location of centreline opposite door in this gate's space */
	object_h=XPLMGetDataf(ref_draw_object_psi);
	object_hrad=object_h * D2R;
	object_hcos=cos(object_hrad);
	object_hsin=sin(object_hrad);
	loc_x=object_hcos*(x-object_x)+object_hsin*(z-object_z);
	loc_y=y-object_y;
	loc_z=object_hcos*(z-object_z)-object_hsin*(x-object_x);

	if (fabs(loc_x)>CAP_X || loc_z<DGS_Z || loc_z>CAP_Z)
	{
		/* Not in range of this gate */
		if (gate_x==object_x && gate_y==object_y && gate_z==object_z)
			resetidle();	/* Just gone out of range */
		return 0;
	}
	    
	if (gate_x!=object_x || gate_y!=object_y || gate_z!=object_z)
	{
		/* Just come into range */
		state=TRACK;
		gate_x=object_x;
		gate_y=object_y;
		gate_z=object_z;
		gate_h=object_h;
	}
	
	local_x=loc_x; local_y=loc_y; local_z=loc_z;
	cache_x=plane_x; cache_y=plane_y; cache_z=plane_z;

	updaterefs();
	return *(float*)inRefcon;
}

static float getdgs(XPLMDataRef inRefcon)
{
	/* loc of plane's cg when remaining vars were last calculated */
	static float cache_x, cache_y, cache_z;

	float plane_x, plane_y, plane_z, plane_hrad;
	float object_x, object_y, object_z;
	float gate_hrad, gate_hcos, gate_hsin;
	float x, y, z;
	
	if (state<=IDLE)
		return 0;

	object_x=XPLMGetDataf(ref_draw_object_x);
	object_y=XPLMGetDataf(ref_draw_object_y);
	object_z=XPLMGetDataf(ref_draw_object_z);

	if (!(dgs_x || dgs_y || dgs_z))
	{
		/* Haven't yet identified the active dgs */
		float x, y, z;
		
		/* Location of this dgs in the active gate's space */
		gate_hrad=gate_h * D2R;
		gate_hcos=cos(gate_hrad);
		gate_hsin=sin(gate_hrad);
		x=gate_hcos*(object_x-gate_x) + gate_hsin*(object_z-gate_z);
		y=object_y-gate_y;
		z=gate_hcos*(object_z-gate_z) - gate_hsin*(object_x-gate_x);
		if (fabs(x)<=DGS_X && z<=0 && z>=DGS_Z)
		{
			dgs_x=object_x;
			dgs_y=object_y;
			dgs_z=object_z;
			dgs_h=XPLMGetDataf(ref_draw_object_psi);
		}
		else
			return 0;
	}
	else if (dgs_x!=object_x || dgs_y!=object_y || dgs_z!=object_z)
		return 0;
	
	/* Re-calculate plane location since gate may no longer be in view */
	plane_x=XPLMGetDatad(ref_plane_x);
	plane_y=XPLMGetDatad(ref_plane_y);
	plane_z=XPLMGetDatad(ref_plane_z);
	if (plane_x==cache_x && plane_y==cache_y && plane_z==cache_z)
		/* We haven't moved since last calculation */
		return *(float*)inRefcon;

	plane_hrad=XPLMGetDataf(ref_plane_psi) * D2R;

	/* Location of centreline opposite door */
	/* Calculation assumes plane is horizontal */
	x=plane_x-door_z*sin(plane_hrad);
	y=plane_y+door_y;
	z=plane_z+door_z*cos(plane_hrad);

	/* Location of centreline opposite door in this gate's space */
	gate_hrad=gate_h * D2R;
	gate_hcos=cos(gate_hrad);
	gate_hsin=sin(gate_hrad);
	local_x=gate_hcos*(x-gate_x)+gate_hsin*(z-gate_z);
	local_y=y-gate_y;
	local_z=gate_hcos*(z-gate_z)-gate_hsin*(x-gate_x);
	cache_x=plane_x; cache_y=plane_y; cache_z=plane_z;
	
	updaterefs();
	return *(float*)inRefcon;
}

/* Update published data used by gate and dgs */
static void updaterefs(void)
{
	int running;
	int locgood=(fabs(local_x)<=AZI_X && fabs(local_z)<=GOOD_Z);
	float now=XPLMGetDataf(ref_total_running_time_sec);
	
	XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);

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
				fabs(local_x) > AZI_X)
				track=1;	/* lead-in only */
			else
			{
				distance=((float)((int)((local_z-
							 GOOD_Z)*2))) / 2;
				azimuth=((float)((int)(local_x*2))) / 2;
				if (azimuth>4)	azimuth=4;
				if (azimuth<-4) azimuth=-4;
				if (azimuth<=-0.5)
					lr=1;
				else if (azimuth>=0.5)
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
		}
		break;

	case DISENGAGE:
		/* Blank */
		if (now>timestamp+DURATION)
		{
			state=DISENGAGED;
			lat=vert=0;
		}
		else
		{
			float ratio=1 - (now-timestamp)/DURATION;
			lat =(door_x-OBJ_X) * ratio;
			vert=(local_y-OBJ_Y) * ratio;
		}
		break;

	case DISENGAGED:
		/* Blank */
		if (local_z-GOOD_Z > AZI_Z || fabs(local_x) > AZI_X)
			/* Go back to lead-in */
			state=TRACK;
		break;
	}
}

static XPLMDataRef floatref(char *inDataName,
			    XPLMGetDataf_f inReadFloat,
			    float *inRefcon)
{
	return XPLMRegisterDataAccessor(inDataName,
					xplmType_Float, 0,
					NULL, NULL,
					inReadFloat, NULL,
					NULL, NULL,
					NULL, NULL, NULL, NULL, NULL, NULL,
					inRefcon, 0);
}
	

#ifdef DEBUG
static void drawdebug(XPLMWindowID inWindowID, void *inRefcon)
{
	char buf[128];
	int left, top, right, bottom;
	float color[] = { 1.0, 1.0, 1.0 };	/* RGB White */
	int running;
	int locgood=(fabs(local_x)<=AZI_X && fabs(local_z)<=GOOD_Z);
	XPLMGetDatavi(ref_ENGN_running, &running, 0, 1);

	XPLMGetWindowGeometry(inWindowID, &left, &top, &right, &bottom);
	XPLMDrawTranslucentDarkBox(left, top, right, bottom);

	sprintf(buf, "State: %d %d %d %d",
		state, plane_type, locgood, running);
	XPLMDrawString(color, left + 5, top - 10, buf, 0, xplmFont_Basic);
	sprintf(buf, "Hook : %10.3f %10.3f %10.3f",
		0.0,
		XPLMGetDataf(ref_acf_tow_hook_Y),
		XPLMGetDataf(ref_acf_tow_hook_Z));
	XPLMDrawString(color, left + 5, top - 20, buf, 0, xplmFont_Basic);
	sprintf(buf, "Door : %10.3f %10.3f %10.3f",
		XPLMGetDataf(ref_acf_door_x),
		XPLMGetDataf(ref_acf_door_y),
		XPLMGetDataf(ref_acf_door_z));
	XPLMDrawString(color, left + 5, top - 30, buf, 0, xplmFont_Basic);
	sprintf(buf, "Plane: %10.3f %10.3f %10.3f %6.2f",
		XPLMGetDatad(ref_plane_x), XPLMGetDatad(ref_plane_y),
		XPLMGetDatad(ref_plane_z), XPLMGetDataf(ref_plane_psi));
	XPLMDrawString(color, left + 5, top - 40, buf, 0, xplmFont_Basic);
	sprintf(buf, "Gate : %10.3f %10.3f %10.3f %6.2f",
		gate_x, gate_y, gate_z, gate_h);
	XPLMDrawString(color, left + 5, top - 50, buf, 0, xplmFont_Basic);
	sprintf(buf, "DGS  : %10.3f %10.3f %10.3f %6.2f",
		dgs_x, dgs_y, dgs_z, dgs_h);
	XPLMDrawString(color, left + 5, top - 60, buf, 0, xplmFont_Basic);
	sprintf(buf, "Local: %10.3f %10.3f %10.3f",
		local_x, local_y, local_z);
	XPLMDrawString(color, left + 5, top - 70, buf, 0, xplmFont_Basic);
	sprintf(buf, "Time : %10.3f",
		timestamp);
	XPLMDrawString(color, left + 5, top - 80, buf, 0, xplmFont_Basic);
	sprintf(buf, "Data : %6.3f %6.3f %1.0f %1.0f %1.0f %1.0f %1.0f %1.0f %1.0f %4.1f %4.1f %4.1f",
		lat, vert,
		status, id1, id2, id3, id4,
		lr, track,
		azimuth, distance, distance2);
	XPLMDrawString(color, left + 5, top - 90, buf, 0, xplmFont_Basic);
}				    
#endif

