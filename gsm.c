/*
#
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Copyright 2011, 2012 doctorxyz, SP193 & reprep
# Copyright 2013 doctorxyz
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
#
*/

#include <syscallnr.h>
#include <stdio.h>
#include <kernel.h>
#include <malloc.h>
#include <string.h>
#include <debug.h>
#include <sifrpc.h>
#include <fileXio_rpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <iopcontrol.h>
#include <libmc.h>
#include <fileio.h>
#include <libhdd.h>
#include <libpad.h>

#include <gsKit.h>

#include "timer.h"
#include "pad.h"

#define TITLE			"Graphics Synthesizer Mode Selector"
#define VERSION			"0.37"
#define AUTHORS			"doctorxyz, dlanor, SP193 and reprep"

#define gsKit_fontm_printf_scaled(gsGlobal, gsFontM, X, Y, Z, scale, color, format, args...) \
	sprintf(tempstr, format, args); \
	gsKit_fontm_print_scaled(gsGlobal, gsFontM, X, Y, Z, scale, color, tempstr);
	
#define make_display_magic_number(dh, dw, magv, magh, dy, dx) \
        (((u64)(dh)<<44) | ((u64)(dw)<<32) | ((u64)(magv)<<27) | \
         ((u64)(magh)<<23) | ((u64)(dy)<<12)   | ((u64)(dx)<<0)     )

// After doing some investigation, I''ve found these interesting FONTM character number codes ;-)
#define FONTM_CIRCLE			"\f0090"
#define FONTM_FILLED_CIRCLE		"\f0091"
#define FONTM_SQUARE			"\f0095"
#define FONTM_FILLED_SQUARE		"\f0096"
#define FONTM_TRIANGLE			"\f0097"
#define FONTM_FILLED_TRIANGLE	"\f0098"
#define FONTM_CROSS				"\f1187"

#define MAKE_J(func)		(u32)( (0x02 << 26) | (((u32)func) / 4) )	// Jump (MIPS instruction)
#define NOP					0x00000000									// No Operation (MIPS instruction)

// GS Registers
#define GS_BGCOLOUR *((volatile unsigned long int*)0x120000E0)

// VMODE TYPES
#define PS1_VMODE	1
#define SDTV_VMODE	2
#define HDTV_VMODE	3
#define VGA_VMODE	4

/// DTV 576 Progressive Scan (720x576)
#define GS_MODE_DTV_576P  0x53

/// DTV 1080 Progressive Scan (1920x1080)
#define GS_MODE_DTV_1080P  0x54

// Prototypes for External Functions
#define _GSM_ENGINE_ __attribute__((section(".gsm_engine")))		// Resident section

extern void *Old_SetGsCrt _GSM_ENGINE_;

extern u32 Source_INTERLACE _GSM_ENGINE_;
extern u32 Source_MODE _GSM_ENGINE_;
extern u32 Source_FFMD _GSM_ENGINE_;

extern u64 Calculated_DISPLAY1 _GSM_ENGINE_;
extern u64 Calculated_DISPLAY2 _GSM_ENGINE_;

extern u32 Target_INTERLACE _GSM_ENGINE_;
extern u32 Target_MODE _GSM_ENGINE_;
extern u32 Target_FFMD _GSM_ENGINE_;

extern u64 Target_SMODE2 _GSM_ENGINE_;
extern u64 Target_DISPLAY1 _GSM_ENGINE_;
extern u64 Target_DISPLAY2 _GSM_ENGINE_;
extern u64 Target_SYNCV _GSM_ENGINE_;

extern u8 automatic_adaptation _GSM_ENGINE_;
extern u8 DISPLAY_fix _GSM_ENGINE_;
extern u8 SMODE2_fix _GSM_ENGINE_;
extern u8 SYNCV_fix _GSM_ENGINE_;
extern u8 skip_videos_fix _GSM_ENGINE_;

extern u32 X_offset _GSM_ENGINE_;
extern u32 Y_offset _GSM_ENGINE_;

extern void Hook_SetGsCrt() _GSM_ENGINE_;
extern void GSHandler() _GSM_ENGINE_;

extern void RunLoaderElf(char *filename, char *);

