#define TITLE			"Graphics Synthesizer Mode Selector"
#define VERSION			"0.29"
#define AUTHORS			"doctorxyz and dlanor"
/*
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
*/

#include <stdio.h>
#include <kernel.h>
#include <sifrpc.h>
#include <libhdd.h>
#include <debug.h>
#include <malloc.h>
#include <fileXio_rpc.h>
#include <loadfile.h>
#include <fileio.h>
#include <string.h>
#include <libmc.h>
#include <libpad.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "modules.h"
#include "timer.h"
#include "pad.h"

#include <gsKit.h>

#include <libjpg.h>

int	patcher_enabled = 0;

// SetGsCrt params
volatile u32 interlace, mode, ffmd;

// GS Registers
#define GS_BGCOLOUR *((volatile unsigned long int*)0x120000E0)
volatile u64 display, syncv, act_syncv, smode2;

// DisplayX GS Registers' Bit Fields
//RA/doctorxyz NB: Into GSM and gsKit, gs_dw and gs_dh are 1 unit higher than the real GS register values
//RA NB: so for a 480p resolution we use 480, though the register gets 479
volatile u32 gs_dx, gs_dy, gs_magh, gs_magv, gs_dw, gs_dh;

typedef struct gsm_vmode {
	u32	interlace;
	u32	mode;
	u32	field;
	u64	display;
	u64	syncv;
	u64	smode2;
} GSM_vmode;

typedef struct gsm_exit_option {
	u8	id;
	char description[12];
	char elf_path[0x40];
} GSM_exit_option;


// gsKit Color vars
// Object Creation Macro for RGBAQ Color Values
// Reference table: 	"RGB to Color Name Mapping (Triplet and Hex)", by By Kevin J. Walsh (http://web.njit.edu/~kevin/rgb.txt.html)
//
// safe NTSC colors:
// The following RGB values were later adjusted by lee4 in order to get safe NTSC colors, because "Basic computer RGB gamma is not compatible use for NTSC output.
// NTSC colors are dull, as for me I just add gray tint to RGB color to get that effect." (lee4)
// Those intested on how to convert an ordinary RGB color to a safe NTSC ones should follow these links:
// --> "Making Computer Graphics NTSC Compliant" by by Tom Buehler (http://erikdemaine.org/SoCG2003_multimedia/graphics.html)
// --> Use Photoshop "color gamma for NTSC" feature - Go (http://www.adobe.com/cfusion/search/index.cfm?cat=support&loc=en_us) and look for "NTSC color"
// --> Google for "safe NTSC color" (http://www.google.com/search?q=safe+NTSC+color)
// --> How USA TV stations test their video output (http://en.wikipedia.org/wiki/NTSC_color_bars)
u64 Black = GS_SETREG_RGBAQ(0x00,0x00,0x00,0x00,0x00);
u64 White = GS_SETREG_RGBAQ(0xEB,0xEB,0xEB,0x00,0x00);
u64 Red = GS_SETREG_RGBAQ(0xB4,0x10,0x10,0x00,0x00);
u64 Green = GS_SETREG_RGBAQ(0x10,0xB4,0x10,0x00,0x00);
u64 Blue = GS_SETREG_RGBAQ(0x10,0x10,0xB4,0x00,0x00);
u64 Yellow = GS_SETREG_RGBAQ(0xB4,0xB4,0x10,0x00,0x00);
u64 Trans = GS_SETREG_RGBAQ(0x80,0x80,0x80,0xA0,0x00); // 0xA0 in alpha channel give some more color to icons

u32 WhiteFont = GS_SETREG_RGBAQ(0xEB,0xEB,0xEB,0x80,0x00);
u32 BlackFont = GS_SETREG_RGBAQ(0x00,0x00,0x00,0x80,0x00);
u32 RedFont = GS_SETREG_RGBAQ(0xB4,0x10,0x10,0x80,0x00);
u32 GreenFont = GS_SETREG_RGBAQ(0x10,0xB4,0x10,0x80,0x00);
u32 BlueFont = GS_SETREG_RGBAQ(0x10,0x10,0xB4,0x80,0x00);
u32 YellowFont = GS_SETREG_RGBAQ(0xB4,0xB4,0x10,0x80,0x00);
u32 DarkOrangeFont = GS_SETREG_RGBAQ(0xB4,0x8A,0x10,0x80,0x00);
u32 DeepSkyBlueFont = GS_SETREG_RGBAQ(0x10,0xB4,0xB4,0x80,0x00);
u32 OrangeRedFont = GS_SETREG_RGBAQ(0xB4,0x2F,0x10,0x80,0x00);
u32 MagentaFont = GS_SETREG_RGBAQ(0xB4,0x10,0xB4,0x80,0x00);

