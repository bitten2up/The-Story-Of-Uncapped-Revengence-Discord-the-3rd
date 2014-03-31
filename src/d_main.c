// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2014 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  d_main.c
/// \brief SRB2 main program
///
///        SRB2 main program (D_SRB2Main) and game loop (D_SRB2Loop),
///        plus functions to parse command line parameters, configure game
///        parameters, and call the startup functions.

#if (defined (__unix__) && !defined (MSDOS)) || defined(__APPLE__) || defined (UNIXCOMMON)
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifdef __GNUC__
#include <unistd.h> // for getcwd
#endif

#ifdef PC_DOS
#include <stdio.h> // for snprintf
int	snprintf(char *str, size_t n, const char *fmt, ...);
//int	vsnprintf(char *str, size_t n, const char *fmt, va_list ap);
#endif

#if (defined (_WIN32) && !defined (_WIN32_WCE)) && !defined (_XBOX)
#include <direct.h>
#include <malloc.h>
#endif

#if !defined (UNDER_CE)
#include <time.h>
#elif defined (_XBOX)
#define NO_TIME
#endif

#include "doomdef.h"
#include "am_map.h"
#include "console.h"
#include "d_net.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_video.h"
#include "m_argv.h"
#include "m_menu.h"
#include "m_misc.h"
#include "p_setup.h"
#include "p_saveg.h"
#include "r_main.h"
#include "r_local.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "d_main.h"
#include "d_netfil.h"
#include "m_cheat.h"
#include "y_inter.h"
#include "p_local.h" // chasecam
#include "mserv.h" // ms_RoomId
#include "m_misc.h" // screenshot functionality
#include "dehacked.h" // Dehacked list test
#include "m_cond.h" // condition initialization
#include "fastcmp.h"

#ifdef _XBOX
#include "sdl/SRB2XBOX/xboxhelp.h"
#endif

#ifdef HWRENDER
#include "hardware/hw_main.h" // 3D View Rendering
#endif

#ifdef _WINDOWS
#include "win32/win_main.h" // I_DoStartupMouse
#endif

#ifdef HW3SOUND
#include "hardware/hw3sound.h"
#endif

//
// DEMO LOOP
//
//static INT32 demosequence;
static const char *pagename = "MAP1PIC";
static char *startupwadfiles[MAX_WADFILES];

boolean devparm = false; // started game with -devparm

boolean singletics = false; // timedemo
boolean lastdraw = false;

postimg_t postimgtype = postimg_none;
INT32 postimgparam;
postimg_t postimgtype2 = postimg_none;
INT32 postimgparam2;

#ifdef _XBOX
boolean nomidimusic = true, nosound = true;
boolean nodigimusic = true;
#else
boolean nomidimusic = false, nosound = false;
boolean nodigimusic = false; // No fmod-based music
#endif

// These variables are only true if
// the respective sound system is initialized
// and active, but no sounds/music should play.
boolean music_disabled = false;
boolean sound_disabled = false;
boolean digital_disabled = false;

boolean advancedemo;
#ifdef DEBUGFILE
INT32 debugload = 0;
#endif

#ifdef _arch_dreamcast
char srb2home[256] = "/cd";
char srb2path[256] = "/cd";
#else
char srb2home[256] = ".";
char srb2path[256] = ".";
#endif
boolean usehome = true;
const char *pandf = "%s" PATHSEP "%s";

//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
// referenced from i_system.c for I_GetKey()

event_t events[MAXEVENTS];
INT32 eventhead, eventtail;

boolean dedicated = false;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(const event_t *ev)
{
	events[eventhead] = *ev;
	eventhead = (eventhead+1) & (MAXEVENTS-1);
}
// just for lock this function
#ifndef DOXYGEN
void D_PostEvent_end(void) {};
#endif

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents(void)
{
	event_t *ev;

	for (; eventtail != eventhead; eventtail = (eventtail+1) & (MAXEVENTS-1))
	{
		ev = &events[eventtail];

		// Screenshots over everything so that they can be taken anywhere.
		if (M_ScreenshotResponder(ev))
			continue; // ate the event

		if (gameaction == ga_nothing && gamestate == GS_TITLESCREEN)
		{
			if (cht_Responder(ev))
				continue;
		}

		// Menu input
		if (M_Responder(ev))
			continue; // menu ate the event

		// console input
		if (CON_Responder(ev))
			continue; // ate the event

		G_Responder(ev);
	}
}

//
// D_Display
// draw current display, possibly wiping it from the previous
//

// wipegamestate can be set to -1 to force a wipe on the next draw
// added comment : there is a wipe eatch change of the gamestate
gamestate_t wipegamestate = GS_LEVEL;

