#define TITLE			"Graphics Synthesizer Mode Selector"
#define VERSION			"0.23x2"
#define AUTHORS			"doctorxyz and dlanor"
#define CNF_VERSION	"v0.23s2" //GSM version which defined current CNF format
/*
# GS Mode Selector - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#------------------------------------------------------------------------------
# Copyright 2009, 2010 doctorxyz & dlanor
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
#include <hw.h>
#include <libpad.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#include "modules.h"
#include "timer.h"
#include "pad.h"

#include <gsKit.h>

#include <libjpg.h>

#include "screenshot.h"

int	patcher_enabled = 0;

// SetGsCrt params
volatile u32 interlace, mode, field;
volatile u32 act_interlace, act_mode, act_field;
volatile u32 act_height;

// GS Registers
#define GS_BGCOLOUR *((volatile unsigned long int*)0x120000E0)
volatile u64 display, syncv, act_syncv, smode2;

// DisplayX GS Registers (presets, requested and calculated)
volatile u64 display_presets, display_requested, display_calculated;

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
	u64 smode2;
} GSM_vmode;

u32	user_vmode_slots = 16;	//Must be a power of two (uses bitmask = value-1)

GSM_vmode user_vmode[16] = {
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0},
	{0, 0, 0, 0, 0, 0}
};

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
	u8	field;
	u64	display;
	u64	syncv;
} GSM_predef_vmode;

struct gsm_settings *GSM = NULL;

char ROMVER_data[20]; 	//16 byte file read from rom0:ROMVER at init

// Prototypes for External Functions
void RunLoaderElf(char *filename, char *);

//Splash Screen
extern u32 size_splash;
extern void splash;

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

#define MAX_PATH 260

/*
// To make the reset and flush the GS in GSM, possible!!!
#define GS_REG_CSR		(volatile u64 *)0x12001000
#define GS_SET_CSR(A,B,C,D,E,F,G,H,I,J,K,L) \
    (u64)(A & 0x00000001) <<  0 | (u64)(B & 0x00000001) <<  1 | \
    (u64)(C & 0x00000001) <<  2 | (u64)(D & 0x00000001) <<  3 | \
    (u64)(E & 0x00000001) <<  4 | (u64)(F & 0x00000001) <<  8 | \
    (u64)(G & 0x00000001) <<  9 | (u64)(H & 0x00000001) << 12 | \
    (u64)(I & 0x00000001) << 13 | (u64)(J & 0x00000003) << 14 | \
    (u64)(K & 0x000000FF) << 16 | (u64)(L & 0x000000FF) << 24
*/

#include "TSR.c"

#include "MISC.c"

//----------------------------------------------------------------------------
void setNormalAdaption(void) //Should be called before exiting main program
{
	set_kmode();
	Adapt_Flags &= 0xFFFFFF00; //Clear 1 byte flag of 4
  set_umode();

}

//----------------------------------------------------------------------------
/* Update parameters to be forced by ModdedSetGsCrt and DisplayHandler TSR functions */
void UpdateModdedSetGsCrtDisplayHandlerParams(u32 interlace, u32 mode, u32 field, u64 display, u64 syncv, u64 smode2)
{
	set_kmode();
	Target_Interlace = interlace;
	Target_Mode = mode;
	Target_Field = field;
	Target_Display = display;
	Target_SyncV = syncv;
	Target_SMode2 = smode2;
	Adapt_SMode2 = (u8) smode2;
  	set_umode();
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
	setNormalAdaption();
	TimerEnd();
	padPortClose(1,0);
	padPortClose(0,0);
	padEnd();
}

