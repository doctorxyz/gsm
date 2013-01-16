/*
#
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
#
*/

#define TITLE			"Graphics Synthesizer Mode Selector"
#define VERSION			"0.36b (can be bootable and relaunchable by FCMB E1 launch keys)"
#define AUTHORS			"doctorxyz and dlanor"

#include <syscallnr.h>
#include <kernel.h>
#include <debug.h>
#include <sifrpc.h>
#include <fileXio_rpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <sbv_patches.h>
#include <iopcontrol.h>
#include <malloc.h>
#include <libmc.h>
#include <fileio.h>
#include <string.h>
#include <libhdd.h>
#include <libpad.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "timer.h"
#include "pad.h"

#include <gsKit.h>

#include <libjpg.h>

#define _GSM_ENGINE_ __attribute__((section(".gsm_engine")))		// Resident section

extern void *Old_SetGsCrt _GSM_ENGINE_;

extern volatile u32 Source_INTERLACE _GSM_ENGINE_;
extern volatile u32 Source_MODE _GSM_ENGINE_;
extern volatile u32 Source_FFMD _GSM_ENGINE_;

extern volatile u64 Calculated_DISPLAY _GSM_ENGINE_;

extern volatile u32 Target_INTERLACE _GSM_ENGINE_;
extern volatile u32 Target_MODE _GSM_ENGINE_;
extern volatile u32 Target_FFMD _GSM_ENGINE_;

extern volatile u64 Target_SMODE2 _GSM_ENGINE_;
extern volatile u64 Target_DISPLAY _GSM_ENGINE_;
extern volatile u64 Target_SYNCV _GSM_ENGINE_;

extern volatile u8 automatic_adaptation _GSM_ENGINE_;
extern volatile u8 DISPLAY_fix _GSM_ENGINE_;
extern volatile u8 SMODE2_fix _GSM_ENGINE_;
extern volatile u8 SYNCV_fix _GSM_ENGINE_;

extern volatile u32 X_offset _GSM_ENGINE_;
extern volatile u32 Y_offset _GSM_ENGINE_;

extern volatile void Hook_SetGsCrt() _GSM_ENGINE_;
extern volatile void GSHandler() _GSM_ENGINE_;

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
	char description[34];
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
        (((u64)(dh)<<44) | ((u64)(dw)<<32) | ((u64)(magv)<<27) | \
         ((u64)(magh)<<23) | ((u64)(dy)<<12)   | ((u64)(dx)<<0)     )

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

/*-------------------*/
/* Update GSM params */
/*-------------------*/
// Update parameters to be enforced by Hook_SetGsCrt syscall hook and GSHandler service routine functions
void UpdateGSMParams(u32 interlace, u32 mode, u32 ffmd, u64 display, u64 syncv, u64 smode2, int dx_offset, int dy_offset)
{
	DI();
	ee_kmode_enter();

	Target_INTERLACE 	= (u32) interlace;
	Target_MODE 		= (u32) mode;
	Target_FFMD 		= (u32) ffmd;

	Target_SMODE2 		= (u8) smode2;
	Target_DISPLAY 		= (u64) display;
	Target_SYNCV 		= (u64) syncv;

	automatic_adaptation	= 0;	// Automatic Adaptation -> 0 = On, 1 = Off ; Default = 0 = On
	DISPLAY_fix		= 0;	// DISPLAYx Fix ---------> 0 = On, 1 = Off ; Default = 0 = On
	SMODE2_fix		= 0;	// SMODE2 Fix -----------> 0 = On, 1 = Off ; Default = 0 = On
	SYNCV_fix		= 0;	// SYNCV Fix ------------> 0 = On, 1 = Off ; Default = 0 = On

	X_offset		= dx_offset;	// X-axis offset -> Use it only when automatic adaptations formulas don't suffice
	Y_offset		= dy_offset;	// Y-axis offset -> Use it only when automatic adaptations formulas don't suffice

	__asm__ __volatile__(
		"sync.l;"
		"sync.p;"
	);

	ee_kmode_exit();
	EI();
}