static void D_Display(void)
{
	static boolean menuactivestate = false;
	static gamestate_t oldgamestate = -1;
	boolean redrawsbar = false;

	static boolean wipe = false;
	INT32 wipedefindex = 0;

	if (dedicated)
		return;

	if (nodrawers)
		return; // for comparative timing/profiling

	// check for change of screen size (video mode)
	if (setmodeneeded && !wipe)
		SCR_SetMode(); // change video mode

	if (vid.recalc)
		SCR_Recalc(); // NOTE! setsizeneeded is set by SCR_Recalc()

	// change the view size if needed
	if (setsizeneeded)
	{
		R_ExecuteSetViewSize();
		oldgamestate = -1; // force background redraw
		redrawsbar = true;
	}

	// save the current screen if about to wipe
	if (gamestate != wipegamestate)
	{
		wipe = true;
		F_WipeStartScreen();
	}
	else
		wipe = false;

	// draw buffered stuff to screen
	// Used only by linux GGI version
	I_UpdateNoBlit();

	if (wipe)
	{
		// set for all later
		wipedefindex = gamestate; // wipe_xxx_toblack
		if (gamestate == GS_INTERMISSION)
		{
			if (intertype == int_spec) // Special Stage
				wipedefindex = wipe_specinter_toblack;
			else if (intertype != int_coop) // Multiplayer
				wipedefindex = wipe_multinter_toblack;
		}

		if (rendermode != render_none)
		{
			// Fade to black first
			if (gamestate != GS_LEVEL // fades to black on its own timing, always
			 && wipedefs[wipedefindex] != UINT8_MAX)
			{
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
				F_WipeEndScreen();
				F_RunWipe(wipedefs[wipedefindex], gamestate != GS_TIMEATTACK);
			}

			F_WipeStartScreen();
		}
	}

	// do buffered drawing
	switch (gamestate)
	{
		case GS_LEVEL:
			if (!gametic)
				break;
			HU_Erase();
			if (automapactive)
				AM_Drawer();
			if (wipe || menuactivestate || (rendermode != render_soft && rendermode != render_none) || vid.recalc)
				redrawsbar = true;
			break;

		case GS_INTERMISSION:
			Y_IntermissionDrawer();
			HU_Erase();
			HU_Drawer();
			break;

		case GS_TIMEATTACK:
			break;

		case GS_INTRO:
			F_IntroDrawer();
			if (wipegamestate == (gamestate_t)-1)
				wipe = true;
			break;

		case GS_CUTSCENE:
			F_CutsceneDrawer();
			HU_Erase();
			HU_Drawer();
			break;

		case GS_GAMEEND:
			F_GameEndDrawer();
			break;

		case GS_EVALUATION:
			F_GameEvaluationDrawer();
			HU_Drawer();
			break;

		case GS_CONTINUING:
			F_ContinueDrawer();
			break;

		case GS_CREDITS:
			F_CreditDrawer();
			HU_Erase();
			HU_Drawer();
			break;

		case GS_TITLESCREEN:
			F_TitleScreenDrawer();
			break;

		case GS_WAITINGPLAYERS:
			// The clientconnect drawer is independent...
		case GS_DEDICATEDSERVER:
		case GS_NULL:
			break;
	}

	// clean up border stuff
	// see if the border needs to be initially drawn
	if (gamestate == GS_LEVEL)
	{
#if 0
		if (oldgamestate != GS_LEVEL)
			R_FillBackScreen(); // draw the pattern into the back screen
#endif

		// draw the view directly
		if (!automapactive && !dedicated && cv_renderview.value)
		{
			if (players[displayplayer].mo || players[displayplayer].playerstate == PST_DEAD)
			{
				topleft = screens[0] + viewwindowy*vid.width + viewwindowx;
				objectsdrawn = 0;
#ifdef HWRENDER
				if (rendermode != render_soft)
					HWR_RenderPlayerView(0, &players[displayplayer]);
				else
#endif
				if (rendermode != render_none)
					R_RenderPlayerView(&players[displayplayer]);
			}

			// render the second screen
			if (splitscreen && players[secondarydisplayplayer].mo)
			{
#ifdef HWRENDER
				if (rendermode != render_soft)
					HWR_RenderPlayerView(1, &players[secondarydisplayplayer]);
				else
#endif
				if (rendermode != render_none)
				{
					viewwindowy = vid.height / 2;
					M_Memcpy(ylookup, ylookup2, viewheight*sizeof (ylookup[0]));

					topleft = screens[0] + viewwindowy*vid.width + viewwindowx;

					R_RenderPlayerView(&players[secondarydisplayplayer]);

					viewwindowy = 0;
					M_Memcpy(ylookup, ylookup1, viewheight*sizeof (ylookup[0]));
				}
			}

			// Image postprocessing effect
			if (postimgtype)
				V_DoPostProcessor(0, postimgtype, postimgparam);
			if (postimgtype2)
				V_DoPostProcessor(1, postimgtype2, postimgparam2);
		}

		if (lastdraw)
		{
			if (rendermode == render_soft)
			{
				VID_BlitLinearScreen(screens[0], screens[1], vid.width*vid.bpp, vid.height, vid.width*vid.bpp, vid.rowbytes);
				usebuffer = true;
			}
			lastdraw = false;
		}

		ST_Drawer(redrawsbar);

		HU_Drawer();
	}

	// change gamma if needed
	if (gamestate != oldgamestate && gamestate != GS_LEVEL)
		V_SetPalette(0);

	menuactivestate = menuactive;
	oldgamestate = wipegamestate = gamestate;

	// draw pause pic
	if (paused && cv_showhud.value && (!menuactive || netgame))
	{
		INT32 py;
		patch_t *patch;
		if (automapactive)
			py = 4;
		else
			py = viewwindowy + 4;
		patch = W_CachePatchName("M_PAUSE", PU_CACHE);
		V_DrawScaledPatch(viewwindowx + (BASEVIDWIDTH - SHORT(patch->width))/2, py, 0, patch);
	}

	// vid size change is now finished if it was on...
	vid.recalc = 0;

	// FIXME: draw either console or menu, not the two
	if (gamestate != GS_TIMEATTACK)
		CON_Drawer();

	M_Drawer(); // menu is drawn even on top of everything
	NetUpdate(); // send out any new accumulation

	// It's safe to end the game now.
	if (G_GetExitGameFlag())
	{
		Command_ExitGame_f();
		G_ClearExitGameFlag();
	}

	//
	// normal update
	//
	if (!wipe)
	{
		if (cv_netstat.value)
		{
			char s[50];
			Net_GetNetStat();

			s[sizeof s - 1] = '\0';

			snprintf(s, sizeof s - 1, "get %d b/s", getbps);
			V_DrawRightAlignedString(BASEVIDWIDTH, BASEVIDHEIGHT-ST_HEIGHT-40, V_YELLOWMAP, s);
			snprintf(s, sizeof s - 1, "send %d b/s", sendbps);
			V_DrawRightAlignedString(BASEVIDWIDTH, BASEVIDHEIGHT-ST_HEIGHT-30, V_YELLOWMAP, s);
			snprintf(s, sizeof s - 1, "GameMiss %.2f%%", gamelostpercent);
			V_DrawRightAlignedString(BASEVIDWIDTH, BASEVIDHEIGHT-ST_HEIGHT-20, V_YELLOWMAP, s);
			snprintf(s, sizeof s - 1, "SysMiss %.2f%%", lostpercent);
			V_DrawRightAlignedString(BASEVIDWIDTH, BASEVIDHEIGHT-ST_HEIGHT-10, V_YELLOWMAP, s);
		}

		I_FinishUpdate(); // page flip or blit buffer
		return;
	}

	//
	// wipe update
	//
	wipedefindex += WIPEFINALSHIFT;

	if (rendermode != render_none)
	{
		F_WipeEndScreen();
		F_RunWipe(wipedefs[wipedefindex], gamestate != GS_TIMEATTACK);
	}
}