//-------------------------------------------------------------------------
// GetROMVersion - reads version of PS2 ROM
// Taken from the ps2menu.c file, from the PS2Menu project
//-------------------------------------------------------------------------
int GetROMVersion(void)
{
	int fd;

	fd = open("rom0:ROMVER", O_RDONLY);
	if(fd < 0)
		return 0; //Return 0 for unidentified version
	read(fd, ROMVER_data, 14);
	ROMVER_data[14]=0;
	close(fd);
	return 1; //Return 1 for identified version
}
//endfunc GetROMVersion
//----------------------------------------------------------------------------

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
/*
	// Reset and flush the GS
	*GS_REG_CSR = GS_SET_CSR(0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0);
*/
	// GS Init

	//The following line eliminates overflow
	gsGlobal = gsKit_init_global_custom(GS_RENDER_QUEUE_OS_POOLSIZE+GS_RENDER_QUEUE_OS_POOLSIZE/2, GS_RENDER_QUEUE_PER_POOLSIZE);

	// Buffer Init
	gsGlobal->PrimAAEnable = GS_SETTING_ON;
	gsGlobal->DoubleBuffering = GS_SETTING_OFF;
	gsGlobal->ZBuffering      = GS_SETTING_OFF;


/*	// DMAC Init
	dmaKit_init(D_CTRL_RELE_OFF,D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);
*/

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
		gsGlobal->Field = field;
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

	printf("gsGlobal:\nInterlace %01d, Mode 0x%02X, Field %01d\nWidth %04d,Height %04d\nDX %04d, DY %04d\nMAGH %02d, MAGV %01d\nDW %04d, DH=%04d\n", \
	gsGlobal->Interlace, gsGlobal->Mode, gsGlobal->Field, gsGlobal->Width, gsGlobal->Height, gsGlobal->StartX, gsGlobal->StartY, gsGlobal->MagH, gsGlobal->MagV, gsGlobal->DW, gsGlobal->DH);
	
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
//Use XBRA protocol to unlink an old instance of GSM, if any such is active
void unlink_GSM()
{
	u32 SyscallVectorTableAddress;
	u32 SetGsCrtVectorAddress;
	u32 SetGsCrt_Addr;
	XBRA_header *setGsCrt_XBRA_p;

	SyscallVectorTableAddress = GetSyscallVectorTableAddress();
	SetGsCrtVectorAddress = SyscallVectorTableAddress + 2 * 4;
	
	set_kmode();
	syscallTable = (void *) SyscallVectorTableAddress;
	SetGsCrt_Addr = *(volatile u32 *)SetGsCrtVectorAddress;
	setGsCrt_XBRA_p = (XBRA_header *)(SetGsCrt_Addr - XB_code);

	if((setGsCrt_XBRA_p -> xb_magic == XBRA_MAGIC)
	&& (setGsCrt_XBRA_p -> xb_id == GSM__MAGIC)
	){
		//Here we need to abort an old install, as GSM is already active.
		//So we unlink that old install before linking in the new one
		//It has to be done this way because the new instance may execute
		//in a different KSEG location, for game compatibility reasons
		//Note that similar unlinking is not needed for DisplayHandler,
		//since this debug trap routine is not chained to any previous instance,
		//as each new instance completely replaces any old one.
		//But care must still be taken so as not to crash the console by making
		//vmode changes through setGsCrt in a half initialized state
		SetGsCrt_Addr = setGsCrt_XBRA_p -> xb_next;
		*(volatile u32 *)SetGsCrtVectorAddress = SetGsCrt_Addr;	//unlink setGsCrt of old GSM
	}
}