u32 BlueTrans = GS_SETREG_RGBAQ(0x10,0x10,0xB4,0x40,0x00);
u32 RedTrans = GS_SETREG_RGBAQ(0xB4,0x10,0x10,0x60,0x00);
u32 GreenTrans = GS_SETREG_RGBAQ(0x10,0xB4,0x10,0x50,0x00);
u32 WhiteTrans = GS_SETREG_RGBAQ(0xEB,0xEB,0xEB,0x50,0x00);

// gsGlobal is required for all painting functiions of gsKit.
GSGLOBAL *gsGlobal;

int gskit_vmode;

// Font used for printing text.
GSFONTM *gsFontM;


//  Splash Screen Texture Skin
GSTEXTURE TexSkin;



int updateScr_1;     //flags screen updates for drawScr()
int updateScr_2;     //used for anti-flicker delay in drawScr()
u64 updateScr_t = 0; //exit time of last drawScr()

u64 WaitTime = 0; //used for time waiting

/// DTV 576 Progressive Scan (720x576)
#define GS_MODE_DTV_576P  0x53

typedef struct gsm_predef_vmode {
	u8	id;
	u8	category;
	char description[36];
	u8	interlace;
	u8	mode;
	u8	ffmd;
	u64	display;
	u64	syncv;
} GSM_predef_vmode;

struct gsm_settings *GSM = NULL;

// Prototypes for External Functions
void RunLoaderElf(char *filename, char *);

//Variadic macro by doctorxyz
#define gsKit_fontm_printf_scaled(gsGlobal, gsFontM, X, Y, Z, scale, color, format, args...) \
	sprintf(tempstr, format, args); \
	gsKit_fontm_print_scaled(gsGlobal, gsFontM, X, Y, Z, scale, color, tempstr);
	
#define make_display_magic_number(dh, dw, magv, magh, dy, dx) \
        (((u64)(dh-1)<<44) | ((u64)(dw-1)<<32) | ((u64)(magv)<<27) | \
         ((u64)(magh)<<23) | ((u64)(dy)<<12)   | ((u64)(dx)<<0)     )
//RA NB: dw and dh are here 1 unit higher than the real GS register values
//RA NB: so for a 480p resolution we use 480, though the register gets 479
//RA NB: which is thus what we here embed in the bits of the return value

// After some investigation, I found this interesting FONTM character number codes ;-)
#define FONTM_CIRCLE			"\f0090"
#define FONTM_FILLED_CIRCLE		"\f0091"
#define FONTM_SQUARE			"\f0095"
#define FONTM_FILLED_SQUARE		"\f0096"
#define FONTM_TRIANGLE			"\f0097"
#define FONTM_FILLED_TRIANGLE	"\f0098"
#define FONTM_CROSS				"\f1187"

// VMODE TYPES
#define PS1_VMODE	1
#define SDTV_VMODE	2
#define HDTV_VMODE	3
#define VGA_VMODE	4
	
#define MAKE_J(func)		(u32)( (0x02 << 26) | (((u32)func) / 4) )	// Jump (MIPS instruction)
#define NOP					0x00000000									// No Operation (MIPS instruction)

#include "misc.c"

//----------------------------------------------------------------------------
void Timer_delay(int timeout){ //Will delay at least timeout ms (at most 1 more)
	u64 start_time;

	start_time = Timer();
	while(Timer() < (start_time + (u64) (timeout + 1)));
} //ends Timer_delay
//----------------------------------------------------------------------------
void	CleanUp(void)
{
	TimerEnd();
	padPortClose(1,0);
	padPortClose(0,0);
	padEnd();
}