// =========================================================================
// D_SRB2Loop
// =========================================================================

tic_t rendergametic;
boolean supdate;

void D_SRB2Loop(void)
{
	tic_t oldentertics = 0, entertic = 0, realtics = 0, rendertimeout = INFTICS;

	if (dedicated)
		server = true;

	if (M_CheckParm("-voodoo")) // 256x256 Texture Limiter
		COM_BufAddText("gr_voodoocompatibility on\n");

	// Pushing of + parameters is now done back in D_SRB2Main, not here.

	CONS_Printf("I_StartupKeyboard()...\n");
	I_StartupKeyboard();

#ifdef _WINDOWS
	CONS_Printf("I_StartupMouse()...\n");
	I_DoStartupMouse();
#endif

	oldentertics = I_GetTime();

	// end of loading screen: CONS_Printf() will no more call FinishUpdate()
	con_startup = false;

	// make sure to do a d_display to init mode _before_ load a level
	SCR_SetMode(); // change video mode
	SCR_Recalc();

	// Check and print which version is executed.
	// Use this as the border between setup and the main game loop being entered.
	CONS_Printf(
	"===========================================================================\n"
	"                   We hope you enjoy this game as\n"
	"                     much as we did making it!\n"
	"                            ...wait. =P\n"
	"===========================================================================\n");

	// hack to start on a nice clear console screen.
	COM_ImmedExecute("cls;version");

	if (rendermode == render_soft)
		V_DrawScaledPatch(0, 0, 0, (patch_t *)W_CacheLumpNum(W_GetNumForName("CONSBACK"), PU_CACHE));
	I_FinishUpdate(); // page flip or blit buffer

	for (;;)
	{
		if (lastwipetic)
		{
			oldentertics = lastwipetic;
			lastwipetic = 0;
		}

		// get real tics
		entertic = I_GetTime();
		realtics = entertic - oldentertics;
		oldentertics = entertic;

#ifdef DEBUGFILE
		if (!realtics)
			if (debugload)
				debugload--;
#endif

		if (!realtics && !singletics)
		{
			I_Sleep();
			continue;
		}

#ifdef HW3SOUND
		HW3S_BeginFrameUpdate();
#endif

		// don't skip more than 10 frames at a time
		// (fadein / fadeout cause massive frame skip!)
		if (realtics > 8)
			realtics = 1;

		// process tics (but maybe not if realtic == 0)
		TryRunTics(realtics);

		if (lastdraw || singletics || gametic > rendergametic)
		{
			rendergametic = gametic;
			rendertimeout = entertic+TICRATE/17;

			// Update display, next frame, with current state.
			D_Display();
			supdate = false;

			if (moviemode)
				M_SaveFrame();
			if (takescreenshot) // Only take screenshots after drawing.
				M_DoScreenShot();
		}
		else if (rendertimeout < entertic) // in case the server hang or netsplit
		{
			// Lagless camera! Yay!
			if (gamestate == GS_LEVEL && netgame)
			{
				if (splitscreen && camera2.chase)
					P_MoveChaseCamera(&players[secondarydisplayplayer], &camera2, false);
				if (camera.chase)
					P_MoveChaseCamera(&players[displayplayer], &camera, false);
			}
			D_Display();

			if (moviemode)
				M_SaveFrame();
			if (takescreenshot) // Only take screenshots after drawing.
				M_DoScreenShot();
		}

		// consoleplayer -> displayplayer (hear sounds from viewpoint)
		S_UpdateSounds(); // move positional sounds

		// check for media change, loop music..
		I_UpdateCD();

#ifdef HW3SOUND
		HW3S_EndFrameUpdate();
#endif
	}
}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
	advancedemo = true;
}