//----------------------------------------------------------------------------
// Install GS (Graphics Sinthesizer) Mode Selector
void installGSModeSelector()
{
	u32 SyscallVectorTableAddress;
	u32 SetGsCrtVectorAddress;
	u32 SetGsCrt_Addr;
	XBRA_header *setGsCrt_XBRA_p;

	SyscallVectorTableAddress = GetSyscallVectorTableAddress();
	SetGsCrtVectorAddress = SyscallVectorTableAddress + 2 * 4;
	
	printf("Syscall Vector Table found at 0x%08x\n", SyscallVectorTableAddress);
	   
	// Install sceGsSetCrt patcher
	// Syscall hooked: Syscall #2 (SetGsCrt)
	set_kmode();
	Adapt_Flags = 0x00000001;						//Init 4 byte flags
	set_umode();

	set_kmode();
	syscallTable = (void *) SyscallVectorTableAddress;
	SetGsCrt_Addr = *(volatile u32 *)SetGsCrtVectorAddress;
	setGsCrt_XBRA_p = (XBRA_header *)(SetGsCrt_Addr - XB_code);

	//Note that this routine is no longer responsible for unlinking old instances
	//It is assumed that this is done previously by a call to unlink_GSM
	//performed before the new routines were copied to KSEG RAM (already done)

	//Here we are ready to link in the new setGsCrt routine of a new GSM instance
	setGsCrt_XBRA_p = (XBRA_header *)(KSEG_ModdedSetGsCrt_entry_p - XB_code);
	sceSetGsCrt = (void *) SetGsCrt_Addr;
	setGsCrt_XBRA_p -> xb_next = (u32) sceSetGsCrt;
	*(volatile u32 *)SetGsCrtVectorAddress = (int)KSEG_ModdedSetGsCrt_entry_p;
	set_umode();

	printf("SyscallVectorTableAddress[2]\n");
	printf(" [BEFORE]SetGsCrt       = 0x%08x\n", SetGsCrt_Addr);
	printf(" [AFTER ]ModdedSetGsCrt = 0x%08x\n\n", (int)KSEG_ModdedSetGsCrt_entry_p);

	printf("[BEFORE] Core Debug Exception Handler (V_DEBUG handler)\n");

	// Install Display Handler in place of the Core Debug Exception Handler (V_DEBUG handler)
	// Exception Vector Address for Debug Level 2 Exception when Stadus.DEV bit is 0 (normal): 0x80000100
	// 'Level 2' is a generalization of Error Level (from previous MIPS processors)
	// When this exception is recognized, control is transferred to the applicable service routine;
	// in our case the service routine is 'DisplayHandler'!
	
	set_kmode();
	*(volatile u32 *)0x80000100 = MAKE_J((int)KSEG_DisplayHandler_entry_p);
	*(volatile u32 *)0x80000104 = 0;
	set_umode();

	printf("[AFTER ] DisplayHandler -> J 0x%08x\n", (int)KSEG_DisplayHandler_entry_p);
    
	// Set Data Address Write Breakpoint
	// Trap writes to GS registers, so as to control their values
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"
	
	"li $a0, 0x12000000\n"	// Address base for trapping
	"li $a1, 0x1FFFFF1F\n"	// Address mask for trapping
	//We trap writes to 0x12000000 + 0x00,0x20,0x40,0x60,0x80,0xA0,0xC0,0xE0
	//We only want 0x20, 0x60, 0x80, 0xA0, but can't mask for that combination
	//But the trapping range is now extended to match all kernel access segments

	"di\n"									// Disable Interupts
	"sync\n"								// Wait until the preceding loads are completed

	"ori $k0, $zero, 0x8000\n"
	"mtc0 $k0, $24\n"			// All breakpoints off (BED = 1)
	"sync.p\n"						// Await instruction completion
	"mtdab	$a0\n"
	"mtdabm	$a1\n"
	"sync.p\n"						// Await instruction completion
	"mfc0 $k1, $24\n"
	"sync.p\n"						// Await instruction completion

	"lui $k0, 0x2020\n"			// Data write breakpoint on (DWE, DUE = 1)
	"or $k1, $k1, $k0\n"
	"xori $k1, $k1, 0x8000\n"		// DEBUG exception trigger on (BED = 0)
	"mtc0 $k1, $24\n"
	"sync.p\n"						//  Await instruction completion

	"ei\n"						// Enable Interupts
	"nop\n"
	"nop\n"
	
	".set at\n"
	".set reorder\n"
	);

	printf("Now, writes to the GS registers we need to control are trapped!\n\n");

}

#include "main.c"