//------------------------------------------------------------------------------------------------------------------------
// Clear_Screen - Taken from the fmcb1.7 sources
//------------------------------------------------------------------------------------------------------------------------
void Clear_Screen(void)
{

	gsKit_clear(gsGlobal, Black);
	gsKit_prim_sprite_texture(gsGlobal,
	 &TexSkin, 0, 0, 0, 0,
	 gsGlobal->Width, gsGlobal->Height, TexSkin.Width, TexSkin.Height,
	 0, Trans);
	 
	//gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, gsGlobal->Heigth, 0, Trans);

}
//endfunc Clear_Screen
//------------------------------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Setup_GS - Took and Adopted from the fmcb1.7, mcboot1.5 and HD_ProjectV1.07 sources
// I got it from Compilable PS2 source collection
// by Lazy Bastard from GHSI (http://www.gshi.org/vb/showthread.php?t=3098)
//----------------------------------------------------------------------------
void Setup_GS()
{

	// GS Init

	//The following line eliminates overflow
	gsGlobal = gsKit_init_global_custom(GS_RENDER_QUEUE_OS_POOLSIZE+GS_RENDER_QUEUE_OS_POOLSIZE/2, GS_RENDER_QUEUE_PER_POOLSIZE);

	// Buffer Init
	gsGlobal->PrimAAEnable = GS_SETTING_ON;
	gsGlobal->DoubleBuffering = GS_SETTING_OFF;

	gsGlobal->ZBuffering      = GS_SETTING_OFF;


	gsGlobal->Width = 640;

	if(gsKit_detect_signal() == GS_MODE_NTSC) {
		gsGlobal->Height = 448;
	} else {
		gsGlobal->Height = 512;
	}
	if(mode == GS_MODE_VGA_640_60) {
		gsGlobal->Mode = GS_MODE_VGA_640_60;
		gsGlobal->Height = 480;
	}

	// In order to have proper displaying on GSM OSD (always showed after enabling the patcher),
	// we must handle these params to avoid showing beyond the current framebuffer limits,
	// that would result on wrong sized display (doubled rows, and garbage at the bottom, etc.)
	if(patcher_enabled == 1) {
		gsGlobal->Mode = mode;
		gsGlobal->Interlace = interlace;
		gsGlobal->Field = ffmd;
		//gsGlobal->Height = (gs_dh / (gs_magv+1)); 
		//Trying to solve GSM OSD gsKit issues, using the example "basic.c - Example demonstrating basic gsKit operation"
		if(mode == GS_MODE_DTV_1080I) {
			gsGlobal->Height = 540;
			gsGlobal->PSM = GS_PSM_CT16;
			gsGlobal->PSMZ = GS_PSMZ_16;
			gsGlobal->Dithering = GS_SETTING_ON;
		}
		if(mode == GS_MODE_DTV_720P) {
			gsGlobal->Height = 360;
			gsGlobal->PSM = GS_PSM_CT16;
			gsGlobal->PSMZ = GS_PSMZ_16;
		}
	}

	// Screen Init - Here remaining gsGlobal params are setted to the defaults, based on these ones:
	// gsGlobal->Interlace
	// gsGlobal->Mode
	// gsGlobal->Field
	// gsGlobal->Width
	// gsGlobal->Height
	gsKit_init_screen(gsGlobal);

	gsKit_mode_switch(gsGlobal, GS_ONESHOT);
	gsKit_clear(gsGlobal, Black);

	//Enable transparency
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

}
//----------------------------------------------------------------------------
//endfunc Setup_GS
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Draw_Screen - Taken from the fmcb1.7, mcboot1.5 and HD_ProjectV1.07 sources
// I got it from Compilable PS2 source collection
// by Lazy Bastard from GHSI (http://www.gshi.org/vb/showthread.php?t=3098)
//----------------------------------------------------------------------------
void Draw_Screen(void)
{
	if(updateScr_2){            //Did we render anything last time
		while(Timer() < updateScr_t+5);  //if so, delay to complete rendering
	}
	gsKit_sync_flip(gsGlobal);  //Await sync and flip buffers
	gsKit_queue_exec(gsGlobal); //Start rendering recent transfers for NEXT time
	updateScr_t = Timer();      //Note the time when the rendering started
	updateScr_2 = updateScr_1;  //Note if this rendering had expected updates
	updateScr_1 = 0;            //Note that we've nothing expected for next time
} //NB: Apparently the GS keeps rendering while we continue with other work
//----------------------------------------------------------------------------
//endfunc Draw_Screen
//----------------------------------------------------------------------------

#include "api.c"

#include "main.c"