// =========================================================================
// D_SRB2Main
// =========================================================================

//
// D_StartTitle
//
void D_StartTitle(void)
{
	if (netgame)
	{
		if (gametype == GT_COOP)
		{
			G_SetGamestate(GS_WAITINGPLAYERS); // hack to prevent a command repeat

			if (server)
			{
				char mapname[6];

				strlcpy(mapname, G_BuildMapName(spstage_start), sizeof (mapname));
				strlwr(mapname);
				mapname[5] = '\0';

				COM_BufAddText(va("map %s\n", mapname));
			}
		}

		return;
	}

	// okay, stop now
	// (otherwise the game still thinks we're playing!)
	SV_StopServer();

	// In case someone exits out at the same time they start a time attack run,
	// reset modeattacking
	modeattacking = ATTACKING_NONE;

	// empty maptol so mario/etc sounds don't play in sound test when they shouldn't
	maptol = 0;

	gameaction = ga_nothing;
	playerdeadview = false;
	displayplayer = consoleplayer = 0;
	//demosequence = -1;
	gametype = GT_COOP;
	paused = false;
	advancedemo = false;
	F_StartTitleScreen();
	CON_ToggleOff();

	// Reset the palette
#ifdef HWRENDER
	if (rendermode == render_opengl)
		HWR_SetPaletteColor(0);
	else
#endif
	if (rendermode != render_none)
		V_SetPaletteLump("PLAYPAL");
}

//
// D_AddFile
//
static void D_AddFile(const char *file)
{
	size_t pnumwadfiles;
	char *newfile;

	for (pnumwadfiles = 0; startupwadfiles[pnumwadfiles]; pnumwadfiles++)
		;

	newfile = malloc(strlen(file) + 1);
	if (!newfile)
	{
		I_Error("No more free memory to AddFile %s",file);
	}
	strcpy(newfile, file);

	startupwadfiles[pnumwadfiles] = newfile;
}

static inline void D_CleanFile(void)
{
	size_t pnumwadfiles;
	for (pnumwadfiles = 0; startupwadfiles[pnumwadfiles]; pnumwadfiles++)
	{
		free(startupwadfiles[pnumwadfiles]);
		startupwadfiles[pnumwadfiles] = NULL;
	}
}

#ifndef _MAX_PATH
#define _MAX_PATH MAX_WADPATH
#endif

// ==========================================================================
// Identify the SRB2 version, and IWAD file to use.
// ==========================================================================

static void IdentifyVersion(void)
{
	char *srb2wad1, *srb2wad2;
	const char *srb2waddir = NULL;

#if (defined (__unix__) && !defined (MSDOS)) || defined (UNIXCOMMON) || defined (SDL)
	// change to the directory where 'srb2.srb' is found
	srb2waddir = I_LocateWad();
#endif

	// get the current directory (possible problem on NT with "." as current dir)
	if (srb2waddir)
	{
		strlcpy(srb2path,srb2waddir,sizeof (srb2path));
	}
	else
	{
#if !defined(_WIN32_WCE) && !defined(_PS3)
		if (getcwd(srb2path, 256) != NULL)
			srb2waddir = srb2path;
		else
#endif
		{
#ifdef _arch_dreamcast
			srb2waddir = "/cd";
#else
			srb2waddir = ".";
#endif
		}
	}

#if defined (macintosh) && !defined (SDL)
	// cwd is always "/" when app is dbl-clicked
	if (!stricmp(srb2waddir, "/"))
		srb2waddir = I_GetWadDir();
#endif
	// Commercial.
	srb2wad1 = malloc(strlen(srb2waddir)+1+8+1);
	srb2wad2 = malloc(strlen(srb2waddir)+1+8+1);
	if (srb2wad1 == NULL && srb2wad2 == NULL)
		I_Error("No more free memory to look in %s", srb2waddir);
	if (srb2wad1 != NULL)
		sprintf(srb2wad1, pandf, srb2waddir, "srb2.srb");
	if (srb2wad2 != NULL)
		sprintf(srb2wad2, pandf, srb2waddir, "srb2.wad");

	// will be overwritten in case of -cdrom or unix/win home
	snprintf(configfile, sizeof configfile, "%s" PATHSEP CONFIGFILENAME, srb2waddir);
	configfile[sizeof configfile - 1] = '\0';

	// Load the IWAD
	if (srb2wad2 != NULL && FIL_ReadFileOK(srb2wad2))
		D_AddFile(srb2wad2);
	else if (srb2wad1 != NULL && FIL_ReadFileOK(srb2wad1))
		D_AddFile(srb2wad1);
	else
		I_Error("SRB2.SRB/SRB2.WAD not found! Expected in %s, ss files: %s and %s\n", srb2waddir, srb2wad1, srb2wad2);

	if (srb2wad1)
		free(srb2wad1);
	if (srb2wad2)
		free(srb2wad2);

	// if you change the ordering of this or add/remove a file, be sure to update the md5
	// checking in D_SRB2Main

	// Add the maps
	D_AddFile(va(pandf,srb2waddir,"zones.dta"));

	// Add the players
	D_AddFile(va(pandf,srb2waddir, "player.dta"));

	// Add the weapons
	D_AddFile(va(pandf,srb2waddir,"rings.dta"));

	// Add our crappy patches to fix our bugs
	D_AddFile(va(pandf,srb2waddir,"patch.dta"));

#if !defined (SDL) || defined (HAVE_MIXER)
	{
#if defined (DC) && 0
		const char *musicfile = "music_dc.dta";
#else
		const char *musicfile = "music.dta";
#endif
		const char *musicpath = va(pandf,srb2waddir,musicfile);
		int ms = W_VerifyNMUSlumps(musicpath); // Don't forget the music!
		if (ms == 1)
			D_AddFile(musicpath);
		else if (ms == 0)
			I_Error("File %s has been modified with non-music lumps",musicfile);
	}
#endif
}