typedef struct predef_vmode_struct {
	u8	category;
	char desc[34];
	u8	interlace;
	u8	mode;
	u8	ffmd;
	u64	display;
	u64	syncv;
} predef_vmode_struct;

typedef struct off_on_struct {
	char desc[4];
	u8	value;
} off_on_struct __attribute__((aligned(16)));

typedef struct exit_struct {
	char desc[22];
	char elf_path[0x40];
} exit_struct;

// Pre-defined vmodes 
// Some of following vmodes gives BOSD and/or freezing, depending on the console BIOS version, TV/Monitor set, PS2 cable (composite, component, VGA, ...)
// Therefore there are many variables involved here that can lead us to success or faild depending on the circumstances above mentioned.
//
//	category	description								interlace			mode			 	ffmd	   	display							dh		dw		magv	magh	dy		dx		syncv
//	--------	-----------								---------			----			 	----		----------------------------	--		--		----	----	--		--		-----
static const predef_vmode_struct predef_vmode[30] = {
	{  SDTV_VMODE,"NTSC                           ",	GS_INTERLACED,		GS_MODE_NTSC,		GS_FIELD,	(u64)make_display_magic_number(	 447,	2559,	0,		3,		 46,	700),	0x00C7800601A01801},
	{  SDTV_VMODE,"NTSC Non Interlaced            ",	GS_INTERLACED,		GS_MODE_NTSC,		GS_FRAME,	(u64)make_display_magic_number(	 223,	2559,	0,		3,		 26,	700),	0x00C7800601A01802},
	{  SDTV_VMODE,"PAL                            ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FIELD,	(u64)make_display_magic_number(	 511,	2559,	0,		3,		 70,	720),	0x00A9000502101401},
	{  SDTV_VMODE,"PAL Non Interlaced             ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FRAME,	(u64)make_display_magic_number(	 255,	2559,	0,		3,		 37,	720),	0x00A9000502101404},
	{  SDTV_VMODE,"PAL @60Hz                      ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FIELD,	(u64)make_display_magic_number(	 447,	2559,	0,		3,		 46,	700),	0x00C7800601A01801},
	{  SDTV_VMODE,"PAL @60Hz Non Interlaced       ",	GS_INTERLACED,		GS_MODE_PAL,		GS_FRAME,	(u64)make_display_magic_number(	 223,	2559,	0,		3,		 26,	700),	0x00C7800601A01802},
	{  PS1_VMODE, "PS1 NTSC (HDTV 480p @60Hz)     ",	GS_NONINTERLACED,	GS_MODE_DTV_480P,	GS_FRAME,	(u64)make_display_magic_number(	 255,	2559,	0,		1,		 12,	736),	0x00C78C0001E00006},
	{  PS1_VMODE, "PS1 PAL (HDTV 576p @50Hz)      ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,	GS_FRAME,	(u64)make_display_magic_number(	 255,	2559,	0,		1,		 23,	756),	0x00A9000002700005},
	{  HDTV_VMODE,"HDTV 480p @60Hz                ",	GS_NONINTERLACED,	GS_MODE_DTV_480P,	GS_FRAME, 	(u64)make_display_magic_number(	 479,	1279,	0,		1,		 51,	308),	0x00C78C0001E00006},
	{  HDTV_VMODE,"HDTV 576p @50Hz                ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,	GS_FRAME,	(u64)make_display_magic_number(	 575,	1279,	0,		1,		 64,	320),	0x00A9000002700005},
	{  HDTV_VMODE,"HDTV 720p @60Hz                ",	GS_NONINTERLACED,	GS_MODE_DTV_720P,	GS_FRAME, 	(u64)make_display_magic_number(	 719,	1279,	1,		1,		 24,	302),	0x00AB400001400005},
	{  HDTV_VMODE,"HDTV 1080i @60Hz               ",	GS_INTERLACED,		GS_MODE_DTV_1080I,	GS_FIELD, 	(u64)make_display_magic_number(	1079,	1919,	1,		2,		 48,	238),	0x0150E00201C00005},
	{  HDTV_VMODE,"HDTV 1080i @60Hz Non Interlaced",	GS_INTERLACED,		GS_MODE_DTV_1080I,	GS_FRAME, 	(u64)make_display_magic_number(	1079,	1919,	0,		2,		 48,	238),	0x0150E00201C00005},
	{  HDTV_VMODE,"HDTV 1080p @60Hz               ",	GS_NONINTERLACED,	GS_MODE_DTV_1080P,	GS_FRAME, 	(u64)make_display_magic_number(	1079,	1919,	1,		2,		 48,	238),	0x0150E00201C00005},
	{  VGA_VMODE, "VGA 640x480p @60Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_640_60,	GS_FRAME, 	(u64)make_display_magic_number(	 479,	1279,	0,		1,		 54,	276),	0x004780000210000A},
	{  VGA_VMODE, "VGA 640x960i @60Hz             ",	GS_INTERLACED,		GS_MODE_VGA_640_60,	GS_FIELD,	(u64)make_display_magic_number(	 959,	1279,	1,		1,		128,	291),	0x004F80000210000A},
	{  VGA_VMODE, "VGA 640x480p @72Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_640_72, GS_FRAME,	(u64)make_display_magic_number(  480,	1280,	0,		1,		 18,	330),	0x0067800001C00009},
	{  VGA_VMODE, "VGA 640x480p @75Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_640_75, GS_FRAME, 	(u64)make_display_magic_number(  480,	1280,	0,		1,		 18,	360),	0x0067800001000001},
	{  VGA_VMODE, "VGA 640x480p @85Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_640_85, GS_FRAME,	(u64)make_display_magic_number(  480,	1280,	0,		1,		 18,	260),	0x0067800001000001},
	{  VGA_VMODE, "VGA 800x600p @56Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_800_56, GS_FRAME,	(u64)make_display_magic_number(  600,	1600,	0,		1,		 25,	450),	0x0049600001600001},
	{  VGA_VMODE, "VGA 800x600p @60Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_800_60, GS_FRAME, 	(u64)make_display_magic_number(  600,	1600,	0,		1,		 25,	465),	0x0089600001700001},
	{  VGA_VMODE, "VGA 800x600p @72Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_800_72, GS_FRAME,	(u64)make_display_magic_number(  600,	1600,	0,		1,		 25,	465),	0x00C9600001700025},
	{  VGA_VMODE, "VGA 800x600p @75Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_800_75, GS_FRAME, 	(u64)make_display_magic_number(  600,	1600,	0,		1,		 25,	510),	0x0069600001500001},
	{  VGA_VMODE, "VGA 800x600p @85Hz             ",	GS_NONINTERLACED,	GS_MODE_VGA_800_85, GS_FRAME,	(u64)make_display_magic_number(  600,	1600,	0,		1,		 15,	500),	0x0069600001B00001},
	{  VGA_VMODE, "VGA 1024x768p @60Hz            ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_60, GS_FRAME, 	(u64)make_display_magic_number(  768,	2048,	0,		2,		 30,	580),	0x00CC000001D00003},
	{  VGA_VMODE, "VGA 1024x768p @70Hz            ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_70, GS_FRAME,	(u64)make_display_magic_number(  768,	1024,	0,		0,		 30,	266),	0x00CC000001D00003},
	{  VGA_VMODE, "VGA 1024x768p @75Hz            ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_75, GS_FRAME, 	(u64)make_display_magic_number(  768,	1024,	0,		0,		 30,	260),	0x006C000001C00001},
	{  VGA_VMODE, "VGA 1024x768p @85Hz            ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_85, GS_FRAME,	(u64)make_display_magic_number(  768,	1024,	0,		0,		 30,	290),	0x006C000002400001},
	{  VGA_VMODE, "VGA 1280x1024p @60Hz           ",	GS_NONINTERLACED,	GS_MODE_VGA_1280_60, GS_FRAME, 	(u64)make_display_magic_number(  1024,	1280,	1,		1,		 40,	350),	0x0070000002600001},
	{  VGA_VMODE, "VGA 1280x1024p @75Hz           ",	GS_NONINTERLACED,	GS_MODE_VGA_1280_75, GS_FRAME, 	(u64)make_display_magic_number(  1024,	1280,	1,		1,		 40,	350),	0x0070000002600001}
}; //ends predef_vmode definition

u32 predef_vmode_size = 	sizeof( predef_vmode ) / sizeof( predef_vmode[0] );

// Skip Videos fix
//
//	description	flag
//	----------- ----
volatile static off_on_struct off_on[2] = {
	{ "OFF", 	0},
	{ "ON ", 	1}
};//ends skip_videos definition

u32 off_on_size = 	sizeof( off_on ) / sizeof( off_on[0] );

// Exit Method
//-
//	description			elf_path
//	-----------			---------
volatile static exit_struct exit_option[5] = {
	{ "PS2 BROWSER          ",	""},
	{ "mc0:BOOT/BOOT.ELF    ",	"mc0:BOOT/BOOT.ELF\0"},
	{ "mc0:APPS/BOOT.ELF    ",	"mc0:APPS/BOOT.ELF\0"},
	{ "mc0:BOOT/HDLOADER.ELF",	"mc0:BOOT/HDLOADER.ELF\0"},
	{ "mc0:boot/boot.elf    ",	"mc0:boot/boot.elf\0"},
};//ends exit_option definition

u32 exit_option_size = 	sizeof( exit_option ) / sizeof( exit_option[0] );

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

// Font used for printing text.
GSFONTM *gsFontM;

//  Splash Screen Texture Skin
GSTEXTURE TexSkin;

int updateScr_1;     //flags screen updates for drawScr()
int updateScr_2;     //used for anti-flicker delay in drawScr()
u64 updateScr_t = 0; //exit time of last drawScr()

u64 WaitTime = 0; //used for time waiting

struct gsm_settings *GSM = NULL;

#define DI2	DIntr
#define EI2	EIntr

_GSM_ENGINE_ int ee_kmode_enter2() {
	u32 status, mask;

	__asm__ volatile (
		".set\tpush\n\t"		\
		".set\tnoreorder\n\t"		\
		"mfc0\t%0, $12\n\t"		\
		"li\t%1, 0xffffffe7\n\t"	\
		"and\t%0, %1\n\t"		\
		"mtc0\t%0, $12\n\t"		\
		"sync.p\n\t"
		".set\tpop\n\t" : "=r" (status), "=r" (mask));

	return status;
}

_GSM_ENGINE_ int ee_kmode_exit2() {
	int status;

	__asm__ volatile (
		".set\tpush\n\t"		\
		".set\tnoreorder\n\t"		\
		"mfc0\t%0, $12\n\t"		\
		"ori\t%0, 0x10\n\t"		\
		"mtc0\t%0, $12\n\t"		\
		"sync.p\n\t" \
		".set\tpop\n\t" : "=r" (status));

	return status;
}

_GSM_ENGINE_ void SetSyscall2(int number, void (*functionPtr)(void)) {
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"
	"li $3, 0x74\n"
    "add $4, $0, %0    \n"   // Specify the argument #1
    "add $5, $0, %1    \n"   // Specify the argument #2
   	"syscall\n"
	"jr $31\n"
	"nop\n"
	".set at\n"
	".set reorder\n"
    :
    : "r"( number ), "r"( functionPtr )
    );
}

_GSM_ENGINE_ u32* GetROMSyscallVectorTableAddress(void) {
	//Search for Syscall Table in ROM
	u32 i;
	u32 startaddr;
	u32* ptr;
	u32* addr;
	startaddr = 0;
	for (i = 0x1FF00000; i < 0x1FFFFFFF; i+= 4)
	{
		if ( *(u32*)(i + 0) == 0x40196800 )
		{
			if ( *(u32*)(i + 4) == 0x3C1A8001 )
			{
				startaddr = i - 8;
				break;
			}
		}
	}
	ptr = (u32 *) (startaddr + 0x02F0);
	addr = (u32*)((ptr[0] << 16) | (ptr[2] & 0xFFFF));
	addr = (u32*)((u32)addr & 0x1fffffff);
	addr = (u32*)((u32)addr + startaddr);
	return addr;
}

_GSM_ENGINE_ void InitGSM(u32 interlace, u32 mode, u32 ffmd, u64 display, u64 syncv, u64 smode2, int dx_offset, int dy_offset, u8 skip_videos)
 {

	u32* ROMSyscallTableAddress;

	// Update GSM params
	DI2();
	ee_kmode_enter2();

	Target_INTERLACE		= interlace;
	Target_MODE				= mode;
	Target_FFMD				= ffmd;
	Target_DISPLAY1			= display;
	Target_DISPLAY2			= display;
	Target_SYNCV			= syncv;
	Target_SMODE2			= smode2;
	X_offset				= dx_offset;		// X-axis offset -> Use it only when automatic adaptations formulas don't fit into your needs
	Y_offset				= dy_offset;		// Y-axis offset -> Use it only when automatic adaptations formulas dont't fit into your needs
	skip_videos_fix			= skip_videos ^ 1;	// Skip Videos Fix ------------> 0 = On, 1 = Off ; Default = 0 = On
	
	automatic_adaptation	= 0;				// Automatic Adaptation -> 0 = On, 1 = Off ; Default = 0 = On
	DISPLAY_fix				= 0;				// DISPLAYx Fix ---------> 0 = On, 1 = Off ; Default = 0 = On
	SMODE2_fix				= 0;				// SMODE2 Fix -----------> 0 = On, 1 = Off ; Default = 0 = On
	SYNCV_fix				= 0;				// SYNCV Fix ------------> 0 = On, 1 = Off ; Default = 0 = On

	ee_kmode_exit2();
	EI2();

	// Hook SetGsCrt
	ROMSyscallTableAddress = GetROMSyscallVectorTableAddress();
	Old_SetGsCrt = (void*)ROMSyscallTableAddress[2];
	SetSyscall2(2, &Hook_SetGsCrt);

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

	// Replace Core Debug Exception Handler (V_DEBUG handler) by GSHandler
	DI2();
	ee_kmode_enter2();
	*(u32 *)0x80000100 = MAKE_J((int)GSHandler);
	*(u32 *)0x80000104 = 0;
	ee_kmode_exit2();
	EI2();
}

/*-------------------*/
/* Update GSM params */
/*-------------------*/
// Update parameters to be enforced by Hook_SetGsCrt syscall hook and GSHandler service routine functions
static inline void UpdateGSMParams(u32 interlace, u32 mode, u32 ffmd, u64 display, u64 syncv, u64 smode2, int dx_offset, int dy_offset, u8 skip_videos)
{
	DI();
	ee_kmode_enter();

	Target_INTERLACE		= (u32) interlace;
	Target_MODE				= (u32) mode;
	Target_FFMD				= (u32) ffmd;

	Target_SMODE2			= (u8) smode2;
	Target_DISPLAY1			= (u64) display;
	Target_DISPLAY2			= (u64) display;
	Target_SYNCV			= (u64) syncv;

	automatic_adaptation	= 0;				// Automatic Adaptation -> 0 = On, 1 = Off ; Default = 0 = On
	DISPLAY_fix				= 0;				// DISPLAYx Fix ---------> 0 = On, 1 = Off ; Default = 0 = On
	SMODE2_fix				= 0;				// SMODE2 Fix -----------> 0 = On, 1 = Off ; Default = 0 = On
	SYNCV_fix				= 0;				// SYNCV Fix ------------> 0 = On, 1 = Off ; Default = 0 = On

	X_offset				= dx_offset;		// X-axis offset -> Use it only when automatic adaptations formulas don't fit into your needs
	Y_offset				= dy_offset;		// Y-axis offset -> Use it only when automatic adaptations formulas dont't fit into your needs

	skip_videos_fix			= skip_videos ^ 1;	// Skip Videos Fix ------------> 0 = On, 1 = Off ; Default = 0 = On

	ee_kmode_exit();
	EI();
}

/*---------------------------------------------------------*/
/* Disable Graphics Synthesizer Mode Selector (a.k.a. GSM) */
/*---------------------------------------------------------*/
static inline void DeInitGSM(void)
{
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

void Timer_delay(int timeout){ //Will delay at least timeout ms (at most 1 more)
	u64 start_time;

	start_time = Timer();
	while(Timer() < (start_time + (u64) (timeout + 1)));
}

void CleanUp(void)
{
	TimerEnd();
	padPortClose(1,0);
	padPortClose(0,0);
	padEnd();
}

void Clear_Screen(void)
{
	gsKit_clear(gsGlobal, Black);
	gsKit_prim_sprite_texture(gsGlobal,
	 &TexSkin, 0, 0, 0, 0,
	 gsGlobal->Width, gsGlobal->Height, TexSkin.Width, TexSkin.Height,
	 0, Trans);
}

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
 
	// 999: None value chosen yet
	// Other values: Value chosen by user
	int predef_vmode_idx = 999;
	int XOffset = 0;
	int YOffset = 0;
	int skip_videos_idx = 0;
	int exit_option_idx = 999;

	//----------------------------------------------------------------------------

	// -1: Keep into inner loop
	// 0: Exit both inner and outer loop and go to the launch method choosen
	// 1: exit inner loop
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

	//----------------------------------------------------------------------------
	// Let's Setup GS by the first time
	Setup_GS();
	// FontM Init - Make it once and here!!! Avoid avoid EE Exceptions (for instance, memory leak & overflow)
	gsFontM = gsKit_init_fontm();
	gsKit_fontm_upload(gsGlobal, gsFontM);
	gsFontM->Spacing = 0.95f;
	gsKit_clear(gsGlobal, Black);
	text_height = (26.0f * gsFontM->Spacing * 0.5f);
	edge_size = text_height;

	// Main loop
outer_loop_restart:
	while (!(updateflag == 0)) {//---------- Start of outer while loop ----------

		gsKit_clear(gsGlobal, Black);
				
		// OSD
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, 10, 1, 0.6f, YellowFont, TITLE);
		rownumber = 4;
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber++)*9, 1, 0.4f, DarkOrangeFont, "%s - by %s", VERSION, AUTHORS);
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_CIRCLE" SDTV vmodes");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_SQUARE" HDTV vmodes");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_TRIANGLE" VGA vmodes");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, FONTM_CROSS" PS1 SDTV vmodes");
		rownumber++;
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[DPAD] X and Y axis offsets");
		rownumber++;
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[L1] Skip Videos fix");
		rownumber++;
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[SELECT] Exit Method");
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[START] Exit");
		rownumber++;
		rownumber++;
		if(predef_vmode_idx != 999) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "GS Mode Selected: %s", predef_vmode[predef_vmode_idx].desc);
			rownumber++;
		}
		if(exit_option_idx != 999) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "Exit Method Selected: %s", exit_option[exit_option_idx].desc);
			rownumber++;
		}
		if(XOffset != 0) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "X-axis offset: %+d", XOffset);
			rownumber++;
		}
		if(YOffset != 0) {
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "Y-axis offset: %+d", YOffset);
			rownumber++;
		}
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "Skip Videos fix: %s", off_on[skip_videos_idx].desc);

		Draw_Screen();

		updateflag = -1;

					
		// Pad stuff
		//---------- Start of inner while loop ----------
		while (updateflag == -1) {

			while(!(waitAnyPadReady(), readpad(), new_pad)); //await a new button
			retval = paddata;
			
			if(retval == PAD_TRIANGLE)	{ //VGA
				if (predef_vmode[predef_vmode_idx].category != VGA_VMODE) predef_vmode_idx = -1;
				do
				{
					predef_vmode_idx++;
					if(predef_vmode_idx > (predef_vmode_size - 1)) predef_vmode_idx = 0;
				}while (predef_vmode[predef_vmode_idx].category != VGA_VMODE);

				updateflag = 1; //exit inner loop
			}	
			else if(retval == PAD_SQUARE){ //HDTV
				if (predef_vmode[predef_vmode_idx].category != HDTV_VMODE) predef_vmode_idx = -1;
				do
				{
					predef_vmode_idx++;
					if(predef_vmode_idx > (predef_vmode_size - 1)) predef_vmode_idx = 0;
				}while (predef_vmode[predef_vmode_idx].category != HDTV_VMODE);

				updateflag = 1; //exit inner loop

			}	
			else if(retval == PAD_CIRCLE)	{ //NTSC/PAL
				if (predef_vmode[predef_vmode_idx].category != SDTV_VMODE) predef_vmode_idx = -1;
				do
				{
					predef_vmode_idx++;
					if(predef_vmode_idx > (predef_vmode_size - 1)) predef_vmode_idx = 0;
				}while (predef_vmode[predef_vmode_idx].category != SDTV_VMODE);

				updateflag = 1; //exit inner loop
			}	
			else if(retval == PAD_CROSS)	{ //PS1 NTSC/PAL
				if (predef_vmode[predef_vmode_idx].category != PS1_VMODE) predef_vmode_idx = -1;
				do
				{
					predef_vmode_idx++;
					if(predef_vmode_idx > (predef_vmode_size - 1)) predef_vmode_idx = 0;
				}while (predef_vmode[predef_vmode_idx].category != PS1_VMODE);

				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_SELECT))	{ //Select Exit Method
				exit_option_idx++;
				if(exit_option_idx > (exit_option_size - 1)) exit_option_idx = 0;
				updateflag = 1; //exit inner loop
			}
			else if(retval == PAD_START)	{ //Exit GSM
				updateflag = 0; //exit outer loop
			}
			else if((retval == PAD_LEFT))	{ //Decrease DX
				XOffset -= 4;
				if(XOffset < -(4096/4)) XOffset += 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_RIGHT))	{ //Increase DX
				XOffset += 4;
				if(XOffset > (4096/4)) XOffset -= 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_UP))	{ //Increase DY
				YOffset += 4;
				if(YOffset > (2048/4)) YOffset -= 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_DOWN))	{ //Decrease DY
				YOffset -= 4;
				if(YOffset < -(2048/4)) YOffset += 4;
				updateflag = 1; //exit inner loop
			}
			else if((retval == PAD_L1))	{ //Skip Videos toggle
				skip_videos_idx ^= 1;
				updateflag = 1; //exit inner loop
			}

		delay(1);

		}	//---------- End of inner while loop ----------
	}	//---------- End of outer while loop ----------

	updateflag = -1;
	halfWidth = gsGlobal->Width / 2;
	if((predef_vmode_idx == 999)||(exit_option_idx == 999)){	//Nothing chosen yet
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
	
	if(exit_option_idx == 0) {
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Exiting to PS2 BROWSER...");
	}
	else {
		sprintf(tempstr, "%s", exit_option[exit_option_idx].elf_path);
		strcpy(elf_path, tempstr);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Loading %s ...", elf_path);
	}
	Draw_Screen();
	delay(4);

	// Cleanup gsKit and others stuffs
	gsKit_vram_clear(gsGlobal);
	gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
	CleanUp();

	InitGSM(predef_vmode[predef_vmode_idx].interlace, \
					predef_vmode[predef_vmode_idx].mode, \
					predef_vmode[predef_vmode_idx].ffmd, \
					predef_vmode[predef_vmode_idx].display, \
					predef_vmode[predef_vmode_idx].syncv, \
					((predef_vmode[predef_vmode_idx].ffmd)<<1)|(predef_vmode[predef_vmode_idx].interlace), \
					XOffset, \
					YOffset, \
					off_on[skip_videos_idx].value);

	// Call sceSetGsCrt syscall in order to "bite" the new video mode
	__asm__ __volatile__(
		"li  $3, 0x02\n"   // Syscall Number = 2 (sceGsCrt)
		"add $4, $0, %0\n"   // interlace
		"add $5, $0, %1\n"   // mode
		"add $6, $0, %2\n"   // ffmd

		"syscall\n"			// Perform the syscall
		"nop\n"				// nop for Branch delay slot

		:
		:	"r" (predef_vmode[predef_vmode_idx].interlace), \
			"r" (predef_vmode[predef_vmode_idx].mode), \
			"r" (predef_vmode[predef_vmode_idx].ffmd)
	);

	// Exit from GSM to the selected method
	if(exit_option_idx == 0) {
		__asm__ __volatile__( // Run PS2 Browser
		".set noreorder\n"
		"li $3, 0x04\n"
		"syscall\n"
		"nop\n"
		".set reorder\n"
		);
	}
	else {
		//RunLoaderElf("mc0:BOOT/BOOT.ELF\0",party);
		RunLoaderElf(elf_path, party);	// Run ELF
	}

	SleepThread(); // Should never get here
	return 0;
}