/*---------------------------------------------------------*/
/* Disable Graphics Synthesizer Mode Selector (a.k.a. GSM) */
/*---------------------------------------------------------*/
static inline void DeInitGSM(void)
{
	#define GS_BGCOLOUR *((volatile unsigned long int*)0x120000E0)
	//Search for Syscall Table in ROM
	u32 i;
	u32 KernelStart;
	u32* Pointer;
	u32* SyscallTable;
	KernelStart = 0;
	for (i = 0x1fc00000+0x300000; i < 0x1fc00000+0x3fffff; i+=4)
	{
		if ( *(u32*)(i+0) == 0x40196800 )
		{
			if ( *(u32*)(i+4) == 0x3c1a8001 )
			{
				KernelStart = i - 8;
				break;
			}
		}
	}
	if (KernelStart == 0)
	{
		GS_BGCOLOUR = 0x00ffff;	// Yellow	
		while (1) {;}
	}
	Pointer = (u32 *) (KernelStart + 0x2f0);
	SyscallTable = (u32*)((Pointer[0] << 16) | (Pointer[2] & 0xFFFF));
	SyscallTable = (u32*)((u32)SyscallTable & 0x1fffffff);
	SyscallTable = (u32*)((u32)SyscallTable + KernelStart);
	
	DI();
	ee_kmode_enter();
	// Restore SetGsCrt (even when it isn't hooked)
	SetSyscall(2, (void*)SyscallTable[2]);
	// Remove all breakpoints (even when they aren't enabled)
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"
	"li $k0, 0x8000\n"
	"mtbpc $k0\n"			// All breakpoints off (BED = 1)
	"sync.p\n"				// Await instruction completion
	".set at\n"
	".set reorder\n"
	);
	ee_kmode_exit();
	EI();
}

/*--------------------------*/
/* Call sceSetGsCrt syscall */
/*--------------------------*/

void gs_setmode(int Interlace, int Mode, int FFMD)
{

	__asm__ __volatile__(
		"li  $3, 0x02      \n"   // Specify the sceSetGsCrt syscall. (reg 3)
		// Specify the 3 arguments (regs 4 - 6)
		"add $4, $0, %0    \n"   // Interlace
		"add $5, $0, %1    \n"   // Mode
		"add $6, $0, %2    \n"   // FFMD

		"syscall           \n"   // Perform the syscall
		"nop               \n"   //

		:
		: "r" (Interlace), "r" (Mode), "r" (FFMD)
	);
  }

//----------------------------------------------------------------------------
void delay(int count)
{
	int i;
	int ret;
	for (i = 0; i < count; i++) {
		ret = 0x01000000;
			while(ret--) asm("nop\nnop\nnop\nnop");
	}
}

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

//----------------------------------------------------------------------------
/* Loads SIO2MAN, MCMAN, MCSERV and PADMAN modules */
void LoadModules(void)
{
	int id;

    if ((id = SifLoadModule("rom0:SIO2MAN", 0, NULL)) < 0) {
		printf("\tERROR: cannot load sio2man: %d.\n", id);
		return;
	}
	if ((id = SifLoadModule("rom0:MCMAN", 0, NULL)) < 0) {
		printf("\tERROR: cannot load mcman: %d.\n", id);
		return;
	}
    if ((id = SifLoadModule("rom0:MCSERV", 0, NULL)) < 0) {
		printf("\tERROR: cannot load mcserv: %d.\n", id);
		return;
	}
	
	mcInit(MC_TYPE_MC);
	
    if ((id = SifLoadModule("rom0:PADMAN", 0, NULL)) < 0) {
		printf("\tERROR: cannot load padman: %d.\n", id);
		return;
	}
}