/* ======================================================================== */
// Just print the nice red titlebar like the original SRB2 for DOS.
/* ======================================================================== */
#ifdef PC_DOS
static inline void D_Titlebar(char *title1, char *title2)
{
	// SRB2 banner
	clrscr();
	textattr((BLUE<<4)+WHITE);
	clreol();
	cputs(title1);

	// standard srb2 banner
	textattr((RED<<4)+WHITE);
	clreol();
	gotoxy((80-strlen(title2))/2, 2);
	cputs(title2);
	normvideo();
	gotoxy(1,3);
}
#endif

//
// Center the title string, then add the date and time of compilation.
//
static inline void D_MakeTitleString(char *s)
{
	char temp[82];
	char *t;
	const char *u;
	INT32 i;

	for (i = 0, t = temp; i < 82; i++)
		*t++=' ';

	for (t = temp + (80-strlen(s))/2, u = s; *u != '\0' ;)
		*t++ = *u++;

	u = compdate;
	for (t = temp + 1, i = 11; i-- ;)
		*t++ = *u++;
	u = comptime;
	for (t = temp + 71, i = 8; i-- ;)
		*t++ = *u++;

	temp[80] = '\0';
	strcpy(s, temp);
}


//
// D_SRB2Main
//
void D_SRB2Main(void)
{
	INT32 p;
	char srb2[82]; // srb2 title banner
	char title[82];

	INT32 pstartmap = 1;
	boolean autostart = false;

	// keep error messages until the final flush(stderr)
#if !defined (PC_DOS) && !defined (_WIN32_WCE) && !defined(NOTERMIOS)
	if (setvbuf(stderr, NULL, _IOFBF, 1000))
		I_OutputMsg("setvbuf didnt work\n");
#endif

#ifdef GETTEXT
	// initialise locale code
	M_StartupLocale();
#endif

	// get parameters from a response file (eg: srb2 @parms.txt)
	M_FindResponseFile();

	// MAINCFG is now taken care of where "OBJCTCFG" is handled
	G_LoadGameSettings();

	// Test Dehacked lists
	DEH_Check();

	// identify the main IWAD file to use
	IdentifyVersion();

#if !defined (_WIN32_WCE) && !defined(NOTERMIOS)
	setbuf(stdout, NULL); // non-buffered output
#endif

#if defined (_WIN32_WCE) //|| defined (_DEBUG) || defined (GP2X)
	devparm = !M_CheckParm("-nodebug");
#else
	devparm = M_CheckParm("-debug");
#endif

	// for dedicated server
#if !defined (_WINDOWS) //already check in win_main.c
	dedicated = M_CheckParm("-dedicated") != 0;
#endif

	strcpy(title, "Sonic Robo Blast 2");
	strcpy(srb2, "Sonic Robo Blast 2");
	D_MakeTitleString(srb2);

#ifdef PC_DOS
	D_Titlebar(srb2, title);
#endif

#if defined (__OS2__) && !defined (SDL)
	// set PM window title
	snprintf(pmData->title, sizeof (pmData->title),
		"Sonic Robo Blast 2" VERSIONSTRING ": %s",
		title);
	pmData->title[sizeof (pmData->title) - 1] = '\0';
#endif

	if (devparm)
		CONS_Printf(M_GetText("Development mode ON.\n"));

	// default savegame
	strcpy(savegamename, SAVEGAMENAME"%u.ssg");

	{
		const char *userhome = D_Home(); //Alam: path to home

		if (!userhome)
		{
#if ((defined (__unix__) && !defined (MSDOS)) || defined(__APPLE__) || defined (UNIXCOMMON)) && !defined (__CYGWIN__) && !defined (DC) && !defined (PSP) && !defined(GP2X)
			I_Error("Please set $HOME to your home directory\n");
#elif defined (_WIN32_WCE) && 0
			if (dedicated)
				snprintf(configfile, sizeof configfile, "/Storage Card/SRB2DEMO/d"CONFIGFILENAME);
			else
				snprintf(configfile, sizeof configfile, "/Storage Card/SRB2DEMO/"CONFIGFILENAME);
#else
			if (dedicated)
				snprintf(configfile, sizeof configfile, "d"CONFIGFILENAME);
			else
				snprintf(configfile, sizeof configfile, CONFIGFILENAME);
#endif
		}
		else
		{
			// use user specific config file
#ifdef DEFAULTDIR
			snprintf(srb2home, sizeof srb2home, "%s" PATHSEP DEFAULTDIR, userhome);
			snprintf(downloaddir, sizeof downloaddir, "%s" PATHSEP "DOWNLOAD", srb2home);
			if (dedicated)
				snprintf(configfile, sizeof configfile, "%s" PATHSEP "d"CONFIGFILENAME, srb2home);
			else
				snprintf(configfile, sizeof configfile, "%s" PATHSEP CONFIGFILENAME, srb2home);

			// can't use sprintf since there is %u in savegamename
			strcatbf(savegamename, srb2home, PATHSEP);

			I_mkdir(srb2home, 0700);
#else
			snprintf(srb2home, sizeof srb2home, "%s", userhome);
			snprintf(downloaddir, sizeof downloaddir, "%s", userhome);
			if (dedicated)
				snprintf(configfile, sizeof configfile, "%s" PATHSEP "d"CONFIGFILENAME, userhome);
			else
				snprintf(configfile, sizeof configfile, "%s" PATHSEP CONFIGFILENAME, userhome);

			// can't use sprintf since there is %u in savegamename
			strcatbf(savegamename, userhome, PATHSEP);
#endif
		}

		configfile[sizeof configfile - 1] = '\0';

#ifdef _arch_dreamcast
	strcpy(downloaddir, "/ram"); // the dreamcast's TMP
#endif
	}

	// rand() needs seeded regardless of password
	srand((unsigned int)time(NULL));

	if (M_CheckParm("-password") && M_IsNextParm())
		D_SetPassword(M_GetNextParm());
	else
	{
		size_t z;
		char junkpw[25];
		for (z = 0; z < 24; z++)
			junkpw[z] = (char)(rand() & 64)+32;
		junkpw[24] = '\0';
		D_SetPassword(junkpw);
	}

	// add any files specified on the command line with -file wadfile
	// to the wad list
	if (!(M_CheckParm("-connect")))
	{
		if (M_CheckParm("-file"))
		{
			// the parms after p are wadfile/lump names,
			// until end of parms or another - preceded parm
			while (M_IsNextParm())
			{
				const char *s = M_GetNextParm();

				if (s) // Check for NULL?
				{
					if (!W_VerifyNMUSlumps(s))
						G_SetGameModified(true);
					D_AddFile(s);
				}
			}
		}
	}

	// get map from parms

	if (M_CheckParm("-server") || dedicated)
		netgame = server = true;

	if (M_CheckParm("-warp") && M_IsNextParm())
	{
		const char *word = M_GetNextParm();
		if (fastncmp(word, "MAP", 3))
			pstartmap = M_MapNumber(word[3], word[4]);
		else
			pstartmap = atoi(word);
		// Don't check if lump exists just yet because the wads haven't been loaded!
		// Just do a basic range check here.
		if (pstartmap < 1 || pstartmap > NUMMAPS)
			I_Error("Cannot warp to map %d (out of range)\n", pstartmap);
		else
		{
			if (!M_CheckParm("-server"))
				G_SetGameModified(true);
			autostart = true;
		}
	}

	CONS_Printf("Z_Init(): Init zone memory allocation daemon. \n");
	Z_Init();

	// adapt tables to SRB2's needs, including extra slots for dehacked file support
	P_PatchInfoTables();

	//---------------------------------------------------- READY TIME
	// we need to check for dedicated before initialization of some subsystems

	CONS_Printf("I_StartupTimer()...\n");
	I_StartupTimer();

	// Make backups of some SOCcable tables.
	P_BackupTables();

	// Setup default unlockable conditions
	M_SetupDefaultConditionSets();

	// load wad, including the main wad file
	CONS_Printf("W_InitMultipleFiles(): Adding IWAD and main PWADs.\n");
	if (!W_InitMultipleFiles(startupwadfiles))
#ifdef _DEBUG
		CONS_Error("A WAD file was not found or not valid.\nCheck the log to see which ones.\n");
#else
		I_Error("A WAD file was not found or not valid.\nCheck the log to see which ones.\n");
#endif
	D_CleanFile();

#if 1 // md5s last updated 3/22/14

	// Check MD5s of autoloaded files
	W_VerifyFileMD5(0, "ac309fb3c7d4b5b685e2cd26beccf0e8"); // srb2.srb/srb2.wad
	W_VerifyFileMD5(1, "a894044b555dfcc71865cee16a996e88"); // zones.dta
	W_VerifyFileMD5(2, "4c410c1de6e0440cc5b2858dcca80c3e"); // player.dta
	W_VerifyFileMD5(3, "85901ad4bf94637e5753d2ac2c03ea26"); // rings.dta
	W_VerifyFileMD5(4, "386ab4ffc8c9fb0fa62f788a16e5c218"); // patch.dta

	// don't check music.dta because people like to modify it, and it doesn't matter if they do
	// ...except it does if they slip maps in there, and that's what W_VerifyNMUSlumps is for.
#endif

	mainwads = 5; // there are 5 wads not to unload

	cht_Init();

	//---------------------------------------------------- READY SCREEN
	// we need to check for dedicated before initialization of some subsystems

	CONS_Printf("I_StartupGraphics()...\n");
	I_StartupGraphics();

	//--------------------------------------------------------- CONSOLE
	// setup loading screen
	SCR_Startup();

	// we need the font of the console
	CONS_Printf("HU_Init(): Setting up heads up display.\n");
	HU_Init();

	COM_Init();
	// libogc has a CON_Init function, we must rename SRB2's CON_Init in WII/libogc
#ifndef _WII
	CON_Init();
#else
	CON_InitWii();
#endif

	D_RegisterServerCommands();
	D_RegisterClientCommands(); // be sure that this is called before D_CheckNetGame
	R_RegisterEngineStuff();
	S_RegisterSoundStuff();

	I_RegisterSysCommands();

	//--------------------------------------------------------- CONFIG.CFG
	M_FirstLoadConfig(); // WARNING : this do a "COM_BufExecute()"

	G_LoadGameData();

#if (defined (__unix__) && !defined (MSDOS)) || defined (UNIXCOMMON) || defined (SDL)
	VID_PrepareModeList(); // Regenerate Modelist according to cv_fullscreen
#endif

	// set user default mode or mode set at cmdline
	SCR_CheckDefaultMode();

	wipegamestate = gamestate;

	P_InitMapHeaders();
	savedata.lives = 0; // flag this as not-used

	//------------------------------------------------ COMMAND LINE PARAMS

	// Initialize CD-Audio
	if (M_CheckParm("-usecd") && !dedicated)
		I_InitCD();

	if (M_CheckParm("-noupload"))
		COM_BufAddText("downloading 0\n");

	CONS_Printf("M_Init(): Init miscellaneous info.\n");
	M_Init();

	CONS_Printf("R_Init(): Init SRB2 refresh daemon.\n");
	R_Init();

	// setting up sound
	CONS_Printf("S_Init(): Setting up sound.\n");
	if (M_CheckParm("-nosound"))
		nosound = true;
	if (M_CheckParm("-nomusic")) // combines -nomidimusic and -nodigmusic
		nomidimusic = nodigimusic = true;
	else
	{
		if (M_CheckParm("-nomidimusic"))
			nomidimusic = true; ; // WARNING: DOS version initmusic in I_StartupSound
		if (M_CheckParm("-nodigmusic"))
			nodigimusic = true; // WARNING: DOS version initmusic in I_StartupSound
	}
	I_StartupSound();
	I_InitMusic();
	S_Init(cv_soundvolume.value, cv_digmusicvolume.value, cv_midimusicvolume.value);

	CONS_Printf("ST_Init(): Init status bar.\n");
	ST_Init();

	if (M_CheckParm("-room"))
	{
		if (!M_IsNextParm())
			I_Error("usage: -room <room_id>\nCheck the Master Server's webpage for room ID numbers.\n");
		ms_RoomId = atoi(M_GetNextParm());

#ifdef UPDATE_ALERT
		GetMODVersion_Console();
#endif
	}

	// init all NETWORK
	CONS_Printf("D_CheckNetGame(): Checking network game status.\n");
	if (D_CheckNetGame())
		autostart = true;

	// check for a driver that wants intermission stats
	// start the apropriate game based on parms
	if (M_CheckParm("-metal"))
	{
		G_RecordMetal();
		autostart = true;
	}
	else if (M_CheckParm("-record") && M_IsNextParm())
	{
		G_RecordDemo(M_GetNextParm());
		autostart = true;
	}

	// user settings come before "+" parameters.
	if (dedicated)
		COM_ImmedExecute(va("exec \"%s"PATHSEP"adedserv.cfg\"\n", srb2home));
	else
		COM_ImmedExecute(va("exec \"%s"PATHSEP"autoexec.cfg\" -noerror\n", srb2home));

	if (!autostart)
		M_PushSpecialParameters(); // push all "+" parameters at the command buffer

	// demo doesn't need anymore to be added with D_AddFile()
	p = M_CheckParm("-playdemo");
	if (!p)
		p = M_CheckParm("-timedemo");
	if (p && M_IsNextParm())
	{
		char tmp[MAX_WADPATH];
		// add .lmp to identify the EXTERNAL demo file
		// it is NOT possible to play an internal demo using -playdemo,
		// rather push a playdemo command.. to do.

		strcpy(tmp, M_GetNextParm());
		// get spaced filename or directory
		while (M_IsNextParm())
		{
			strcat(tmp, " ");
			strcat(tmp, M_GetNextParm());
		}

		FIL_DefaultExtension(tmp, ".lmp");

		CONS_Printf(M_GetText("Playing demo %s.\n"), tmp);

		if (M_CheckParm("-playdemo"))
		{
			singledemo = true; // quit after one demo
			G_DeferedPlayDemo(tmp);
		}
		else
			G_TimeDemo(tmp);

		G_SetGamestate(GS_NULL);
		wipegamestate = GS_NULL;
		return;
	}

	if (M_CheckParm("-ultimatemode"))
	{
		autostart = true;
		ultimatemode = true;
	}

	if (autostart || netgame || M_CheckParm("+connect") || M_CheckParm("-connect"))
	{
		gameaction = ga_nothing;

		CV_ClearChangedFlags();

		// Do this here so if you run SRB2 with eg +timelimit 5, the time limit counts
		// as having been modified for the first game.
		M_PushSpecialParameters(); // push all "+" parameter at the command buffer

		if (M_CheckParm("-gametype") && M_IsNextParm())
		{
			// from Command_Map_f
			INT32 j;
			INT16 newgametype = -1;
			const char *sgametype = M_GetNextParm();

			for (j = 0; gametype_cons_t[j].strvalue; j++)
				if (!strcasecmp(gametype_cons_t[j].strvalue, sgametype))
				{
					newgametype = (INT16)gametype_cons_t[j].value;
					break;
				}
			if (!gametype_cons_t[j].strvalue) // reached end of the list with no match
			{
				j = atoi(sgametype); // assume they gave us a gametype number, which is okay too
				if (j >= 0 && j < NUMGAMETYPES)
					newgametype = (INT16)j;
			}

			if (newgametype != -1)
			{
				j = gametype;
				gametype = newgametype;
				D_GameTypeChanged(j);
			}
		}

		if (server && !M_CheckParm("+map") && !M_CheckParm("+connect")
			&& !M_CheckParm("-connect"))
		{
			// Prevent warping to nonexistent levels
			if (W_CheckNumForName(G_BuildMapName(pstartmap)) == LUMPERROR)
				I_Error("Could not warp to %s (map not found)\n", G_BuildMapName(pstartmap));
			// Prevent warping to locked levels
			// ... unless you're in a dedicated server.  Yes, technically this means you can view any level by
			// running a dedicated server and joining it yourself, but that's better than making dedicated server's
			// lives hell.
			else if (!dedicated && M_MapLocked(pstartmap))
				I_Error("You need to unlock this level before you can warp to it!\n");
			else
				D_MapChange(pstartmap, gametype, ultimatemode, true, 0, false, false);
		}
	}
	else if (M_CheckParm("-skipintro"))
	{
		CON_ToggleOff();
		CON_ClearHUD();
		F_StartTitleScreen();
	}
	else
		F_StartIntro(); // Tails 03-03-2002

	if (dedicated && server)
	{
		pagename = "TITLESKY";
		levelstarttic = gametic;
		G_SetGamestate(GS_LEVEL);
		if (!P_SetupLevel(false))
			I_Quit(); // fail so reset game stuff
	}
}