int main(void)
{   

	//---------- Start of variables stuff ----------
	// Launch ELF
	char elf_path[0x40];
	char party[1] = {0};

	int edge_size = 0;  //Used for screen rectangle drawing
	int text_height = 0; //Used for text rows
 
	// Pre-defined vmodes 
	// Some of following vmodes gives BOSD and/or freezing, depending on the console BIOS version, TV/Monitor set, PS2 cable (composite, component, VGA, ...)
	// Therefore there are many variables involved here that can lead us to success or faild depending on the circumstances above mentioned.
	//
	//	id	category	description		interlace		mode			 ffmd	   display                         dh   dw     magv magh dy  dx    syncv
	//	--	--------	-----------		---------		----			 -----	   ----------------------------    --   --     ---- ---- --  --    ------------------
	volatile static GSM_predef_vmode predef_vmode[15] = {

		{  0, SDTV_VMODE,"NTSC                             ",	GS_INTERLACED,		GS_MODE_NTSC,		GS_FIELD,	(u64)make_display_magic_number( 447, 2559,   0,   3,   46, 700), 0x00C7800601A01801},
		{  1, SDTV_VMODE,"NTSC 'Non Interlaced'            ",	GS_INTERLACED,		GS_MODE_NTSC,		GS_FRAME,	(u64)make_display_magic_number( 223, 2559,   0,   3,   26, 700), 0x00C7800601A01802},
		{  2, SDTV_VMODE,"PAL                              ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FIELD,	(u64)make_display_magic_number( 511, 2559,   0,   3,   70, 720), 0x00A9000502101401},
		{  3, SDTV_VMODE,"PAL 'Non Interlaced'             ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FRAME,	(u64)make_display_magic_number( 255, 2559,   0,   3,   37, 720), 0x00A9000502101404},
		{  4, SDTV_VMODE,"PAL @60Hz                        ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FIELD,	(u64)make_display_magic_number( 447, 2559,   0,   3,   46, 700), 0x00C7800601A01801},
		{  5, SDTV_VMODE,"PAL @60Hz 'Non Interlaced'       ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FRAME,	(u64)make_display_magic_number( 223, 2559,   0,   3,   26, 700), 0x00C7800601A01802},
		{  6, PS1_VMODE, "PS1 NTSC (HDTV 480p @60Hz)       ",	GS_NONINTERLACED,	GS_MODE_DTV_480P,	GS_FRAME,	(u64)make_display_magic_number( 255, 2559,   0,   1,   12, 736), 0x00C78C0001E00006},
		{  7, PS1_VMODE, "PS1 PAL (HDTV 576p @50Hz)        ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,	GS_FRAME,	(u64)make_display_magic_number( 255, 2559,   0,   1,   23, 756), 0x00A9000002700005},
		{  8, HDTV_VMODE,"HDTV 480p @60Hz                  ",	GS_NONINTERLACED,	GS_MODE_DTV_480P,	GS_FRAME, 	(u64)make_display_magic_number( 479, 1279,   0,   1,   51, 308), 0x00C78C0001E00006},
		{  9, HDTV_VMODE,"HDTV 576p @50Hz                  ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,	GS_FRAME,	(u64)make_display_magic_number( 575, 1279,   0,   1,   64, 320), 0x00A9000002700005},
		{ 10, HDTV_VMODE,"HDTV 720p @60Hz                  ",	GS_NONINTERLACED,	GS_MODE_DTV_720P,	GS_FRAME, 	(u64)make_display_magic_number( 719, 1279,   1,   1,   24, 302), 0x00AB400001400005},
		{ 11, HDTV_VMODE,"HDTV 1080i @60Hz                 ",	GS_INTERLACED,		GS_MODE_DTV_1080I,	GS_FIELD, 	(u64)make_display_magic_number(1079, 1919,   1,   2,   48, 238), 0x0150E00201C00005},
		{ 12, HDTV_VMODE,"HDTV 1080i @60Hz 'Non Interlaced'",	GS_INTERLACED,		GS_MODE_DTV_1080I,	GS_FRAME, 	(u64)make_display_magic_number(1079, 1919,   0,   2,   48, 238), 0x0150E00201C00005},
		{ 13, VGA_VMODE, "VGA 640x480p @60Hz               ",	GS_NONINTERLACED,	GS_MODE_VGA_640_60,	GS_FRAME, 	(u64)make_display_magic_number( 479, 1279,   0,   1,   54, 276), 0x004780000210000A},
		{ 14, VGA_VMODE, "VGA 640x960i @60Hz               ",	GS_INTERLACED,		GS_MODE_VGA_640_60,	GS_FIELD,	(u64)make_display_magic_number( 959, 1279,   1,   1,  128, 291), 0x004F80000210000A}

	}; //ends predef_vmode definition

	u32 predef_vmode_size = 	sizeof( predef_vmode ) / sizeof( predef_vmode[0] );

	// predef_vmode_toggle -> Aux for pre-defined vmodes
	//			999: None value chosen yet
	// 			Other values: Value chosen by user
	int predef_vmode_toggle = 999;

	//----------------------------------------------------------------------------

	// X and Y axis offsets
	int dx_offset = 0;
	int dy_offset = 0;

	//----------------------------------------------------------------------------

	// Exit Method
	//-
	//	id	description		path
	//	--	-----------		--------
 	volatile static GSM_exit_option exit_option[9] = {
		{ 0, "PS2 BROWSER", "PS2 BROWSER"},
		{ 1, "DEV1 BOOT  ", "mc0:BOOT/BOOT.ELF\0"},
		{ 2, "DEV1 BOOT  ", "mc0:APPS/BOOT.ELF\0"},
		{ 3, "HDLoader   ", "mc0:BOOT/HDLOADER.ELF\0"},
		{ 4, "PS2LINK    ", "mc0:BWLINUX/PS2LINK.ELF\0"},
		{ 5, "DEV1 boot  ", "mc0:boot/boot.elf\0"},
		{ 6, "DEV1 boot1 ", "mc0:boot/boot1.elf\0"},
		{ 7, "DEV1 boot2 ", "mc0:boot/boot2.elf\0"},
		{ 8, "DEV1 boot3 ", "mc0:boot/boot3.elf\0"}
	};//ends exit_option definition

	u32 exit_option_size = 	sizeof( exit_option ) / sizeof( exit_option[0] );

	int exit_option_toggle = 999;

	//----------------------------------------------------------------------------

	// updateflag -> Aux for the OSD flow control
	//				-1: Keep into inner loop
	//				 0: Exit boot inner and outer loop and go to the launch method choosen
	//				 1: exit inner loop
	//int updateflag = -1;
	int updateflag = 1;

	// Return value
	int retval=0;

	//Auxs for gsKit_fontm_printf_scaled macro
	char tempstr[128];
	
	int rownumber = 0;
	
	int halfWidth = 0;
	
	//---------- End of variables stuff ----------

	
	//----------------------------------------------------------------------------
	//---------- Start of coding stuff ----------

	DeInitGSM();

	// Init SIF RPC
	SifInitRpc(0);
	// Reset IO Processor
	// NB: NULL is specified as an argument to SifIopReset() because it's best for all console models (SCPH-10000 and SCPH-15000 lack rom0:EELOADCNF)
	while(!SifIopReset(NULL, 0)){};
	while(!SifIopSync()){};
	// Init SIF RPC and LOADFILE service: Needed for loading IOP modules properly
	SifInitRpc(0);
	SifLoadFileInit();
	// Apply Sbv patches
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();
	// Load modules
	LoadModules();

	TimerInit();
	Timer();
	setupPad();
	TimerInit();
	Timer();

	//At principle we assume there is no vmode chosen
	mode = 0;
	// Let's Setup GS by the first time
	Setup_GS();
	// FontM Init - Make it once and here!!! Avoid avoid EE Exceptions (for instance, memory leak & overflow)
	gsFontM = gsKit_init_fontm();
	gsKit_fontm_upload(gsGlobal, gsFontM);
	gsFontM->Spacing = 0.95f;
	gsKit_clear(gsGlobal, Black);
	text_height = (26.0f * gsFontM->Spacing * 0.5f);
	edge_size = text_height;

	// The following two lines are useful for making doctorxyz's development and testing easier
	// due to his console setup: PS2 Slim SCPH-90006HK (BIOS rev2.30) - Thunder Pro II Gold modchip
	// predef_vmode_toggle = 11;
	// exit_option_toggle = 6;

	// Main loop
outer_loop_restart:
	while (!(updateflag == 0)) {//---------- Start of outer while loop ----------

		if(updateflag == 1){

			interlace = predef_vmode[predef_vmode_toggle].interlace; 
			mode = predef_vmode[predef_vmode_toggle].mode;
			ffmd = predef_vmode[predef_vmode_toggle].ffmd;
			display  = predef_vmode[predef_vmode_toggle].display;
			syncv = predef_vmode[predef_vmode_toggle].syncv;
			smode2 = (ffmd<<1)|interlace;

			UpdateGSMParams(interlace, mode, ffmd, display, syncv, smode2, dx_offset, dy_offset);

		}//ends if
			
		gsKit_clear(gsGlobal, Black);
				
		// OSD
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, 10, 1, 0.6f, YellowFont, TITLE);
		rownumber = 4;
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber++)*9, 1, 0.4f, DarkOrangeFont, "%s - by %s", VERSION, AUTHORS);
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_CIRCLE" SDTV (PAL/NTSC)");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_SQUARE" HDTV (480p/576p/720p/1080i)");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_TRIANGLE" VGA (640p/640i)");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_CROSS" PS1 SDTV (NTSC/PAL)");
		rownumber++;
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[DPAD] X and Y axis offsets");
		rownumber++;
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[SELECT] Exit Method");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[START] Exit");
			rownumber++;
			rownumber++;
		if(predef_vmode_toggle != 999) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "GS Mode Selected: %s", predef_vmode[predef_vmode_toggle].description);
			rownumber++;
		}
		if(exit_option_toggle != 999) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "Exit Method Selected: %s", exit_option[exit_option_toggle].description);
			rownumber++;
		}
		if(dx_offset != 0) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "X-axis offset: %+d", dx_offset);
			rownumber++;
		}
		if(dy_offset != 0) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "Y-axis offset: %+d", dy_offset);
			rownumber++;
		}
		Draw_Screen();

		updateflag = -1;

					
		// Pad stuff
		//---------- Start of inner while loop ----------
		while (updateflag == -1) {

			delay(1);
			while(!(waitAnyPadReady(), readpad(), new_pad)); //await a new button
			retval = paddata;
			
			if(retval == PAD_TRIANGLE)	{ //VGA
				if (predef_vmode[predef_vmode_toggle].category != VGA_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != VGA_VMODE);

				updateflag = 1; //exit inner loop
			}	
			else if(retval == PAD_SQUARE){ //HDTV
				if (predef_vmode[predef_vmode_toggle].category != HDTV_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != HDTV_VMODE);

				updateflag = 1; //exit inner loop

			}	
			else if(retval == PAD_CIRCLE)	{ //NTSC/PAL
				if (predef_vmode[predef_vmode_toggle].category != SDTV_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != SDTV_VMODE);

				updateflag = 1; //exit inner loop
			}	
			else if(retval == PAD_CROSS)	{ //PS1 NTSC/PAL
				if (predef_vmode[predef_vmode_toggle].category != PS1_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != PS1_VMODE);

				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_SELECT))	{ //Select Exit Method
				exit_option_toggle++;
				if(exit_option_toggle > (exit_option_size - 1)) exit_option_toggle = 0;
				updateflag = 1; //exit inner loop
			}
			else if(retval == PAD_START)	{ //Exit GSM
				updateflag = 0; //exit outer loop
			}
			else if((retval == PAD_LEFT))	{ //Decrease DX
				dx_offset -= 4;
				if(dx_offset < -(4096/4)) dx_offset += 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_RIGHT))	{ //Increase DX
				dx_offset += 4;
				if(dx_offset > (4096/4)) dx_offset -= 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_UP))	{ //Increase DY
				dy_offset += 4;
				if(dy_offset > (2048/4)) dy_offset -= 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_DOWN))	{ //Decrease DY
				dy_offset -= 4;
				if(dy_offset < -(2048/4)) dy_offset += 4;
				updateflag = 1; //exit inner loop
			}
			
		}	//---------- End of inner while loop ----------
	}	//---------- End of outer while loop ----------

	updateflag = -1;
	halfWidth = gsGlobal->Width / 2;
	if((predef_vmode_toggle == 999)||(exit_option_toggle == 999)){	//Nothing chosen yet
		gsKit_clear(gsGlobal, Black);
		gsFontM->Align = GSKIT_FALIGN_CENTER;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, RedFont, "Choose what you want!");
		gsFontM->Align = GSKIT_FALIGN_LEFT;
		Draw_Screen();
		delay(6);
		goto outer_loop_restart;
	}

	//
	//Exit procedures
	//

	//OSD	
	gsKit_clear(gsGlobal, Black);
	gsFontM->Align = GSKIT_FALIGN_CENTER;
	
	if(exit_option[exit_option_toggle].id == 0) {
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Exiting to PS2 BROWSER...");
	}
	else {
		sprintf(tempstr, "%s", exit_option[exit_option_toggle].elf_path);
		strcpy(elf_path, tempstr);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Loading %s ...", elf_path);
	}
	Draw_Screen();
	delay(4);

	/*-------------------------------------------------------*/
	/* Install and Enable Graphics Synthesizer Mode Selector */
	/*-------------------------------------------------------*/

	/*----------------------------*/
	/* Replace SetGsCrt in kernel */
	/*----------------------------*/
	if(GetSyscallHandler(__NR_SetGsCrt) != &Hook_SetGsCrt) {
		Old_SetGsCrt = GetSyscallHandler(__NR_SetGsCrt);
		SetSyscall(__NR_SetGsCrt, &Hook_SetGsCrt);
	}

	/*----------------------------------------------------------------------------------------------------*/
	/* Replace Core Debug Exception Handler (V_DEBUG handler) in kernel                                   */
	/* Exception Vector Address for Debug Level 2 Exception when Stadus.DEV bit is 0 (normal): 0x80000100 */
	/* 'Level 2' is a generalization of Error Level (from previous MIPS processors)                       */
	/* When this exception is recognized, control is transferred to the applicable service routine;       */
	/* in our case the service routine is 'GSHandler'!                                                    */
	/*----------------------------------------------------------------------------------------------------*/
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"

	"sync.l\n"
	"sync.p\n"
	
	".set at\n"
	".set reorder\n"
	);
	DI();
	ee_kmode_enter();
	*(u32 *)0x80000100 = MAKE_J((int)GSHandler);
	*(u32 *)0x80000104 = 0;
	ee_kmode_exit();
	EI();

	// Cleanup gsKit and others stuffs
	gsKit_vram_clear(gsGlobal);
	gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
	CleanUp();

	// Call sceSetGsCrt syscall in order to "bite" the new video mode
	gs_setmode(predef_vmode[predef_vmode_toggle].mode, predef_vmode[predef_vmode_toggle].mode, predef_vmode[predef_vmode_toggle].ffmd);

	// Exit from GSM to the selected method
	if(exit_option[exit_option_toggle].id == 0) {
		__asm__ __volatile__( // Run PS2 Browser
		".set noreorder\n"
		"li $3, 0x04\n"
		"syscall\n"
		"nop\n"
		".set reorder\n"
		);
	}
	else {
		//RunLoaderElf("mc0:boot/boot1.elf\0",party);
		RunLoaderElf(elf_path, party);	// Run ELF
	}

	SleepThread(); // Should never get here
	return 0;
}