const char *D_Home(void)
{
	const char *userhome = NULL;

#ifdef ANDROID
	return "/data/data/org.srb2/";
#endif
#ifdef _arch_dreamcast
	char VMUHOME[] = "HOME=/vmu/a1";
	putenv(VMUHOME); //don't use I_PutEnv
#endif

	if (M_CheckParm("-home") && M_IsNextParm())
		userhome = M_GetNextParm();
	else
	{
#if defined (GP2X)
		usehome = false; //let use the CWD
		return NULL;
#elif !((defined (__unix__) && !defined (MSDOS)) || defined(__APPLE__) || defined (UNIXCOMMON)) && !defined (__APPLE__) && !defined(_WIN32_WCE)
		if (FIL_FileOK(CONFIGFILENAME))
			usehome = false; // Let's NOT use home
		else
#endif
			userhome = I_GetEnv("HOME"); //Alam: my new HOME for srb2
	}
#if defined (_WIN32) && !defined(_WIN32_WCE) //Alam: only Win32 have APPDATA and USERPROFILE
	if (!userhome && usehome) //Alam: Still not?
	{
		char *testhome = NULL;
		testhome = I_GetEnv("APPDATA");
		if (testhome != NULL
			&& (FIL_FileOK(va("%s" PATHSEP "%s" PATHSEP CONFIGFILENAME, testhome, DEFAULTDIR))))
		{
			userhome = testhome;
		}
	}
#ifndef __CYGWIN__
	if (!userhome && usehome) //Alam: All else fails?
	{
		char *testhome = NULL;
		testhome = I_GetEnv("USERPROFILE");
		if (testhome != NULL
			&& (FIL_FileOK(va("%s" PATHSEP "%s" PATHSEP CONFIGFILENAME, testhome, DEFAULTDIR))))
		{
			userhome = testhome;
		}
	}
#endif// !__CYGWIN__
#endif// _WIN32
	if (usehome) return userhome;
	else return NULL;
}
