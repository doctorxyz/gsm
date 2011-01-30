#define TITLE			"Graphics Synthesizer Mode Selector"
#define VERSION			"GSModeSelector_v0.23x1"
#define AUTHORS			"doctorxyz and dlanor"
#define CNF_VERSION	"v0.23s2" //GSM version which defined current CNF format
/*
# GS Mode Selector - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#------------------------------------------------------------------------------
# Copyright 2009, doctorxyz & dlanor
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
*
* Thank goes to (in alphabetical order)...
* acdacd           * guxtabo        * peterbostwick
* badcrew          * hrgs           * ps2dragon    
* bootlegninja     * iancer         * ragnarok2040 
* bootsector       * ichisuke       * rikimaru     
* danadamkof       * jepjepjep      * rodpad       
* dariuszg         * jimmikaelkael  * s8n          
* darkcrono666     * junebug        * spud42       
* darthkamikaz3    * katananja      * stefanol69   
* david_wgtn       * kevstah2004    * tna          
* donroberto       * lee4           * unclejun     
* ep               * lyalen         * urbigbro     
* euzeb2           * maverickhl     * vegeta       
* ffgriever        * mysticy        * voegelchen   
* frozen9999       * neme           * yangjuniori  
* germanno         * nicocmoi81     * zin0099      
* gilbertomoreira  * northbear                     
* goriath          * patters98                     
* ... and all those that contributed (and still contribute) for this project!
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

struct gsm_settings {
	char	GSM_CNF_version[64];
	char  option_1[64];
	int   option_2;
	int   exit_option;
};

int	MC_index = -1;	//index of MC where a CNF was successfully found or created

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

typedef struct gsm_exit_option {
	u8	id;
	char description[12];
	char elf_path[0x40];
} GSM_exit_option;


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

// TSR - Data Section
// ------------------

	#include "KSEG_Macros.h"

	KDef(void, KSeg, 0x00080000);  //KSeg is the base of our KSeg area
	KRel(u128, MIPS_Regs, 0x0000); //Here we save exception entry registers
	//NB: The MIPS_Regs area must be 16-byte aligned, for 128 bit MIPS registers
	//But our coding also requires MIPS_Regs == KSeg, with the lower 16 bits zero

	KRel(u32, Target_Interlace, 0x0200);
	KRel(u32, Target_Mode,      0x0204);
	KRel(u32, Target_Field,     0x0208);

	KRel(u32, Adapt_Flags,  0x020C);	//Four Byte flags combined
	KRel(u8,  Adapt_Flag_0, 0x020C);	//Byte flag
	KRel(u8,  Adapt_Flag_1, 0x020D);	//Byte flag
	KRel(u8,  Adapt_Flag_2, 0x020E);	//Byte flag
	KRel(u8,  Adapt_Flag_3, 0x020F);	//Byte flag

	KRel(u64, Target_Display, 0x0210);

	KRel(u32,  Source_Interlace, 0x0220);
	KRel(u32,  Source_Mode,    0x0224);
	KRel(u32,  Source_Field,   0x0228);
	KRel(u64, Source_Display, 0x0230);
//NB: Source_Display above is unsafe! Something zeroes it...
//NB: For this reason we now use Safe_Orig_display instead
	KRel(void*, syscallTable, 0x0240);  //Pointer to function vector table
	KRel(void*, sceSetGsCrt, 0x0244);	  //Pointer to function

	KRel(u64, Target_SMode2,	0x0248);
	KRel(u64, Target_SyncV,   0x0250);
	KRel(u64, Source_SMode2, 0x0258);
	KRel(u64, Source_SyncV, 0x0260);

	KRel(void*, DisplayHandler_p, 0x0268); //Allows DisplayHandler to find itself

	KRel(u8, Adapt_DoubleHeight, 0x026C); //Flags Height Doubling at INT&FFMD
	KRel(u8, Adapt_SMode2, 0x026D);				//Adapted SMode2 patch value

	KRel(u64, Safe_Orig_Display, 0x0270);
	KRel(u64, Calc_Display, 0x0278);

	KDef(void *, KSEG_func_base, 0x00080280);

	KDef(u64, GS_BASE,    0x12000000);
	GS_Rel(u64, PMODE,    0x0000);	//unwanted trap
	GS_Rel(u64, SMODE1,   0x0010);	//not used
	GS_Rel(u64, SMODE2,   0x0020);	//patch trap
	GS_Rel(u64, SRFSH,    0x0030);	//not used
	GS_Rel(u64, SYNCH1,   0x0040);	//unwanted trap
	GS_Rel(u64, SYNCH2,   0x0050);	//not used
	GS_Rel(u64, SYNCV,    0x0060);	//patch trap
	GS_Rel(u64, DISPFB1,  0x0070);	//not used
	GS_Rel(u64, DISPLAY1, 0x0080);	//patch trap
	GS_Rel(u64, DISPFB2,  0x0090);	//not used
	GS_Rel(u64, DISPLAY2, 0x00A0);	//patch trap
	GS_Rel(u64, EXTBUF,   0x00B0);	//not used
	GS_Rel(u64, EXTDATA,  0x00C0);	//unwanted trap
	GS_Rel(u64, EXTRITE,  0x00D0);	//not used
	GS_Rel(u64, BGCOLOR,  0x00E0);	//unwanted trap
//-----
	GS_Rel(u64, CSR,      0x0400);	//not used
	GS_Rel(u64, IMR,      0x0410);	//not used
//-----
	GS_Rel(u64, BUSDIR,   0x0440);	//not used
//-----
	GS_Rel(u64, SIGLBLID, 0x0480);	//not used
//-----
	GS_Rel(u64, SYSCNT,   0x04F0);	//not used
//-----

// TSR - Code Section
// ------------------

void *KSEG_next_func_p = &KSEG_func_base;

//----------------------------------------------------------------------------
//Here start the function segments intended for KSEG
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Start of ModdedSetGsCrt stuff
//----------------------------------------------------------------------------
KFun_start(ModdedSetGsCrt)
	".word	0\n"
	".word	0\n"
	".word	XBRA_MAGIC\n"
	".word	GSM__MAGIC\n"
	".word	0\n"
KFun_entry(ModdedSetGsCrt)
	"addiu $sp, $sp, -0x0010\n"		// reserve 16 bytes stack space (1 reg)
	"sd $ra, 0($sp)\n"	// Push return address on stack
	"la $v0, KSeg\n"	// Upper Immediate 8 = 0x80000 = Base Address
	"sw		$a0, rel_Source_Interlace($v0)\n"
	"sw		$a1, rel_Source_Mode($v0)\n"
	"sw		$a2, rel_Source_Field($v0)\n"
	"and	$a3, $a0,$a2\n"			//a3 = Interlace & Field
	"andi $a3, $a3,1\n"				//a3 &= 1 limited to 1 bit
	"sb		$a3, rel_Adapt_DoubleHeight($v0)\n"
	"ld $a0, rel_Target_SMode2($v0)\n"
	"sb		$a0, rel_Adapt_SMode2($v0)\n"
	"lw		$a0, rel_Target_Interlace($v0)\n"
	"lw		$a1, rel_Target_Mode($v0)\n"
	"lw		$a2, rel_Target_Field($v0)\n"
	"lw		$a3, rel_sceSetGsCrt($v0)\n"		// a3 -> original SetGsCrt function
	"sync.l\n"	//The opcodes sync.l and sync.p made a lot of games compatible
	"sync.p\n"	//They ensure all values are stable before the following call
	"jalr $a3\n"				// Call original SetGsCrt
	"nop\n" 
	"ld $ra, 0($sp)\n"	// Pull return address from stack 
	"jr		$ra\n"					// Return to caller
	"addiu $sp, $sp, 0x0010\n"	// Restore sp during return
KFun_end(ModdedSetGsCrt);
//----------------------------------------------------------------------------
// End of ModdedSetGsCrt stuff
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
// Start of DisplayHandler stuff
//----------------------------------------------------------------------------
/*
When the processor takes a level 2 exception, the processor switches to
kernel mode, by setting Status.ERL to 1.
*/
KFun_start(DisplayHandler)
	".word	0\n"
	".word	0\n"
	".word	XBRA_MAGIC\n"
	".word	GSM__MAGIC\n"
	".word	0\n"
KFun_entry(DisplayHandler)
	".set noreorder\n"
	".set noat\n"
	".set nomacro\n"
	"sync.l\n"
	"sync.p\n"
	"sq $k0, -0x10($zero)\n"		// Store registers reserved for kernel
	"sq $k1, -0x20($zero)\n"		// usage in interrupt/trap handling 
	//Save all MIPS registers except zero($0) k0($26) and k1($27))
	//RA NB: NO!!! ALL registers are needed in this array, for evaluations
	//RA NB: Even the $zero register is needed, as it may be used in conditionals
	"la $k0, MIPS_Regs\n"
	"sq $zero, 0($k0)\n"		//$zero
	"sq	$1, 0x10($k0)\n"		//at
	"sq	$2, 0x20($k0)\n"		//v0
	"sq	$3, 0x30($k0)\n"		//v1
	"sq	$4, 0x40($k0)\n"		//a0
	"sq	$5, 0x50($k0)\n"		//a1
	"sq	$6, 0x60($k0)\n"		//a2
	"sq	$7,	0x70($k0)\n"		//a3
	"sq	$8, 0x80($k0)\n"
	"sq	$9, 0x90($k0)\n"
	"sq	$10, 0xA0($k0)\n"
	"sq	$11, 0xB0($k0)\n"
	"sq	$12, 0xC0($k0)\n"
	"sq	$13, 0xD0($k0)\n"
	"sq	$14, 0xE0($k0)\n"
	"sq	$15, 0xF0($k0)\n"
	"sq	$16, 0x100($k0)\n"
	"sq	$17, 0x110($k0)\n"
	"sq	$18, 0x120($k0)\n"
	"sq	$19, 0x130($k0)\n"
	"sq	$20, 0x140($k0)\n"
	"sq	$21, 0x150($k0)\n"
	"sq	$22, 0x160($k0)\n"
	"sq	$23, 0x170($k0)\n"
	"sq	$24, 0x180($k0)\n"
	"sq	$25, 0x190($k0)\n"
	//0x1A0 must be set later, to initial $k0 value
	//0x1B0 must be set later, to initial $k1 value
	"sq	$28, 0x1C0($k0)\n"
	"sq	$29, 0x1D0($k0)\n"
	"sq	$30, 0x1E0($k0)\n"
	"sq	$31, 0x1F0($k0)\n"

	"lq $t0, -0x10($zero)\n"		//t0 = entry k0
	"lq	$t1, -0x20($zero)\n"		//t1 = entry k1
	"sq	$t0, 0x1A0($k0)\n"			//store entry k0 in register array
	"sq	$t1, 0x1B0($k0)\n"			//store entry k1 in register array
	
	"beqzl	$zero,main_DisplayHandler\n"	//Jump past the data block
	"nop\n"

//NB: This table needs to be here, because idiot GCC hates some forward refs.
//    It's like an opcode emulation jump table, to differentiate between
//    different kinds of access that may have triggered the debug trap we use.
//    This way the number of cases does not affect the time delay for testing.

"op_t:\n"		//First we have a table with jump offsets for opcode dependency
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//00-03
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//04-07
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//08-0B
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//0C-0F
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//10-13
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//14-17
	".word ignore-op_t,ignore-op_t, ldl_op-op_t,ldr_op-op_t\n"	//18-1B
	".word ignore-op_t,ignore-op_t, lq_op-op_t, sq_op-op_t\n"		//1C-1F
	".word lb_op-op_t, lh_op-op_t,  lwl_op-op_t,lw_op-op_t\n"		//20-23
	".word lbu_op-op_t,lhu_op-op_t, lwr_op-op_t,lwu_op-op_t\n"	//24-27
	".word sb_op-op_t, sh_op-op_t,  swl_op-op_t,sw_op-op_t\n"		//28-2B
	".word sdl_op-op_t,sdr_op-op_t, swr_op-op_t,ignore-op_t\n"	//2C-2F
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//30-33
	".word ignore-op_t,ignore-op_t, ignore-op_t,ld_op-op_t\n"		//34-37
	".word ignore-op_t,ignore-op_t, ignore-op_t,ignore-op_t\n"	//38-3B
	".word ignore-op_t,ignore-op_t, ignore-op_t,sd_op-op_t\n"		//3C-3F

"BD_t1:\n"		//Table 1 for branch opcodes when trapping branch delay slot
	".word B_com0-op_t,	B_com1-op_t,	B_J-op_t,			B_JAL-op_t\n"		//00-03
	".word B_BEQ-op_t,	B_BNE-op_t, 	B_BLEZ-op_t,	B_BGTZ-op_t\n"	//04-07
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//08-0B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//0C-0F
	".word B_BC0x-op_t,	B_BC1x-op_t,	B_skip-op_t,	B_skip-op_t\n"	//10-13
	".word B_BEQL-op_t,	B_BNEL-op_t,	B_BLEZL-op_t,	B_BGTZL-op_t\n"	//14-17
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//18-1B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//1C-1F
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//20-23
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//24-27
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//28-2B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//2C-2F
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//30-33
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//34-37
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//38-3B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//3C-3F

"BD_t2:\n"		//Table 2 for branch sub-opcodes when trapping branch delay slot
	".word B_BLTZ-op_t,	B_BGEZ-op_t,	B_BLTZL-op_t,	B_BGEZL-op_t\n"	//00-03
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//04-07
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//08-0B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//0C-0F
	".word B_BLTZAL-op_t,B_BGEZAL-op_t,B_BLTZALL-op_t,B_BGEZALL-op_t\n"	//10-13
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//14-17
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//18-1B
	".word B_skip-op_t,	B_skip-op_t,	B_skip-op_t,	B_skip-op_t\n"	//1C-1F

"main_DisplayHandler:\n"
/*
The read/write ErrorEPC register holds the virtual address at which instruction
processing can resume after servicing an error. This address can be:
- The virtual address of the instruction that caused the exception
- The virtual address of the immediately preceding branch or jump instruction
(when the instruction is in a branch delay slot, and the BD2 bit in the Cause
register is set).
*/
	"mfc0 $k1, $13\n"		//k1 = Cause bits of last exception (COP0 reg 13)
	"srl $k1, 30\n"		//k1 is aligned for BD2 (Flags branch delay slot used)
													//1 -> delay slot, 0 -> normal
	"andi $k1, 1\n"			//k1 = BD2
	"sll $k1, 2\n"			//k1 = BD2*4
	"mfc0 $k0, $30\n"		//k0 = ErrorPC (COP0 reg 30) -> MIPS instruction
	"addu $k0, $k1\n"		//Add 4 to opcode address for Branch Delay Slot
										//Next get rt (target register) and write address
										//but first check that the instruction is one we patch
	"lw $v0, 0($k0)\n"			//v0 = MIPS instruction that caused trap

	"srl $v1,$v0,26\n"				//v1 = opcode (range 0x00-0x3F)
	"andi $v1,$v1, 0x003F\n"	//v1 = pure opcode number
	"sll $v1,$v1,2\n"				//v1 = op_num*2 (word offset for jump table)
	"la $a2, KSeg\n"	//a2 -> KSeg
	"lw $a3,rel_DisplayHandler_p($a2)\n"		//a3 -> beg_DisplayHandler
	"addi $a3,$a3,op_t-beg_DisplayHandler\n"	//a3 -> op_t
	"addu $a0,$v1,$a3\n"			//a0 -> active entry in op_t
	"lw $a1,0($a0)\n"				//a1 = offset from op_t to opcode handler
	"addu $a1,$a1,$a3\n"      //a1 -> opcode handler
	"jr $a1\n"							//jump to separate opcode handlers
	"nop\n"										//with v0=instruction, a2->KSeg, a3->op_t

//For the present we ignore read operations (should never happen. Not trapped)
"ldl_op:\n"
"ldr_op:\n"
"lq_op:\n"
"lb_op:\n"
"lh_op:\n"
"lwl_op:\n"
"lw_op:\n"
"lbu_op:\n"
"lhu_op:\n"
"lwr_op:\n"
"lwu_op:\n"
"lq_op:\n"
"ld_op:\n"
"ignore:\n"		//We just ignore weird opcodes that we don't implement
	"beqzl $zero,exit_DisplayHandler\n"
	"nop\n"

//For the present we treat all write operations as 'sd'
"sq_op:\n"
"sb_op:\n"
"sh_op:\n"
"swl_op:\n"
"sw_op:\n"
"sdl_op:\n"
"sdr_op:\n"
"swr_op:\n"
"sd_op:\n"
"have_some_write:\n"				//Opcode is a write, so we must check further
	"srl $a1, $v0, 16\n"			
	"andi $a1, $a1, 0x1f\n"		//a1 = unscaled rt reg index
	"srl $a0, $v0, 21\n"
	"andi $a0, $a0, 0x1f\n"		//a0 = unscaled base reg index

	"sll $k0, $a0, 4\n"			//k0 = raw base_ix << 4 (scaled base_ix reg index)

	"addu $v1, $a2, $k0\n"		//v1 = &MIPS_Regs[base_ix]; (if type = u128)
	"lw		$a3, 0($v1)\n"			//a3 = base register value
	"andi $k1, $v0, 0xFFFF\n"	//k1 = offset field of instruction
	"addu $a3, $a3, $k1\n"		//a3 = address which triggered breakpoint

	"sll $k0, $a1, 4\n"			//k0 = raw rt_ix << 4 (scaled rt_ix reg index)
	"addu $v0, $a2, $k0\n"		//v0 = &MIPS_Regs[rt_ix];
	"ld $a1, 0($v0)\n"			//a1 = value in rt

//NB: The trapping method forces us to trap some GS registers we don't want.
//    It is crucial that the writing of those registers proceeds undisturbed.
//    This is handled by the final test case below, at label "not_wanted_reg".

//Here a1=source_data, a2->KSeg, a3=dest_address
//NB: Since address is changed to offset by ANDI, it is valid for all segments
//NB: We avoid masking a3 itself though, in case this is an unwanted register
//NB: Remasking for kseg1 should be done in each handler for wanted registers
	"andi $v0,$a3,0xFFFF\n"							//v0 = dest offset from GS_BASE
	"addi $v1,$v0,-GS_rel_SMODE2\n"
	"beqzl $v1,have_SMODE2_write\n"			//if dest == GS_reg_SMODE2
	"nop\n"
	"addi $v1,$v0,-GS_rel_DISPLAY2\n"
	"beqzl $v1,have_DISPLAYx_write\n"		//if dest == GS_reg_DISPLAY2
	"nop\n"
	"addi $v1,$v0,-GS_rel_DISPLAY1\n"
	"beqzl $v1,have_DISPLAYx_write\n"		//if dest == GS_reg_DISPLAY1
	"nop\n"
	"addi $v1,$v0,-GS_rel_SYNCV\n"
	"beqzl $v1,have_SYNCV_write\n"			//if dest == GS_reg_SYNCV
	"nop\n"
"not_wanted_reg:\n"			//Register unwanted, so perform op unchanged
	"sd $a1,0($a3)\n"		//Store source data unchanged to destination
	"beqzl $zero,exit_DisplayHandler\n"
	"nop\n"
//----------------------------	
/*SMODE2
.----.---.---------.-----------------------------------.
|Name|Pos|Format   |Contents                           |
+----+---+---------+-----------------------------------|
|INT | 0 |int 0:1:0|Interlace Mode Setting             |
|    |   |         |0 Non-Interlace Mode               |
|    |   |         |1 Interlace Mode                   |
|FFMD| 1 |int 0:1:0|Setting in Interlace Mode          |
|    |   |         |0 FIELD Mode(Read every other line)|
|    |   |         |1 FRAME Mode(Read every line)      |
|DPMS|3:2|int 0:2:0|VESA DPMS Mode Setting             |
|    |   |         |00 On          10 Suspend          |
|    |   |         |01 Stand-by    11 Off              |
^----^---^---------^-----------------------------------.*/

"have_SMODE2_write:\n"
	"lui $v0,0xB200\n"									//v0 = GS base address in kseg1
	"andi $a3,$a3,0xFFFF\n"							//a3 = GS register offset
	"or	$a3,$a3,$v0\n"									//a3 = GS register address in kseg1
	"sd $a1,rel_Source_SMode2($a2)\n"		//Source_SMode2 = a1
	"lb $v0,rel_Adapt_Flag_2($a2)\n"		//v0 = Adapt_Flag_2
	"bnel $v0,$zero,store_v0_as_SMODE2\n"	//if Separate SMODE2 fix disabled
	"or		$v0,$zero,$a1\n"								//	go use v0=a1 for SMODE2
	"srl $v0,$a1,1\n"									//v0 = a1 aligned for FFMD in bit0
	"and	$v0,$v0,$a1\n"								//v0 bit 0 = INT & FFMD
	"andi $v0,$v0,1\n"									//v0 bit 0 = INT & FFMD isolated
	"sb		$v0,rel_Adapt_DoubleHeight($a2)\n"	//store Adapt_DoubleHeight flag
	"beqz	$v0,1f\n"											//if no DoubleHeight need
	"ld $v0,rel_Target_SMode2($a2)\n"	//go use Target_SMode2 as adapted SMode2
																				//else just set v0 = Target_SMode2
 	"andi $a1,$a1,2\n"									//a1 = FFMD of Source_SMode2
 	"andi $v0,$v0,0xFFFD\n"							//v0 = Target_SMode2 without FFMD
 	"or		$v0,$v0,$a1\n"								//v0 = Target_SMode2 + Source FFMD
"1:\n" //Here v0 is adapted SMode2 value
	"sb		$v0,rel_Adapt_SMode2($a2)\n"			//Remember this adaption for later
"store_v0_as_SMODE2:\n"
	"sync.l\n"	//The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
	"sync.p\n"	//These ones give a break to ee take a breath after patching and before enter original SetGsCrt
	"beqzl	$zero,exit_DisplayHandler\n"		//Now go exit
 	"sd $v0,0($a3)\n"											//after storing GS_reg_SMODE2

//----------------------------
/*SYNCV
.----.-----.----------.
|Name|Pos. |Format    |
|----+-----+----------+
|VFP | 9:0 |int 0:10:0|
|VFPE|19:10|int 0:10:0|
|VBP |31:20|int 0:12:0|
|VBPE|41:32|int 0:12:0|
|VDP |52:42|int 0:11:0|
|VS  |??:53|int 0:??:0|
'----^-----^----------^*/

"have_SYNCV_write:\n"
	"lui $v0,0xB200\n"									//v0 = GS base address in kseg1
	"andi $a3,$a3,0xFFFF\n"							//a3 = GS register offset
	"or	$a3,$a3,$v0\n"									//a3 = GS register address in kseg1
	"sd $a1,rel_Source_SyncV($a2)\n"			//Source_SyncV = a1
	"lb $v0,rel_Adapt_Flag_3($a2)\n"		//v0 = Adapt_Flag_3
	"bnel $v0,$zero,store_v0_as_SYNCV\n"	//if Separate SYNCV fix disabled
	"or		$v0,$zero,$a1\n"								//	go use v0=a1 for SYNCV
	"ld $v0, rel_Target_SyncV($a2)\n"			//v0 = Target_SyncV
	"beql $v0,$zero,exit_DisplayHandler\n"	//if Target_SyncV is zero
 	"sd $a1,0($a3)\n"											//	go use Source_SyncV
"store_v0_as_SYNCV:\n"
	"sync.l\n"	//The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
	"sync.p\n"	//These ones give a break to ee take a breath after patching and before enter original SetGsCrt
	"beqzl	$zero,exit_DisplayHandler\n"		//Now go exit
 	"sd $v0,0($a3)\n"											//after storing GS_SYNCV

//----------------------------
"have_DISPLAYx_write:\n"	//Here a1=source_data, a2->KSeg, a3=dest_adress
	"lui $v0,0xB200\n"									//v0 = GS base address in kseg1
	"andi $a3,$a3,0xFFFF\n"							//a3 = GS register offset
	"or	$a3,$a3,$v0\n"									//a3 = GS register address in kseg1
	"sd $a1, rel_Safe_Orig_Display($a2)\n"	//request DISPLAYx value = a1
	"ld $v1, rel_Target_Display($a2)\n"			//v1=forcing DISPLAYx template

//Safe_Orig_Display == Requested   DX, DY, MAGH, MAGV, DW and DH values
//Target_Display == Modded(forced) DX, DY, MAGH, MAGV, DW and DH values
//Both are 64 bit units with encoded bit fields like GS DISPLAYx registers

//Patch to adapt request to enforced mode in v1 MUST preserve a1, a2, a3

	"lb $v0,rel_Adapt_Flag_0($a2)\n"
	"bnel $v0,$zero,91f\n"						//if(Adapt_Flag_0)
	"or $a1,$zero,$v1\n"      				//	simulate request same as forced mode
"91:\n"

	"li	$v0,0\n"						// preclear v0 as result DISPLAYx accumulator

//Here a0=free, a1=Source_Display, a2->KSeg, a3=dest_address
//Also v0=result_accumulator, v1=Target_Display, t0-t7=free

#include	"Adapt_X.c"
#include	"Adapt_Y.c"

"Adapt_Calced:\n"
	"sd $v0,rel_Calc_Display($a2)\n"  //Store new DISPLAYx value (for feedback)

//End of Patch to adapt request with the resulting request in v0

	"lb $v1,rel_Adapt_Flag_1($a2)\n"		//v1 = Adapt_Flag_1
	"bnel $v1,$zero,94f\n"								//if(Adapt_Flag_1)
	"ld $v0,rel_Target_Display($a2)\n"	//	use forced mode without adaption
"94:\n"

	"lui $a0,(GS_BASE >> 16)\n"				//a0 -> GS_BASE
	"lb $t0,rel_Adapt_SMode2($a2)\n"	//t0 = adapted SMODE2 value
	"sync.l\n"	//The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
	"sync.p\n"	//These ones give a break to ee take a breath after patching and before enter original SetGsCrt
	"sd $t0,GS_rel_SMODE2($a0)\n"			//store it in GS_reg_SMODE2

//	"sd $v0,0($a3)\n"									//Store modified GS_reg_DISPLAYx
	"sd $v0,GS_rel_DISPLAY2($a0)\n"					//Store modified GS_reg_DISPLAY2
	"sd $v0,GS_rel_DISPLAY1($a0)\n"					//Store modified GS_reg_DISPLAY2

	"ld $t0,rel_Target_SyncV($a2)\n"	//t0 = Target_SyncV
	"sync.l\n"	//The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
	"sync.p\n"	//These ones give a break to ee take a breath after patching and before enter original SetGsCrt
	"bnel $t0,$zero,96f\n"							//if Target_SyncV is non-zero
 	"sd $t0,GS_rel_SYNCV($a0)\n"			//	store it in GS_reg_SYNCV
"96:\n"

"exit_DisplayHandler_complex:\n"
//----- Here we restore some registers, used for complex calculations above
	"lq $10, 0xA0($a2)\n"		//t2
	"lq $11, 0xB0($a2)\n"		//t3
	"lq $12, 0xC0($a2)\n"		//t4
	"lq $13, 0xD0($a2)\n"		//t5
	"lq $14, 0xE0($a2)\n"		//t6
	"lq $15, 0xF0($a2)\n"		//t7
"exit_DisplayHandler:\n"
//----- Here we restore most registers used for all DisplayHandler traps
//----- Since only a few registers are used this way we only restore those
	"la $k0, MIPS_Regs\n"	//Restore MIPS_Regs via k0
	"lq $1, 0x10($k0)\n"		//at
	"lq $2, 0x20($k0)\n"		//v0
	"lq $3, 0x30($k0)\n"		//v1
	"lq $4, 0x40($k0)\n"		//a0
	"lq $5, 0x50($k0)\n"		//a1
	"lq $6, 0x60($k0)\n"		//a2
	"lq $7, 0x70($k0)\n"		//a3

//Past this point in DisplayHandler, use only k0,k1,t0,t1

	"mfc0 $k0, $13\n"	//k0 = Cause of last exception
	"srl $k0, 30\n"	//BD2 Flags debug exception in branch delay slot.
							//1 -> delay slot, 0 -> normal
	"andi $k0, 1\n"		//k0 = BD2 bit isolated
	"bnez $k0,	DisplayHandler_BranchDelaySlotException\n"
	// 	Deal properly with Branch Delay Slot Exceptions (when needed)
	"nop\n"

	"mfc0 $k0,$30\n"			//k0 = ErrorEPC
	"addiu $k0,$k0,4\n"		//k0 = ErrorEPC+4 (-> next opcode)
	"mtc0 $k0,$30\n"			//store k0 in Error Exception Program Counter
//	"sync\n"								//ensure COP0 register update before proceeding

	"b DisplayHandler_Final_Exit\n"
	"nop\n"

//----------------------------
"DisplayHandler_BranchDelaySlotException:\n"
	"mfc0 $k0,$30\n"			//k0 = Error Exception Program Counter
	"lw $k0,0($k0)\n"		//k0 = instruction at EPC location (branch or jump)
	"srl $k1,$k0,26\n"		//k1 = aligned for opcode (range 0x00-0x3F)
	"andi $k1,$k1,0x3F\n"	//k1 = pure opcode number
	"sll $k1,$k1,2\n"		//k1 = op_num*2 (offset for jump table)
	"la $t0, KSeg\n"	//t0 -> KSeg
	"lw $t1,rel_DisplayHandler_p($t0)\n"		//t1 -> beg_DisplayHandler
	"addi $t1,$t1,op_t-beg_DisplayHandler\n"	//t1 -> op_t
	"addu $k1,$k1,$t1\n"				//k1 -> op_t+offset
	"addiu $k1,$k1,BD_t1-op_t\n"	//k1 -> table entry in BD_t1
	"lw $t0,0($k1)\n"					//t0 = table entry from BD_t1
	"addu $t0,$t0,$t1\n"				//t0 -> opcode handler
	"jr $t0\n"								//jump to branch/jump opcode handlers
	"nop\n"												//with k0=instruction, t1->op_t

//----------------------------
"B_com1:\n"			//This group contains 8 different branch operations
	"srl $k1,$k0,16\n"		//k1 = aligned for sub_opcode (range 0x00-0x1F)
	"andi $k1,$k1,0x3F\n"	//k1 = pure sub_opcode number
	"sll $k1,$k1,2\n"		//k1 = sub_op_num*2 (offset for jump table)
	"la $t0, KSeg\n"	//t0 -> KSeg
	"lw $t1,rel_DisplayHandler_p($t0)\n"		//t1 -> beg_DisplayHandler
	"addi $t1,$t1,op_t-beg_DisplayHandler\n"	//t1 -> op_t
	"addu $k1,$k1,$t1\n"				//k1 -> op_t+offset
	"addiu $k1,$k1,BD_t2-op_t\n"	//k1 -> table entry in BD_t2
	"lw $t0,0($k1)\n"					//t0 = table entry from BD_t2
	"addu $t0,$t0,$t1\n"				//t0 -> opcode handler
	"jr $t0\n"								//jump to branch/jump opcode handlers
	"nop\n"												//with k0=instruction, t1->op_t

//----------------------------
"B_com0:\n"			//opcode 0x00 includes both JR and JALR
	"li $t0,0xFC1F07FF\n"		//t0 = bitmask for JALR
	"and $k1,$k0, $t0\n"			//k1 = potential JALR instruction
	"li $t0,9\n"							//t0 = JALR test constant
	"beq $k1,$t0,B_JR_JALR\n"	//if JALR identified, go deal with it
	"nop\n"
	"li $t0,0xFC1FFFFF\n"	//t0 = bitmask for JR
	"and $k1,$k0, $t0\n"		//k1 = potential JR instruction
	"li $t0,8\n"						//t0 = JR test constant
	"bne $k1,$t0,B_skip\n"	//if JR not identified, go skip this code 
	"nop\n"
"B_JR_JALR:\n"	//JR or JALR found, so make register indirect jump
	"srl $k1,$k0, 21\n"			//k1 = aligned for JR/JALR rs register number
	"andi $k1,0x1F\n"				//k1 = register number
	"sll $t1,$k1, 4\n"			//t1 = array index for saved register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $t0,$t1\n"					//t0 -> register data of JR/JALR rs register
	"lw $t0,0($t0)\n"			//t0 = jump destination address
	"mtc0 $t0,$30\n"					//store t0 in Error Exception Program Counter
//	"sync\n"										//ensure COP0 register update before proceeding
	"b DisplayHandler_Final_Exit\n"
	"nop\n"

//----------------------------
"B_J:\n"
"B_JAL:\n"
	//Here we have a definite jump with absolute address/4 in instruction
	"li $t0, 0x3FFFFFF\n"	//t0 = bitmask for jump destination bits
	"and $k1, $k0, $t0\n"		//k1 = destination bits (== destination/4)
	"sll $t0, $k1, 2\n"			//t0 = jump destination address
	"mtc0 $t0, $30\n"				//store t0 in Error Exception Program Counter
//	"sync\n"									//ensure COP0 register update before proceeding
	"b DisplayHandler_Final_Exit\n"
	"nop\n"

//----------------------------
//'likely' type branches will only trap on delay slot if branch is taken,
//so for those cases we do not need to make any further tests of conditions
"B_likely:\n"
"B_BGEZL:\n"
"B_BGEZALL:\n"
"B_BLTZL:\n"
"B_BLTZALL:\n"
"B_BEQL:\n"
"B_BNEL:\n"
"B_BLEZL:\n"
"B_BGTZL:\n"
"B_taken:\n"
//Here we have a 'branch taken' operation with relative offset/4 in instruction
	"li $t0, 0xFFFF\n"			//t0 = bitmask for branch offset bits
	"and $k1, $k0, $t0\n"		//k1 = branch offset bits (== offset/4)
	"sll $k1, 2\n"					//k1 = branch offset
	"mfc0 $t0, $30\n"				//t0 = Error Exception Program Counter
	"addiu $t0, 4\n"					//t0 = ErrorEPC+4 (-> address after branch op)
	"addu $t0, $k1\n"				//t0 = jump destination address
	"mtc0 $t0, $30\n"				//store t0 in Error Exception Program Counter
//	"sync\n"									//ensure COP0 register update before proceeding
	"b DisplayHandler_Final_Exit\n"
	"nop\n"

//----------------------------
"B_BLTZ:\n"
"B_BLTZAL:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = register number
	"sll $t1,$k1, 4\n"			//t1 = array index for saved register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $t0,$t1\n"				//t0 -> register data of rs register
	"lq $t1,0($t0)\n"			//t1 = rs register data
//	"sync\n"
	"bltz $t1,B_taken\n"
	"nop\n"
"B_not_taken:\n"
	"mfc0 $k0,$30\n"			//k0 = ErrorEPC
	"addiu $k0,$k0,8\n"		//k0 = ErrorEPC+8 pass branch_op and delay_slot
	"mtc0 $k0,$30\n"			//store k0 in Error Exception Program Counter
//	"sync\n"								//ensure COP0 register update before proceeding
	"b DisplayHandler_Final_Exit\n"
	"nop\n"
	
//----------------------------
"B_BGEZ:\n"
"B_BGEZAL:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = register number
	"sll $t1,$k1, 4\n"			//t1 = array index for saved register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $t0,$t1\n"				//t0 -> register data of rs register
	"lq $t1,0($t0)\n"			//t1 = rs register data
//	"sync\n"
	"bgez $t1,B_taken\n"
	"nop\n"
	"b B_not_taken\n"
	"nop\n"
//----------------------------
"B_BLEZ:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = register number
	"sll $t1,$k1, 4\n"			//t1 = array index for saved register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $t0,$t1\n"				//t0 -> register data of rs register
	"lq $t1,0($t0)\n"			//t1 = rs register data
//	"sync\n"
	"blez $t1,B_taken\n"
	"nop\n"
	"b B_not_taken\n"
	"nop\n"
//----------------------------
"B_BGTZ:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = register number
	"sll $t1,$k1, 4\n"			//t1 = array index for saved register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $t0,$t1\n"				//t0 -> register data of rs register
	"lq $t1,0($t0)\n"			//t1 = rs register data
//	"sync\n"
	"bgtz $t1,B_taken\n"
	"nop\n"
	"b B_not_taken\n"
	"nop\n"
//----------------------------
"B_BEQ:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = rs register number
	"sll $k1,$k1, 4\n"			//k1 = array index for saved rs register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $k1,$t0\n"				//k1 -> register data of rs register
	"lq $t1,0($k1)\n"			//t1 = rs register data
//	"sync\n"
	"srl $k1,$k0, 16\n"		//k1 = aligned for rt register number
	"andi $k1,0x1F\n"				//k1 = rt register number
	"sll $k1,$k1, 4\n"			//k1 = array index for saved rt register data
	"addu $k1,$t0\n"				//k1 -> register data of rt register
	"lq $t0,0($k1)\n"			//t0 = rt register data
//	"sync\n"
	"beq $t0,$t1,B_taken\n"
	"nop\n"
	"b B_not_taken\n"
	"nop\n"
//----------------------------
"B_BNE:\n"
	"srl $k1,$k0, 21\n"		//k1 = aligned for rs register number
	"andi $k1,0x1F\n"				//k1 = rs register number
	"sll $k1,$k1, 4\n"			//k1 = array index for saved rs register data
	"la $t0, MIPS_Regs\n"		//t0 -> saved register array
	"addu $k1,$t0\n"				//k1 -> register data of rs register
	"lq $t1,0($k1)\n"			//t1 = rs register data
//	"sync\n"
	"srl $k1,$k0, 16\n"		//k1 = aligned for rt register number
	"andi $k1,0x1F\n"				//k1 = rt register number
	"sll $k1,$k1, 4\n"			//k1 = array index for saved rt register data
	"addu $k1,$t0\n"				//k1 -> register data of rt register
	"lq $t0,0($k1)\n"			//t0 = rt register data
//	"sync\n"
	"bne $t0,$t1,B_taken\n"
	"nop\n"
	"b B_not_taken\n"
	"nop\n"
//----------------------------
"B_BC0x:\n"					//At present we do not implement COP0 branches
"B_BC1x:\n"					//At present we do not implement COP1 branches
"B_skip:\n"					//Unrecognized opcode, so just pass it by
	"mfc0 $k0, $30\n"			//k0 = ErrorEPC
	"addiu $k0, $k0, 4\n"	//k0 = ErrorEPC+4 (-> next opcode)
	"mtc0 $k0, $30\n"			//store k0 in Error Exception Program Counter
//	"sync\n"								//ensure COP0 register update before proceeding

"DisplayHandler_Final_Exit:\n"

	"la $k0, KSeg\n"	//k0 -> KSeg
	"lq $8, 0x80($k0)\n"		//t0
	"lq $9, 0x90($k0)\n"		//t1

//	"mfc0 $k0, $12\n"				// Set user mode, interrupts on
//	"ori $k0, $k0, 0x11\n"
//	"mtc0 $k0, $12\n"
//	"sync\n"

	"lq $k0, -0x10($zero)\n"		// Restore k0,k1 reserved for OS Kernel
	"lq $k1, -0x20($zero)\n"
	"sync.p\n"
	"sync.l\n"
	"eret\n"								// 	Return from exception
	"nop\n"

"end_DisplayHandler:\n"
	".set macro\n"
	".set at\n"
	".set reorder\n"
	KFun_end(DisplayHandler);
//----------------------------------------------------------------------------
// End of DisplayHandler stuff
//----------------------------------------------------------------------------

//Here end the function segments intended for KSEG
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
unsigned int get_sr() {
    register unsigned int rv;
	__asm__ __volatile__ (
	".set noreorder\n"
    "mfc0 %0, $12\n"
    "sync\n"
    "nop\n"
	".set reorder\n"
    : "=r" (rv) : );
    return rv;
}

//----------------------------------------------------------------------------
void set_sr(unsigned int v) {
	__asm__ __volatile__ (
	".set noreorder\n"
    "mtc0 %0, $12\n"
    "sync\n"
    "nop\n"
	".set reorder\n"
    : : "r" (v) );
}

//----------------------------------------------------------------------------
void set_kmode() {
	set_sr(get_sr() & (~0x19));
}

//----------------------------------------------------------------------------
void set_umode() {
	set_sr((get_sr() & (~0x19)) | 0x11);
}

//----------------------------------------------------------------------------
int disableClearUserMem()
{
	int *scanMem = (int *)0x80000000;
	int retval = -1;
	int i;
	set_kmode();
	for(i = 0; i < 0x3FFFF; i++)
	{
	 if( ((scanMem[i] & 0xffff0000) == 0x3C040000) &&	 	// lui a0, 0000
		 ((scanMem[i + 1] & 0x7C000000) == 0xC000000) &&	// jal XXXX
		 (scanMem[i + 2] == 0x34842000) )				 	// ori a0, a0, 0x2000
		 {
			 scanMem[i + 1] = NOP;
			 retval = 0;
			 break;
		 }
	}
	set_umode();
	return retval;
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

// LoadModules - Taken from the fmcb1.7 sources
// I got it from Compilable PS2 source collection
// by Lazy Bastard from GHSI (http://www.gshi.org/vb/showthread.php?t=3098)
//----------------------------------------------------------------------------
void LoadModules(void) // Loads SIO2MAN, MCMAN, MCSERV, PADMAN
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


//----------------------------------------------------------------------------
/* This allow KSEG (Kernel EE RAM Area) Memory Copy */
u32 KSEG_memcpy(void *addr, void *buf, u32 size)
{
   DI();						// Disable interrupts
   set_kmode();					// Enter kernel mode
   memcpy(buf, addr, size);		// Memory Copy
   set_umode();					// Leave kernel mode
   EI();						// Enable interrupts
   return size;
}

//----------------------------------------------------------------------------
//Get Syscall Vector Table Address - The value is different among PS2 versions, this is the reason why we must search for that!
//This routine was taken and adopted from the one created by misfire
u32 GetSyscallVectorTableAddress(void)
{
   // Here, we set magical numbers (instead of ordinary address) to system call vector table positions 0xFE and 0xFF 
   // (both are related to syscalls code numbers that are unused). Doing this, i.e., puting non-usual opcodes sequence, 
   // that probably does not exist on original code, let us use this magic numbers (fake addresses) to found
   // Syscall Vector Table Start Address. It is used "BIG BOOBS CODE" here (0x1337C0DE+0xB16B00B5) but it could be
   // anything else that fits on this concept!
   const s32 syscall_num[2] = { 0xFE, 0xFF };
   const u32 magic[2] = { 0x1337C0DE, 0xB16B00B5 };	
   u32 addr = -1;
   u32 i;
   SetSyscall(syscall_num[0], (void*)magic[0]);
   SetSyscall(syscall_num[1], (void*)magic[1]);
   set_kmode();
   // Scan start address
   for (i = 0x80000000; i < 0x80080000; i += 4) {
      if(!memcmp((u32*)i, magic, sizeof(magic))) {
         addr = i - syscall_num[0] * 4;
         break;
      }
   }
   set_umode();
   SetSyscall(syscall_num[0], (void*)0);
   SetSyscall(syscall_num[1], (void*)0);
   return addr;
} 

//----------------------------------------------------------------------------
/* Set Syscall */
void setSyscall(int number, void (*functionPtr)(void)) {
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
//----------------------------------------------------------------------------
int Create_File (char *filepath, u8 *buf, int size) // Creates a file on mc0:
{
	int fd;
	char filepath2[0x40];

	sprintf(filepath2, "mc0:%s", filepath);
	fd = fioOpen(filepath2, O_WRONLY | O_CREAT);
	if(fd < 0)
	{	filepath2[2] = '1'; //change path to mc1:
		fd = fioOpen(filepath2, O_WRONLY | O_CREAT);
		if(fd < 0)
			return 0;
		MC_index = 1;
	} else {
		MC_index = 0;
	}
	if(fioWrite(fd, buf, size) < 0) return 0;
	fioClose(fd);
	
	return 1;
}
//----------------------------------------------------------------------------
int File_Exist (char *filepath) // Check that a file is existing
{
	int fd;
	char filepath2[0x40];

	sprintf(filepath2, "mc0:%s", filepath);
	fd = fioOpen(filepath2, O_RDONLY);
	if(fd < 0)
	{	filepath2[2] = '1'; //change path to mc1:
		fd = fioOpen(filepath2, O_RDONLY);
		if(fd < 0)
			return 0;
		MC_index = 1;
	} else {
		MC_index = 0;
	}
	fioClose(fd);	
	
	return 1;
}
//----------------------------------------------------------------------------
//________________ From uLaunchELF ______________________
//---------------------------------------------------------------------------
// get_CNF_string is the main CNF parser called for each CNF variable in a
// CNF file. Input and output data is handled via its pointer parameters.
// The return value flags 'false' when no variable is found. (normal at EOF)
//---------------------------------------------------------------------------
int	get_CNF_string(unsigned char **CNF_p_p,
                   unsigned char **name_p_p,
                   unsigned char **value_p_p)
{
	unsigned char *np, *vp, *tp = *CNF_p_p;

start_line:
	while((*tp<=' ') && (*tp>'\0')) tp+=1;  	//Skip leading whitespace, if any
	if(*tp=='\0') return 0;            			//but exit at EOF
	np = tp;                               		//Current pos is potential name
	if(*tp<'A')                            		//but may be a comment line
	{                                      		//We must skip a comment line
		while((*tp!='\r')&&(*tp!='\n')&&(*tp>'\0')) tp+=1;  //Seek line end
		goto start_line;                     	//Go back to try next line
	}

	while((*tp>='A')||((*tp>='0')&&(*tp<='9'))) tp+=1;  //Seek name end
	if(*tp=='\0') return 0;            			//but exit at EOF

	while((*tp<=' ') && (*tp>'\0'))
		*tp++ = '\0';                        	//zero&skip post-name whitespace
	if(*tp!='=') return 0;  	           		//exit (syntax error) if '=' missing
	*tp++ = '\0';                          		//zero '=' (possibly terminating name)

	tp += 1;									//skip '='
	while((*tp<=' ') && (*tp>'\0')				//Skip pre-value whitespace, if any
		&& (*tp!='\r') && (*tp!='\n')			//but do not pass EOL
		&& (*tp!='\7')     						//allow ctrl-G (BEL) in value
		)tp+=1;									//skip tested whitespace characters
	if(*tp=='\0') return 0;						//but exit at EOF
	vp = tp;									//Current pos is potential value	

	while((*tp!='\r')&&(*tp!='\n')&&(*tp!='\0')) tp+=1;  //Seek line end
	if(*tp!='\0') *tp++ = '\0';            		//terminate value (passing if not EOF)
	while((*tp<=' ') && (*tp>'\0')) tp+=1;  	//Skip following whitespace, if any

	*CNF_p_p = tp;                         		//return new CNF file position
	*name_p_p = np;                        		//return found variable name
	*value_p_p = vp;                       		//return found variable value
	return 1;                           		//return control to caller
}	//Ends get_CNF_string
//----------------------------------------------------------------
// Load CNF
//----------------------------------------------------------------
int loadConfig(char *filepath)
{
	int fd, var_cnt, user_index=0;

	size_t CNF_size;
	char path[0x40];
	unsigned char *RAM_p, *CNF_p, *name, *value;
	
	sprintf(path, "mc0:%s", filepath);
	fd = -1;
	fd = fioOpen(path, O_RDONLY);  // Try to open cnf from mc0:
	if(fd < 0) {
		path[2] = '1'; //change path to mc1:
		fd = fioOpen(path, O_RDONLY);  // Try to open cnf from mc1:
		if(fd < 0) {
			return 0;
		}
		MC_index = 1;
	} else {
		MC_index = 0;
	}
	// This point is only reached after succefully opening CNF	 

	CNF_size = lseek(fd, 0, SEEK_END);
	fioLseek(fd, 0, SEEK_SET);
	RAM_p = (char*)malloc(CNF_size);
	CNF_p = RAM_p;
	if(CNF_p == NULL) {
		close(fd);
failed_load:
		return 0;
	}
	fioRead(fd, CNF_p, CNF_size); // Read CNF as one long string
	fioClose(fd);
	CNF_p[CNF_size] = '\0'; // Terminate the CNF string

	for (var_cnt = 0; get_CNF_string(&CNF_p, &name, &value); var_cnt++) {
		//A variable was found, now we dispose of its value.
		//The first variable should always be GSM_CNF_version (defines file content)
		if(var_cnt == 0){ //if this is the first variable found ?
			if(!strcmp(name,"GSM_CNF_version")) { //if its name is GSM_CNF_version?
				strncpy(GSM->GSM_CNF_version, value, 63); //Copy body of value string
				GSM->GSM_CNF_version[63] = 0; //Force value string termination 
				//This value can be used to modify CNF interpretation below
			}else{ //if first variable is NOT GSM_CNF_version, the file is invalid
				goto failed_load;
			}
		}
		if(strcmp(GSM->GSM_CNF_version, CNF_VERSION) != 0)
			goto failed_load;

		//The two variables below are just examples for string VS numeric usage
		if(!strcmp(name,"Option_1")) {
			strncpy(GSM->option_1, value, 63); //Copy body of value string
			GSM->option_1[63] = 0; //Force value string termination 
			continue;
		}		
		if(!strcmp(name,"Option_2")) {
			GSM->option_2 = atoi(value);	//This syntax is used for numeric values
			continue;
		}
		if(!strcmp(name,"exit_option")) {
			GSM->exit_option = atoi(value);	//This syntax is used for numeric values
			continue;
		}
		if(!strcmp(name,"User_Index")) {
			user_index = atoi(value) & (user_vmode_slots-1); //Load and limit index
			continue;
		}
		if(!strcmp(name,"interlace")) {
			user_vmode[user_index].interlace = atoi(value);
			continue;
		}
		if(!strcmp(name,"mode")) {
			user_vmode[user_index].mode = atoi(value);
			continue;
		}
		if(!strcmp(name,"field")) {
			user_vmode[user_index].field = atoi(value);
			continue;
		}
		if(!strcmp(name,"display")) {
			user_vmode[user_index].display = atol(value);
			continue;
		}
		if(!strcmp(name,"syncv")) {
			user_vmode[user_index].syncv = atol(value);
			continue;
		}
		if(!strcmp(name,"smode2")) {
			user_vmode[user_index].smode2 = atol(value);
			continue;
		}
	} // ends for
	free(RAM_p);
	//NB: Because the RAM buffer is released here, all string values must be
	//    copied to permanent variable space in the CNF interpretation above.

	return 1;
}
//------------------------------
//endfunc loadConfig
//----------------------------------------------------------------
// Save CNF
//----------------------------------------------------------------
int saveConfig(char *filepath)
{
	int i, ret, fd;
	char tmp[26*MAX_PATH + 30*MAX_PATH];
	char path[0x40];
	size_t CNF_size, CNF_step;

	sprintf(path, "mc0:%s", filepath);
	if(MC_index == 1)
		path[2] = '1';

	CNF_size = 0;
	
	sprintf(tmp+CNF_size,
		"GSM_CNF_version = %s\r\n"
		"Option_1 = %s\r\n"
		"Option_2 = %d\r\n"
		"exit_option = %d\r\n"
		"%n",           // %n causes NO output, but only a measurement
		GSM->GSM_CNF_version,
		GSM->option_1,
		GSM->option_2,
		GSM->exit_option,
		&CNF_step       // This variable measures the size of sprintf data
  );
	CNF_size += CNF_step;

	for(i=0; i<user_vmode_slots; i++){	//Loop to save the user-defined vmodes
		sprintf(tmp+CNF_size,
			"User_Index = %d\r\n"
			"interlace = %d \r\n"
			"mode = %d \r\n"
			"field = %d\r\n"
			"display = %ld\r\n"
			"syncv = %ld\r\n"
			"smode2 = %ld\r\n"
			"%n",           // %n causes NO output, but only a measurement
			i,
			user_vmode[i].interlace,
			user_vmode[i].mode,
			user_vmode[i].field,
			user_vmode[i].display,
			user_vmode[i].syncv,
			user_vmode[i].smode2,
			&CNF_step       // This variable measures the size of sprintf data
	  );
		CNF_size += CNF_step;
	}//ends for

	fd = fioOpen(path,O_CREAT|O_WRONLY|O_TRUNC);  // Try to open cnf on one MC
	if(fd < 0) {
		path[2] ^= 1; //change path to other MC
		fd = fioOpen(path, O_CREAT|O_WRONLY|O_TRUNC);  // Try to open cnf on other MC
		if(fd < 0) {
			return 0;
		}
	}
	// This point is only reached after succefully opening CNF	 
	MC_index = path[2]-'0';

	ret = fioWrite(fd,&tmp,CNF_size);
	fioClose(fd);
	if(ret==CNF_size)
		return 1; //Success
	else
		return 0; //Failure
}
//------------------------------
//endfunc saveConfig
//---------------------------------------------------------------------------
void Set_Default_Settings(void)
{
	strcpy(GSM->GSM_CNF_version, CNF_VERSION);
	strcpy(GSM->option_1, "Default");
	GSM->option_2 = 0;  
	GSM->exit_option = -1;  //NB: Allows test of 0 value loaded from CNF
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
// Some Sprites- Took and adopted from the ones into basic gsKit Example sources
// I got it from Compilable PS2 source collection
// by Lazy Bastard from GHSI (http://www.gshi.org/vb/showthread.php?t=3098)
void Some_Sprites()
{
/*	//Pentagon - Alone is ok. Together with others, gives BSOD and freezing (probably due to memory leakage)
	float *LineStrip;
	float *LineStripPtr;
	LineStripPtr = LineStrip = malloc(12 * sizeof(float));
	*LineStrip++ = 575;	// Segment 1 X
	*LineStrip++ = 260;	// Segment 1 Y
	*LineStrip++ = 625;	// Segment 2 X
	*LineStrip++ = 280;	// Segment 2 Y
	*LineStrip++ = 600;	// Segment 3 X
	*LineStrip++ = 340;	// Segment 3 Y
	*LineStrip++ = 550;	// Segment 4 X
	*LineStrip++ = 340;	// Segment 4 Y
	*LineStrip++ = 525;	// Segment 6 X
	*LineStrip++ = 280;	// Segment 6 Y
	*LineStrip++ = 575;	// Segment 6 X
	*LineStrip++ = 260;	// Segment 6 Y
	gsKit_prim_line_strip(gsGlobal, LineStripPtr, 6, 1, Yellow);
	free(LineStripPtr); LineStripPtr = NULL;
	free(LineStrip); LineStrip = NULL;

	//Square - Alone is ok. Together with others, gives BSOD and freezing (probably due to memory leakage)
	gsKit_prim_quad(gsGlobal, 550.0f, 250.0f, 550.0f, 320.0f, 590.0f, 310.0f,590.0f, 350.0f, 4, Blue);
*/
	//Degradee-featured Square - Alone is ok. Together with others, gives BSOD and freezing (probably due to memory leakage)
	gsKit_prim_quad_gouraud(gsGlobal, 
					   530.0f, 280.0f, 
					   530.0f, 320.0f, 
					   570.0f, 280.0f,
					   570.0f, 320.0f, 2,
					   Red, Green, Blue, Black);
}
//----------------------------------------------------------------------------
//endfunc Some_Sprites
//----------------------------------------------------------------------------


int *CreateCoeffInt( int nLen, int nNewLen, int bShrink ) {

 int nSum    = 0;
 int nSum2   = 0;
 int *pRes   = (int*) malloc( 2 * nLen * sizeof(int) );
 int *pCoeff = pRes;
 int nNorm   = (bShrink) ? (nNewLen << 12) / nLen : 0x1000;
 int nDenom  = (bShrink) ? nLen : nNewLen;
 int i;

 memset( pRes, 0, 2 * nLen * sizeof(int) );

 for( i = 0; i < nLen; i++, pCoeff += 2 ) {

  nSum2 = nSum + nNewLen;

  if(nSum2 > nLen) {

   *pCoeff = ((nLen - nSum) << 12) / nDenom;
   pCoeff[1] = ((nSum2 - nLen) << 12) / nDenom;
   nSum2 -= nLen;

  } else {

   *pCoeff = nNorm;

   if(nSum2 == nLen) {
    pCoeff[1] = -1;
    nSum2 = 0;

   } // end if

  } // end else

  nSum = nSum2;

 } // end for

 return pRes;

} // CreateCoeffInt
//--------------------------------------------------------------
int ShrinkData( u8 *pInBuff, u16 wWidth, u16 wHeight, u8 *pOutBuff, u16 wNewWidth, u16 wNewHeight ) {

 u8 *pLine    = pInBuff, *pPix;
 u8 *pOutLine = pOutBuff;
 u32 dwInLn   = ( 3 * wWidth + 3 ) & ~3;
 u32 dwOutLn  = ( 3 * wNewWidth + 3 ) & ~3;

 int  x, y, i, ii;
 int  bCrossRow, bCrossCol;
 int *pRowCoeff = CreateCoeffInt( wWidth, wNewWidth, 1 );
 int *pColCoeff = CreateCoeffInt( wHeight, wNewHeight, 1 );

 int *pXCoeff, *pYCoeff = pColCoeff;
 u32  dwBuffLn = 3 * wNewWidth * sizeof( u32 );
 u32 *pdwBuff = ( u32* ) malloc( 6 * wNewWidth * sizeof( u32 ) );
 u32 *pdwCurrLn = pdwBuff;  
 u32 *pdwNextLn = pdwBuff + 3 * wNewWidth;
 u32 *pdwCurrPix;
 u32  dwTmp, *pdwNextPix;

 memset( pdwBuff, 0, 2 * dwBuffLn );

 y = 0;
 
 while( y < wNewHeight ) {

  pPix = pLine;
  pLine += dwInLn;

  pdwCurrPix = pdwCurrLn;
  pdwNextPix = pdwNextLn;

  x = 0;
  pXCoeff = pRowCoeff;
  bCrossRow = pYCoeff[ 1 ] > 0;
  
  while( x < wNewWidth ) {
  
   dwTmp = *pXCoeff * *pYCoeff;
   for ( i = 0; i < 3; i++ )
    pdwCurrPix[ i ] += dwTmp * pPix[i];
   
   bCrossCol = pXCoeff[ 1 ] > 0;
   
   if ( bCrossCol ) {

    dwTmp = pXCoeff[ 1 ] * *pYCoeff;
    for ( i = 0, ii = 3; i < 3; i++, ii++ )
     pdwCurrPix[ ii ] += dwTmp * pPix[ i ];

   } // end if

   if ( bCrossRow ) {

    dwTmp = *pXCoeff * pYCoeff[ 1 ];
    for( i = 0; i < 3; i++ )
     pdwNextPix[ i ] += dwTmp * pPix[ i ];
    
    if ( bCrossCol ) {

     dwTmp = pXCoeff[ 1 ] * pYCoeff[ 1 ];
     for ( i = 0, ii = 3; i < 3; i++, ii++ )
      pdwNextPix[ ii ] += dwTmp * pPix[ i ];

    } // end if

   } // end if

   if ( pXCoeff[ 1 ] ) {

    x++;
    pdwCurrPix += 3;
    pdwNextPix += 3;

   } // end if
   
   pXCoeff += 2;
   pPix += 3;

  } // end while
  
  if ( pYCoeff[ 1 ] ) {

   // set result line
   pdwCurrPix = pdwCurrLn;
   pPix = pOutLine;
   
   for ( i = 3 * wNewWidth; i > 0; i--, pdwCurrPix++, pPix++ )
    *pPix = ((u8*)pdwCurrPix)[3];

   // prepare line buffers
   pdwCurrPix = pdwNextLn;
   pdwNextLn = pdwCurrLn;
   pdwCurrLn = pdwCurrPix;
   
   memset( pdwNextLn, 0, dwBuffLn );
   
   y++;
   pOutLine += dwOutLn;

  } // end if
  
  pYCoeff += 2;

 } // end while

 free( pRowCoeff );
 free( pColCoeff );
 free( pdwBuff );

 return 1;

} // end ShrinkData
//--------------------------------------------------------------
int EnlargeData( u8 *pInBuff, u16 wWidth, u16 wHeight, u8 *pOutBuff, u16 wNewWidth, u16 wNewHeight ) {

 u8 *pLine = pInBuff,
    *pPix  = pLine,
    *pPixOld,
    *pUpPix,
    *pUpPixOld;
 u8 *pOutLine = pOutBuff, *pOutPix;
 u32 dwInLn   = ( 3 * wWidth + 3 ) & ~3;
 u32 dwOutLn  = ( 3 * wNewWidth + 3 ) & ~3;

 int x, y, i;
 int bCrossRow, bCrossCol;
 
 int *pRowCoeff = CreateCoeffInt( wNewWidth, wWidth, 0 );
 int *pColCoeff = CreateCoeffInt( wNewHeight, wHeight, 0 );
 int *pXCoeff, *pYCoeff = pColCoeff;
 
 u32 dwTmp, dwPtTmp[ 3 ];
 
 y = 0;
 
 while( y < wHeight ) {
 
  bCrossRow = pYCoeff[ 1 ] > 0;
  x         = 0;
  pXCoeff   = pRowCoeff;
  pOutPix   = pOutLine;
  pOutLine += dwOutLn;
  pUpPix    = pLine;
  
  if ( pYCoeff[ 1 ] ) {

   y++;
   pLine += dwInLn;
   pPix = pLine;

  } // end if
  
  while( x < wWidth ) {

   bCrossCol = pXCoeff[ 1 ] > 0;
   pUpPixOld = pUpPix;
   pPixOld  = pPix;
   
   if( pXCoeff[ 1 ] ) {

    x++;
    pUpPix += 3;
    pPix += 3;

   } // end if
   
   dwTmp = *pXCoeff * *pYCoeff;
   
   for ( i = 0; i < 3; i++ )
    dwPtTmp[ i ] = dwTmp * pUpPixOld[ i ];

   if ( bCrossCol ) {

    dwTmp = pXCoeff[ 1 ] * *pYCoeff;
    for ( i = 0; i < 3; i++ )
     dwPtTmp[ i ] += dwTmp * pUpPix[ i ];

   } // end if

   if ( bCrossRow ) {

    dwTmp = *pXCoeff * pYCoeff[ 1 ];
    for ( i = 0; i < 3; i++ )
     dwPtTmp[ i ] += dwTmp * pPixOld[ i ];
    
    if ( bCrossCol ) {

     dwTmp = pXCoeff[ 1 ] * pYCoeff[ 1 ];
     for(i = 0; i < 3; i++)
      dwPtTmp[ i ] += dwTmp * pPix[ i ];

    } // end if

   } // end if
   
   for ( i = 0; i < 3; i++, pOutPix++ )
    *pOutPix = (  ( u8* )( dwPtTmp + i )  )[ 3 ];
   
   pXCoeff += 2;

  } // end while
  
  pYCoeff += 2;

 } // end while
 
 free( pRowCoeff );
 free( pColCoeff );

 return 1;

} // end EnlargeData

//------------------------------------------------------------------------------------------------------------------------
// ScaleBitmap - Taken from the fmcb1.7 sources
int ScaleBitmap( u8* pInBuff, u16 wWidth, u16 wHeight, u8** pOutBuff, u16 wNewWidth, u16 wNewHeight ) {
 int lRet;
 // check for valid size
 if( wWidth > wNewWidth && wHeight < wNewHeight ) return 0;
 if( wHeight > wNewHeight && wWidth < wNewWidth ) return 0;
 // allocate memory
 *pOutBuff = ( u8* ) memalign(   128, (  ( 3 * wNewWidth + 3 ) & ~3  ) * wNewHeight   );
 if( !*pOutBuff )return 0;
 if( wWidth >= wNewWidth && wHeight >= wNewHeight )
  lRet = ShrinkData( pInBuff, wWidth, wHeight, *pOutBuff, wNewWidth, wNewHeight );
 else
  lRet = EnlargeData( pInBuff, wWidth, wHeight, *pOutBuff, wNewWidth, wNewHeight );
 return lRet;
} // end ScaleBitmap

//------------------------------------------------------------------------------------------------------------------------
// Load_Splash - Taken from the fmcb1.7 sources
void Load_Splash(void)
{
	jpgData* Jpg;
	u8*      ImgData;

	if( ( Jpg = jpgOpenRAW ( &splash, size_splash, JPG_WIDTH_FIX ) ) > 0 ){		
		if( ( ImgData = malloc (  Jpg->width * Jpg->height * ( Jpg->bpp / 8 ) ) ) > 0 ){
			if( ( jpgReadImage( Jpg, ImgData ) ) != -1 ){
				if( ( ScaleBitmap( ImgData, Jpg->width, Jpg->height, (void*)&TexSkin.Mem, gsGlobal->Width, gsGlobal->Height) ) != 0 ){
					TexSkin.PSM = GS_PSM_CT24;
					TexSkin.VramClut = 0;
					TexSkin.Clut = NULL;
					TexSkin.Width =  gsGlobal->Width;
					TexSkin.Height = gsGlobal->Height;
					TexSkin.Filter = GS_FILTER_NEAREST;
					gsGlobal->CurrentPointer=0x15B000;
					//gsGlobal->CurrentPointer=0x144000;
					//gsGlobal->CurrentPointer=0x288000;
					TexSkin.Vram = gsKit_vram_alloc(gsGlobal,
				 	gsKit_texture_size(TexSkin.Width, TexSkin.Height, TexSkin.PSM),
				 		GSKIT_ALLOC_USERBUFFER);
					gsKit_texture_upload(gsGlobal, &TexSkin);
					free(TexSkin.Mem);
				} // end if
				jpgClose( Jpg );
			} // end if
		} // end if
		free(ImgData);
	} // end if
}


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
GS_BGCOLOUR = 0xFF00FF; // magenta
	u32 SyscallVectorTableAddress;
	u32 SetGsCrtVectorAddress;
	u32 SetGsCrt_Addr;
	XBRA_header *setGsCrt_XBRA_p;

	SyscallVectorTableAddress = GetSyscallVectorTableAddress();
	SetGsCrtVectorAddress = SyscallVectorTableAddress + 2 * 4;
	
	printf("Syscall Vector Table found at 0x%08x\n", SyscallVectorTableAddress);
	   
GS_BGCOLOUR = 0x0000FF; // red
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

GS_BGCOLOUR = 0xFFFF00; // cyan
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
    
GS_BGCOLOUR = 0x00FF00; // lime
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
GS_BGCOLOUR = 0x006400; // DarkGreen

}




/*
+------------------------------+
|                              |
|   M A I N   F U N C T I O N  |
|                              |
+------------------------------+
*/
int main(void)
{   
	//---------- Start of variables stuff ----------
	// Launch ELF
	char elf_path[0x40];
	char party[1] = {0};
	char *status_msg_p = NULL;
   
	int edge_size;  //Used for screen rectangle drawing
	int text_height; //Used for text rows
 
	// CNF (Configuration File)
	u32 size;
	
	// Toggles
	char *on_off_s[] = {"ON", "OFF"};

	// Pre-defined vmodes 
	// Some of following vmodes gives BOSD and/or freezing, depending on the console version and the TV/Monitor set
	// I believe that this symptom is due to a sofware limitation - more specifically, on BIOS - prior than a hardware one
	//
	//		id	category	description					interlace			mode				 field	   display                         dh   dw     magv magh dy  dx    syncv
	//		--	--------	-----------					---------			----				 -----	   ----------------------------    --   --     ---- ---- --  --    ------------------
	volatile static GSM_predef_vmode predef_vmode[35] = {

		{  0, SDTV_VMODE,"NTSC-I Full Buffer         ",	GS_INTERLACED,		GS_MODE_NTSC,			GS_FIELD,	(u64)make_display_magic_number( 448, 2560,   0,   3,   46, 700), 0x00C7800601A01801},
		{  1, SDTV_VMODE,"PAL-I  Full Buffer         ",	GS_INTERLACED,		GS_MODE_PAL,			GS_FIELD,	(u64)make_display_magic_number( 512, 2560,   0,   3,   70, 720), 0x00A9000502101401},
		{  2, SDTV_VMODE,"PAL60-I  Full Buffer       ",	GS_INTERLACED,		GS_MODE_PAL,			GS_FIELD,	(u64)make_display_magic_number( 448, 2560,   0,   3,   46, 700), 0x00C7800601A01801},
		{  3, SDTV_VMODE,"NTSC-NI                    ",	GS_NONINTERLACED,	GS_MODE_NTSC,			GS_FRAME,	(u64)make_display_magic_number( 224, 2560,   0,   3,   26, 700), 0x00C7800601A01802},
		{  4, SDTV_VMODE,"PAL-NI                     ",	GS_NONINTERLACED,	GS_MODE_PAL,			GS_FRAME,	(u64)make_display_magic_number( 256, 2560,   0,   3,   37, 720), 0x00A9000502101404},
		{  5, SDTV_VMODE,"PAL60-NI                   ",	GS_NONINTERLACED,	GS_MODE_PAL,			GS_FRAME,	(u64)make_display_magic_number( 224, 2560,   0,   3,   26, 700), 0x00C7800601A01802},
		{  6, SDTV_VMODE,"NTSC-NI to NTSC-I Field    ",	GS_INTERLACED,		GS_MODE_NTSC,			GS_FIELD,	(u64)make_display_magic_number( 448, 2560,   0,   3,   46, 700), 0x00C7800601A01802},
		{  7, SDTV_VMODE,"PAL-NI  to PAL-I  Field    ",	GS_INTERLACED,		GS_MODE_PAL,			GS_FIELD,	(u64)make_display_magic_number( 512, 2560,   0,   3,   70, 720), 0x00A9000502101404},
		{  8, SDTV_VMODE,"NTSC-I Half Buffer         ",	GS_INTERLACED,		GS_MODE_NTSC,			GS_FRAME,	(u64)make_display_magic_number( 224, 2560,   0,   3,   26, 700), 0x00C7800601A01802},
		{  9, SDTV_VMODE,"PAL-I  Half Buffer         ",	GS_INTERLACED,		GS_MODE_PAL,			GS_FRAME,	(u64)make_display_magic_number( 256, 2560,   0,   3,   37, 720), 0x00A9000502101404},

		{  10, PS1_VMODE, "PS1 NTSC via HDTV 480p@60Hz",	GS_NONINTERLACED,	GS_MODE_DTV_480P,		GS_FRAME,	(u64)make_display_magic_number( 256, 2560,   0,   1,   12, 736), 0x00C78C0001E00006},	// From dlanor's experimentations and his discussions with DarkCrono666
		{  11, PS1_VMODE, "PS1 PAL via HDTV 576p@50Hz ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,		GS_FRAME,	(u64)make_display_magic_number( 256, 2560,   0,   1,   23, 756), 0x00A9000002700005},	// From dlanor's experimentations and his discussions with DarkCrono666

		{ 12, HDTV_VMODE,"HDTV 480p@60Hz             ",	GS_NONINTERLACED,	GS_MODE_DTV_480P,		GS_FRAME, 	(u64)make_display_magic_number( 480, 1280,   0,   1,   51, 308), 0x00C78C0001E00006},
		{ 13, HDTV_VMODE,"HDTV 576p@50Hz             ",	GS_NONINTERLACED,	GS_MODE_DTV_576P,		GS_FRAME,	(u64)make_display_magic_number( 576, 1280,   0,   1,   64, 320), 0x00A9000002700005},	// From patters98's idea, EEUG's answer and dlanor's experimentations
		{ 14, HDTV_VMODE,"HDTV 720p@60Hz             ",	GS_NONINTERLACED,	GS_MODE_DTV_720P,		GS_FRAME, 	(u64)make_display_magic_number( 720, 1280,   1,   1,   24, 302), 0x00AB400001400005},
		{ 15, HDTV_VMODE,"HDTV 1080i@60Hz Full Buffer+Field",	GS_INTERLACED,		GS_MODE_DTV_1080I,		GS_FIELD, 	(u64)make_display_magic_number(1080, 1920,   1,   2,   48, 238), 0x0150E00201C00005},
		{ 16, HDTV_VMODE,"HDTV 1080i@60Hz Half Buffer+Frame",	GS_INTERLACED,		GS_MODE_DTV_1080I,		GS_FRAME, 	(u64)make_display_magic_number(1080, 1920,   0,   2,   48, 238), 0x0150E00201C00005},

		{ 17, VGA_VMODE, "VGA 640x480@60Hz           ",	GS_NONINTERLACED,	GS_MODE_VGA_640_60,		GS_FRAME, 	(u64)make_display_magic_number( 480, 1280,   0,   1,   54, 276), 0x004780000210000A},
		{ 18, VGA_VMODE, "VGA 640x960i@60Hz          ",	GS_INTERLACED,		GS_MODE_VGA_640_60,		GS_FIELD,	(u64)make_display_magic_number( 960, 1280,   1,   1,  128, 291), 0x004F80000210000A},	// doctorxyz's experimentations from original GS_MODE_VGA_640_60 vmode. Values were taken, adopted and recalculated
		{ 19, VGA_VMODE, "VGA 640x480@72Hz           ",	GS_NONINTERLACED,	GS_MODE_VGA_640_72,		GS_FRAME,	(u64)make_display_magic_number( 480, 1280,   0,   1,   18, 330), 0x0067800001C00009},
		{ 20, VGA_VMODE, "VGA 640x480@75Hz           ",	GS_NONINTERLACED,	GS_MODE_VGA_640_75,		GS_FRAME, 	(u64)make_display_magic_number( 480, 1280,   0,   1,   18, 360), 0x0067800001000001},
		{ 21, VGA_VMODE, "VGA 640x480p@85Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_640_85,		GS_FRAME,	(u64)make_display_magic_number( 480, 1280,   0,   1,   18, 260), 0x0067800001000001},
		{ 22, VGA_VMODE, "VGA 800x600p@56Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_800_56,		GS_FRAME,	(u64)make_display_magic_number( 600, 1600,   0,   1,   25, 450), 0x0049600001600001},
		{ 23, VGA_VMODE, "VGA 800x600p@60Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_800_60,		GS_FRAME, 	(u64)make_display_magic_number( 600, 1600,   0,   1,   25, 465), 0x0089600001700001},
		{ 24, VGA_VMODE, "VGA 800x600p@72Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_800_72,		GS_FRAME,	(u64)make_display_magic_number( 600, 1600,   0,   1,   25, 465), 0x00C9600001700025},
		{ 25, VGA_VMODE, "VGA 800x600p@75Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_800_75,		GS_FRAME, 	(u64)make_display_magic_number( 600, 1600,   0,   1,   25, 510), 0x0069600001500001},
		{ 26, VGA_VMODE, "VGA 800x600p@85Hz          ",	GS_NONINTERLACED,	GS_MODE_VGA_800_85,		GS_FRAME,	(u64)make_display_magic_number( 600, 1600,   0,   1,   15, 500), 0x0069600001B00001},
		{ 27, VGA_VMODE, "VGA 1024x768p@60Hz         ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_60,	GS_FRAME, 	(u64)make_display_magic_number( 768,  2048,  0,   2,   30, 580), 0x00CC000001D00003},
		{ 28, VGA_VMODE, "VGA 1024x768p@70Hz         ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_70,	GS_FRAME,	(u64)make_display_magic_number( 768,  1024,  0,   0,   30, 266), 0x00CC000001D00003},
		{ 29, VGA_VMODE, "VGA 1024x768p@75Hz         ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_75,	GS_FRAME, 	(u64)make_display_magic_number( 768,  1024,  0,   0,   30, 260), 0x006C000001C00001},
		{ 30, VGA_VMODE, "VGA 1024x768p@85Hz         ",	GS_NONINTERLACED,	GS_MODE_VGA_1024_85,	GS_FRAME,	(u64)make_display_magic_number( 768,  1024,  0,   0,   30, 290), 0x006C000002400001},
		{ 31, VGA_VMODE, "VGA 1280x1024p@60Hz        ",	GS_NONINTERLACED,	GS_MODE_VGA_1280_60,	GS_FRAME, 	(u64)make_display_magic_number( 1024, 1280,  1,   1,   40, 350), 0x0070000002600001},
		{ 32, VGA_VMODE, "VGA 1280x1024p@75Hz        ",	GS_NONINTERLACED,	GS_MODE_VGA_1280_75,	GS_FRAME, 	(u64)make_display_magic_number( 1024, 1280,  1,   1,   40, 350), 0x0070000002600001},

		{ 33, SDTV_VMODE,"NTSC-NI Full Buffer-Frame+NoSyncV",	GS_NONINTERLACED,	GS_MODE_NTSC,		 GS_FRAME, (u64)make_display_magic_number(  448, 2560,  0,   3,   47, 632), 0                 },	// lee4's NTSC testing variant1 made by doctorxyz
		{ 34, SDTV_VMODE,"NTSC-NI Full Buffer-Field+NoSyncV",	GS_NONINTERLACED,	GS_MODE_NTSC,		 GS_FIELD, (u64)make_display_magic_number(  448, 2560,  0,   3,   47, 632), 0                 }	// lee4's NTSC testing variant2 made by doctorxyz

	}; //ends predef_vmode definition

	u32 predef_vmode_size = 	sizeof( predef_vmode ) / sizeof( predef_vmode[0] );

	// predef_vmode_toggle -> Aux for pre-defined vmodes
	//			555: Value loaded from CNF File
	//			999: None value chosen yet
	// Other values: Value chosen by user
	int predef_vmode_toggle = 999;

	//----------------------------------------------------------------------------

	// Exit options
	//
	//	id	description		path
	//	--	-----------		--------
 	volatile static GSM_exit_option exit_option[8] = {
		{ 0, "PS2 BROWSER",	"PS2 BROWSER"},
		{ 1, "DEV1 BOOT  ",	"BOOT/BOOT.ELF\0"},
		{ 2, "HDLoader   ",	"BOOT/HDLOADER.ELF\0"},
		{ 3, "PS2LINK    ",	"BWLINUX/PS2LINK.ELF\0"},
		{ 4, "DEV1 boot  ",	"boot/boot.elf\0"},
		{ 5, "DEV1 boot1 ",	"boot/boot1.elf\0"},
		{ 6, "DEV1 boot2 ",	"boot/boot2.elf\0"},
		{ 7, "DEV1 boot3 ",	"boot/boot3.elf\0"}
	};

	u32 exit_option_size = 	sizeof( exit_option ) / sizeof( exit_option[0] );

	int exit_option_toggle = 999;

	//----------------------------------------------------------------------------

	// updateflag -> Aux for the OSD flow control
	//				-1: Keep into inner loop
	//				 0: Exit boot inner and outer loop and go to the launch method choosen
	//				 1: Exit inner loop and activate presets
	//				 2: Exit inner loop but don't update GS yet
	int updateflag;

	// Adaptation/fix flags (DisplayX, smode2 and syncv)
	int displayx_adapt_fix, smode2_fix, syncv_fix;

	// Current User Video Mode Slot Index (among the 16 available)
	int user_index=0;
	// Copy of CSR GS Register contents
	int local_CSR_copy;
	// Joy step on fine-tuning preset commands
	int joy_step;
	// Return value
	int retval=0;

	//Auxs for gsKit_fontm_printf_scaled macro
	char tempstr[128];
	
	int rownumber;
	
	int halfWidth;
	
	//---------- End of variables stuff ----------

	
	//----------------------------------------------------------------------------
	//---------- Start of coding stuff ----------

	/* Allocate memory to GSM structures */
	GSM = malloc(sizeof(struct gsm_settings));

	/* Allocate memory to gsKit structures */
	// Ex.: vartype variable1 *VARIABLE2 = NULL;
	//      VARIABLE2 = malloc(sizeof(vartype variable1));
	//gsGlobal = malloc(sizeof(GSGLOBAL));
	//gsFontM = malloc(sizeof(GSFONTM));
	
	/* Set GSM default settings for configureable items */
  	Set_Default_Settings();

	/* Initialise a lot of things */
	//SifResetIop();
	SifInitRpc(0);
	LoadModules();

	TimerInit();
	Timer();
	setupPad();
	TimerInit();
	Timer();
	unlink_GSM();
	KFun_install(ModdedSetGsCrt);
	KFun_install(DisplayHandler);
	set_kmode();
	DisplayHandler_p = KSEG_DisplayHandler_p;
	set_umode();
	disableClearUserMem();

	//----------------------------------------------------------------------------
	
GS_BGCOLOUR = 0xFFFFFF; // white
	/* CNF (Configuration File)	stuff */
	char CNF_filepath[] = "GSM/GSM.CNF";
	char CNF_body[] = "invalid CNF\r\n";
	int CNF_size = strlen(CNF_body);

	int CNF_File_io_ok = 0;
	if(!File_Exist(CNF_filepath)) { // Test if GSM.CNF is existing in GSM folder
		if(!Create_File(CNF_filepath, CNF_body, CNF_size)) {
			// if GSM.CNF was NOT created in GSM folder
			printf("Error in %s creation!\n",CNF_filepath);
		} else {
			printf("%s created successfully!\n",CNF_filepath);
			if(!saveConfig(CNF_filepath)) { // Test if defaults saved properly
				printf("But saving default CNF values failed!\n");
			} else {
				printf("And saving default CNF values worked.\n");
				CNF_File_io_ok = 1;
			}
		}
	} else
		printf("%s already created!\n",CNF_filepath);

	if(!loadConfig(CNF_filepath)) {
		printf("Error in %s opening!\n",CNF_filepath);
	} else
		printf("%s opened successfully!\n",CNF_filepath);
		CNF_File_io_ok = 1;
	//NB: Possible failure causes include wrong CNF version ID


GS_BGCOLOUR = 0x00FFFF; // yellow
	//----------------------------------------------------------------------------------------------------
	// I N I T I A L   V M O D E   ( A L W A Y S   A C T I V A T E D   B E F O R E   T H E   P A T C H E R
	//----------------------------------------------------------------------------------------------------

	//At principle we assume there is no vmode chosen
	mode = 0;

	//But if user already stored some vmode into CNF File slot 00, let's use it
	if((CNF_File_io_ok == 1) && (user_vmode[0].mode > 0)) {mode = user_vmode[0].mode;}

	//Also, let's give a chance for the VGA users avoid BSOD, if they press [TRIANGLE] on this moment
	TimerInit();
	WaitTime = Timer();
	while (1) 
	{
		if(Timer() > (WaitTime + 1000)) break;
	}
	retval = readpad_no_KB();
	if(paddata & PAD_TRIANGLE) {mode = GS_MODE_VGA_640_60;}

	// Let's Setup GS by the first time
	Setup_GS();

	// FontM Init - Make it once and here!!! Avoid avoid EE Exceptions (for instance, memory leak & overflow)
	gsFontM = gsKit_init_fontm();
	gsKit_fontm_upload(gsGlobal, gsFontM);
	gsFontM->Spacing = 0.95f;

	gsKit_clear(gsGlobal, Black);
	
	//----------------------------------------------------------------------------
	// P A T C H E R   I N S T A L L A T I O N   S T U F F
	//----------------------------------------------------------------------------

	//The initial presets are always taken from gsKit defaults
	interlace		=	gsGlobal->Interlace;
	mode			=	gsGlobal->Mode;
	field			=	gsGlobal->Field;
	gs_dx			=	gsGlobal->StartX;
	gs_dy			=	gsGlobal->StartY;
	gs_dw			=	gsGlobal->DW;
	gs_dh			=	gsGlobal->DH;
	gs_magh			=	gsGlobal->MagH;
	gs_magv			=	gsGlobal->MagV;
	display_presets	=	make_display_magic_number(gs_dh, gs_dw, gs_magv, gs_magh, gs_dy, gs_dx);
	syncv			=	0;
	smode2			=	(field<<1)|interlace;
	
	//But if CNF file slot 00 has an vmode stored into it, those previous ones will be overwritten
	if((CNF_File_io_ok ==1) && (user_vmode[0].mode > 0)) {	
		predef_vmode_toggle = 555;
		interlace		=	user_vmode[user_index].interlace;
		mode			=	user_vmode[user_index].mode;
		field			=	user_vmode[user_index].field;
		display_presets	=	user_vmode[user_index].display;
		gs_dx			=	(u32)((display_presets >> 00) & 0x0FFF);
		gs_dy			=	(u32)((display_presets >> 12) & 0x07FF);
		gs_magh			=	(u32)((display_presets >> 23) & 0x000F);
		gs_magv			=	(u32)((display_presets >> 27) & 0x0003);
		gs_dw			=	(u32)((display_presets >> 32) & 0x0FFF)+1;
		gs_dh			=	(u32)((display_presets >> 44) & 0x07FF)+1;
		syncv			=	user_vmode[user_index].syncv;
		smode2			=	user_vmode[user_index].smode2;
		if(smode2 == 0) { //if smode2 is zero (always for CNF from v0.22)
			smode2		=	(field<<1)|interlace;	//calculate smode2 form other parts
		}
	}

	//Fill the presets before enable and call the patcher by the first time
	act_interlace		=	interlace;
	act_mode			=	mode;
	act_field			=	field;
	display				=	display_presets;
	act_syncv			=	syncv;

	//Update the patcher params before enable and call the patcher by the first time
	UpdateModdedSetGsCrtDisplayHandlerParams(interlace, mode, field, display, syncv, smode2);

GS_BGCOLOUR = 0x007FFF; // Orange
	// Install patcher
	printf("Installing GS Mode Selector - \n");
	printf("Interlace %01d, Mode 0x%02X, Field %01d, DISPLAYx 0x%08lX, SYNCV 0x%08lX, SMODE2 0x%08lX\n", \
				interlace, mode, field, display, syncv, smode2);
	installGSModeSelector();
	patcher_enabled = 1;
		
	//----------------------------------------------------------------------------

	//If CNF file has an exit method stored into it, then use it!
	if((CNF_File_io_ok ==1) && (GSM->exit_option >= 0)) exit_option_toggle = 555;

	//----------------------------------------------------------------------------
	// S P L A S H   S C R E E N   S T U F F
	//----------------------------------------------------------------------------

	//  Splash Screen Stuff
	TimerInit();
	gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
	Setup_GS();
	Timer_delay(1);
	Clear_Screen();	
	Timer_delay(20);
	//Load_Splash();
	Clear_Screen();	
	Timer_delay(20);
	Draw_Screen();
	WaitTime = Timer();

	updateflag = 1; //Normal Mode (shows GSM OSD after its Splash Scren)
	while (1) 
	{
		if(Timer() > (WaitTime + 6000)) break;
		if(readpad(), new_pad) { // Skip Splash Screen if any key is pressed
			if(paddata & PAD_START) updateflag = 0;	// Use silent mode (exits from GSM without displaying its OSD) with [START] was pressed
			break;
		}
	}

	

	//----------------------------------------------------------------------------
	// M A I N   L O O P I N G   S T U F F
	//----------------------------------------------------------------------------

outer_loop_restart:
//---------- Start of outer while loop ----------
	while (!(updateflag == 0)) {

		display_presets = make_display_magic_number(gs_dh, gs_dw, gs_magv, gs_magh, gs_dy, gs_dx);

		if(updateflag == 1){
			act_interlace = interlace;
			act_mode = mode;
			act_field = field;
			display = display_presets;
			act_syncv = syncv;
			UpdateModdedSetGsCrtDisplayHandlerParams(interlace, mode, field, display, syncv, smode2);

			//NB: The GS vmode initialization needs to be duplicated to 'bite'

			gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
			Setup_GS();
			Timer_delay(20);
			gsKit_clear(gsGlobal, Black);
			Timer_delay(20);
			act_interlace = interlace;
			act_mode = mode;
			act_field = field;
			display = display_presets;
			act_syncv = syncv;
			UpdateModdedSetGsCrtDisplayHandlerParams(interlace, mode, field, display, syncv, smode2);

			gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
			Setup_GS();
			Timer_delay(20);
			gsKit_clear(gsGlobal, Black);
			Timer_delay(20);
			act_interlace = interlace;
			act_mode = mode;
			act_field = field;
			display = display_presets;
			act_syncv = syncv;
			UpdateModdedSetGsCrtDisplayHandlerParams(interlace, mode, field, display, syncv, smode2);

			gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
			Setup_GS();

			status_msg_p = "Presets activated!";

			act_height = 1 + (u16)((display >> 44) & 0x07FF);
			text_height = (26.0f * gsFontM->Spacing * 0.5f);
			edge_size = text_height;

		}//ends if
			
		set_kmode();
		displayx_adapt_fix = Adapt_Flag_1;
		smode2_fix = Adapt_Flag_2;
		syncv_fix = Adapt_Flag_3;
		display_requested = Safe_Orig_Display;
		display_calculated = Calc_Display;
		local_CSR_copy = GS_reg_CSR;
		set_umode();
			
		gsKit_clear(gsGlobal, Black);
				
	//----------------------------------------------------------------------------
	// O S D
	//----------------------------------------------------------------------------

		Some_Sprites();	// Draw some sprites

		//Title bar - Also serves as top edge of screen rectangle
		gsKit_prim_sprite(gsGlobal, \
			0, \
			0, \
			gsGlobal->Width, \
			(4 * text_height), \
			0, Blue);

		//Left and right screen rectangle edges
		gsKit_prim_sprite(gsGlobal, \
			0, \
			0, \
			edge_size-1, \
			act_height, \
			0, Blue);
		gsKit_prim_sprite(gsGlobal, \
			gsGlobal->Width - edge_size, \
			0, \
			gsGlobal->Width, \
			act_height, \
			0, Blue);

		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, 10, 1, 0.6f, YellowFont, TITLE);
		rownumber = 3;
		rownumber++;
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber++)*9, 1, 0.4f, DarkOrangeFont, \
			"%s - by %s", VERSION, AUTHORS);
		retval = GetROMVersion();
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber++)*10, 1, 0.4f, DeepSkyBlueFont, \
			"PS2 GS Revision Number:%u (ID %u)-ROM Version:%s", \
			(u8)( local_CSR_copy >> 16 & 0xFF ), \
			(u8)( local_CSR_copy >> 24 & 0xFF ), \
			ROMVER_data );

//NB: Status bar at bottom must be redrawn last, so the message gets a higher
//    priority than other stuff.

//NB: Status message display has been moved down, as mentioned above

		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, DeepSkyBlueFont, "ACTIVE SETTINGS - DISPLAYx 0x%08lX", display);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, OrangeRedFont, \
			"Interlace %01d, Mode 0x%02X, Field %01d, SYNCV 0x%08lX", \
			act_interlace, act_mode, act_field, act_syncv);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, OrangeRedFont, \
			"(DX=%04d,DY=%04d,MAGH=%02d,MAGV=%01d,DW=%04d,DH=%04d)", \
			(u16)((display >> 00) & 0x0FFF), \
			(u16)((display >> 12) & 0x07FF), \
			(u16)((display >> 23) & 0x000F), \
			(u16)((display >> 27) & 0x0003), \
			(u16)((display >> 32) & 0x0FFF), \
			(u16)((display >> 44) & 0x07FF));
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "PREDEF VMODES: "FONTM_CIRCLE" PAL/NTSC,"FONTM_SQUARE" HDTV,"FONTM_TRIANGLE" VGA,"FONTM_CROSS" NTSC/PAL(PS1)");
		if(predef_vmode_toggle == 999){
			gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "CURRENT: AUTO");}
		else if(predef_vmode_toggle == 555){
			gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "CURRENT: LOADED FROM CNF FILE SLOT 00");}
		else{
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, YellowFont, "CURRENT: %s", predef_vmode[predef_vmode_toggle].description);
		}
		rownumber++;
	
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, DeepSkyBlueFont, "CURRENT PRESETS - DISPLAYx 0x%08lX", display_presets);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, GreenFont, "Interlace %01d, Mode 0x%02X, Field %01d, SYNCV 0x%08lX", interlace, mode, field, syncv);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, GreenFont, "(DX=%04d,DY=%04d,MAGH=%02d,MAGV=%01d,DW=%04d,DH=%04d)", \
			(u16)((display_presets >> 00) & 0x0FFF), \
			(u16)((display_presets >> 12) & 0x07FF), \
			(u16)((display_presets >> 23) & 0x000F), \
			(u16)((display_presets >> 27) & 0x0003), \
			(u16)((display_presets >> 32) & 0x0FFF), \
			(u16)((display_presets >> 44) & 0x07FF));
		rownumber++;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "\efdown Activate Presets");   
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[Left analog stick] DX and DY");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[Right analog stick] DW and DH");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "   Step Combos: +[L1]Step 1 +[none]Step 4 +[R1]Step 16");
		rownumber++;
	
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size,  (++rownumber)*11, 1, 0.4f, DeepSkyBlueFont, "FIXES");
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[L1]+[R1]+"FONTM_CROSS" DISPLAYx Adaption %s", on_off_s[displayx_adapt_fix]);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[L1]+[R1]+"FONTM_CIRCLE" Separate  SMODE2 %s", on_off_s[smode2_fix]);
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[L1]+[R1]+"FONTM_TRIANGLE" Separate  SYNCV %s", on_off_s[syncv_fix]);
		rownumber++;
	
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, DeepSkyBlueFont, "CNF FILE");
		gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont,        "         - Current User Slot:%02d(Base vmode:      )", user_index);
		if(user_vmode[user_index].mode == 0){
			gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont, "                                           unused");
		}
		else{
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont,"                                           0x%02X", user_vmode[user_index].mode);
		}
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "                              \efup Save Slot to RAM");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "             Previous Slot \efleft [R2] \efright Next Slot");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "                              \efdown Load Slot from RAM");
		rownumber++;

		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, DeepSkyBlueFont, "EXIT OPTIONS:");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, WhiteFont,         "                \efup Switch among exit options available");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont,       "             [START]");
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont,       "                \efdown Exit to");
		if(exit_option_toggle == 999){
			gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont,    "                          NONE");
		}
		else if(exit_option_toggle == 555){
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont,   "                          %s(LOADED FROM CNF FILE)", exit_option[GSM->exit_option].description);
		}
		else{
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, edge_size, (rownumber)*11, 1, 0.4f, YellowFont,   "                          %s", exit_option[exit_option_toggle].description);
		}
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, edge_size, (++rownumber)*11, 1, 0.4f, WhiteFont, "[L2]+[R2]Save all slots + current 'Exit to' option to mc");
	
		//Status bar - Also serves as bottom edge of GUI rectangle
		//It must be redrawn after all other stuff, so  an urgent message can be
		//displayed even in a vmode with a low resolution.
		gsKit_prim_sprite(gsGlobal, \
			0, \
			(act_height - text_height), \
			gsGlobal->Width, \
			act_height, \
			0, Blue);

		//Status message - Drawn on top of bottom edge of GUI rectangle
		//It must be redrawn after all other stuff, so  an urgent message can be
		//displayed even in a vmode with a low resolution.
		if(status_msg_p != NULL){
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, \
				0,
				(act_height - text_height), \
				1, 0.5f, YellowFont, "%s", status_msg_p);
			status_msg_p = NULL;
		}

		Draw_Screen();

		updateflag = -1;

					
	//----------------------------------------------------------------------------
	// P A D   S T U F F
	//----------------------------------------------------------------------------

		//---------- Start of inner while loop ----------
		while (updateflag == -1) {

			delay(3);
			while(!(waitAnyPadReady(), readpad(), new_pad)); //await a new button
			retval = paddata;
			
			if(paddata & PAD_JOY){
				if(paddata & PAD_L1)
					joy_step = 1;
				else if(paddata & PAD_R1)
					joy_step = 16;
				else
					joy_step = 4;
			}else
				joy_step = 0;

			if(paddata & (PAD_L3_H0 | PAD_L3_H1))	{
				gs_dx += (1-2*(!(paddata & PAD_L3_H1)))*joy_step;
				gs_dx &= (1<<12)-1;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if(paddata & (PAD_L3_V0 | PAD_L3_V1))	{
				gs_dy += (1-2*(!(paddata & PAD_L3_V1)))*joy_step;
				gs_dy &= (1<<11)-1;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if(paddata & (PAD_R3_H0 | PAD_R3_H1))	{
				gs_dw += (1-2*(!(paddata & PAD_R3_H1)))*joy_step;
				gs_dw &= (1<<12)-1;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if(paddata & (PAD_R3_V0 | PAD_R3_V1))	{
				gs_dh += (1-2*(!(paddata & PAD_R3_V1)))*joy_step;
				gs_dh &= (1<<11)-1;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if((retval & PAD_L1)&&(retval & PAD_R1)&&(retval & PAD_CROSS))	{
				set_kmode();
				Adapt_Flag_1 ^= 1;			//Toggle "DISPLAYx Adaption" flag ON/OFF
  			set_umode();
				updateflag = 1;
			}
			else if((retval & PAD_L1)&&(retval & PAD_R1)&&(retval & PAD_CIRCLE))	{
				set_kmode();
				Adapt_Flag_2 ^= 1;			//Toggle "Separate SMODE2" flag ON/OFF
  			set_umode();
				updateflag = 1;
			}
			else if((retval & PAD_L1)&&(retval & PAD_R1)&&(retval & PAD_TRIANGLE))	{
				set_kmode();
				Adapt_Flag_3 ^= 1;			//Toggle "Separate SYNCV" flag ON/OFF
  			set_umode();
				updateflag = 1;
			}
			else if((retval & PAD_LEFT)&&(retval & PAD_R2))	{
				user_index = (user_index - 1) & (user_vmode_slots-1);
				status_msg_p = "Moved to the previous slot";
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if((retval & PAD_RIGHT)&&(retval & PAD_R2))	{
				user_index = (user_index + 1) & (user_vmode_slots-1);
				status_msg_p = "Moved to the next slot";
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if((retval & PAD_UP)&&(retval & PAD_R2))	{
				user_vmode[user_index].interlace = interlace;
				user_vmode[user_index].mode = mode;
				user_vmode[user_index].field = field;
				user_vmode[user_index].display = display_presets;
				user_vmode[user_index].syncv = syncv;
				user_vmode[user_index].smode2 = smode2;
				status_msg_p = "Current User Slot saved to RAM successfully";
				updateflag = 2;
			}
			else if((retval & PAD_DOWN)&&(retval & PAD_R2))	{
				if(user_vmode[user_index].mode > 0) {
					interlace = user_vmode[user_index].interlace;
					mode = user_vmode[user_index].mode;
					field = user_vmode[user_index].field;
					display_presets = user_vmode[user_index].display;
					syncv = user_vmode[user_index].syncv;
					smode2 = user_vmode[user_index].smode2;
					if(smode2 == 0) { //if smode2 is zero (always for CNF from v0.22)
						smode2 = (field<<1)|interlace;	//calculate smode2 form other parts
					}
					gs_dx		=	(u32)((display_presets >> 00) & 0x0FFF);
					gs_dy		=	(u32)((display_presets >> 12) & 0x07FF);
					gs_magh	=	(u32)((display_presets >> 23) & 0x000F);
					gs_magv	=	(u32)((display_presets >> 27) & 0x0003);
					gs_dw		=	(u32)((display_presets >> 32) & 0x0FFF)+1;
					gs_dh		=	(u32)((display_presets >> 44) & 0x07FF)+1;
					status_msg_p = "Presets restored from RAM successfully";
					updateflag = 2;
				}
			}
			else if((retval & PAD_L2)&&(retval & PAD_R2))	{
				if((exit_option_toggle >= 0)&&(exit_option_toggle <= (exit_option_size - 1)))
				GSM->exit_option = exit_option_toggle;
				if(saveConfig(CNF_filepath))
					status_msg_p = "CNF File saved successfully";
				else
					status_msg_p = "CNF saving failed";
				updateflag = 2;
			}
			else if((retval & PAD_UP)&&(retval & PAD_START))	{
				exit_option_toggle++;
				if(exit_option_toggle > (exit_option_size - 1)) exit_option_toggle = 0;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if((retval & PAD_DOWN)&&(retval & PAD_START))	{
				updateflag = 0;
			}
			//all button combo command tests should be above this point
			//all single button command tests should be below this point
			else if(retval == PAD_TRIANGLE)	{ //VGA mode toggler command
				if (predef_vmode[predef_vmode_toggle].category != VGA_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != VGA_VMODE);
				interlace = predef_vmode[predef_vmode_toggle].interlace; 
				mode      = predef_vmode[predef_vmode_toggle].mode;
				field     = predef_vmode[predef_vmode_toggle].field;
				
				display_presets  = predef_vmode[predef_vmode_toggle].display;
				gs_dx =   (u16)((display_presets >> 00) & 0x0FFF);
				gs_dy =   (u16)((display_presets >> 12) & 0x07FF);
				gs_magh = (u16)((display_presets >> 23) & 0x000F);
				gs_magv = (u16)((display_presets >> 27) & 0x0003);
				gs_dw =   (u16)((display_presets >> 32) & 0x0FFF) + 1;
				gs_dh =   (u16)((display_presets >> 44) & 0x07FF) + 1;
				
				syncv     = predef_vmode[predef_vmode_toggle].syncv;
				smode2 = (field<<1)|interlace;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}	
			else if(retval == PAD_SQUARE){ //HDTV mode toggler command
				if (predef_vmode[predef_vmode_toggle].category != HDTV_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != HDTV_VMODE);
				interlace = predef_vmode[predef_vmode_toggle].interlace; 
				mode      = predef_vmode[predef_vmode_toggle].mode;
				field     = predef_vmode[predef_vmode_toggle].field;
				
				display_presets  = predef_vmode[predef_vmode_toggle].display;
				gs_dx =   (u16)((display_presets >> 00) & 0x0FFF);
				gs_dy =   (u16)((display_presets >> 12) & 0x07FF);
				gs_magh = (u16)((display_presets >> 23) & 0x000F);
				gs_magv = (u16)((display_presets >> 27) & 0x0003);
				gs_dw =   (u16)((display_presets >> 32) & 0x0FFF) + 1;
				gs_dh =   (u16)((display_presets >> 44) & 0x07FF) + 1;
				
				syncv     = predef_vmode[predef_vmode_toggle].syncv;
				smode2 = (field<<1)|interlace;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}	
			else if(retval == PAD_CIRCLE)	{ //PS2 PAL/NTSC mode toggler command
				if (predef_vmode[predef_vmode_toggle].category != SDTV_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != SDTV_VMODE);
				interlace = predef_vmode[predef_vmode_toggle].interlace; 
				mode      = predef_vmode[predef_vmode_toggle].mode;
				field     = predef_vmode[predef_vmode_toggle].field;
				
				display_presets  = predef_vmode[predef_vmode_toggle].display;
				gs_dx =   (u16)((display_presets >> 00) & 0x0FFF);
				gs_dy =   (u16)((display_presets >> 12) & 0x07FF);
				gs_magh = (u16)((display_presets >> 23) & 0x000F);
				gs_magv = (u16)((display_presets >> 27) & 0x0003);
				gs_dw =   (u16)((display_presets >> 32) & 0x0FFF) + 1;
				gs_dh =   (u16)((display_presets >> 44) & 0x07FF) + 1;
				
				syncv     = predef_vmode[predef_vmode_toggle].syncv;
				smode2 = (field<<1)|interlace;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}	
			else if(retval == PAD_CROSS)	{ //PS1 PAL/NTSC mode toggler command
				if (predef_vmode[predef_vmode_toggle].category != PS1_VMODE) predef_vmode_toggle = -1;
				do
				{
					predef_vmode_toggle++;
					if(predef_vmode_toggle > (predef_vmode_size - 1)) predef_vmode_toggle = 0;
				}while (predef_vmode[predef_vmode_toggle].category != PS1_VMODE);
				interlace = predef_vmode[predef_vmode_toggle].interlace; 
				mode      = predef_vmode[predef_vmode_toggle].mode;
				field     = predef_vmode[predef_vmode_toggle].field;
				
				display_presets  = predef_vmode[predef_vmode_toggle].display;
				gs_dx =   (u16)((display_presets >> 00) & 0x0FFF);
				gs_dy =   (u16)((display_presets >> 12) & 0x07FF);
				gs_magh = (u16)((display_presets >> 23) & 0x000F);
				gs_magv = (u16)((display_presets >> 27) & 0x0003);
				gs_dw =   (u16)((display_presets >> 32) & 0x0FFF) + 1;
				gs_dh =   (u16)((display_presets >> 44) & 0x07FF) + 1;
				
				syncv     = predef_vmode[predef_vmode_toggle].syncv;
				smode2 = (field<<1)|interlace;
				updateflag = 2; //Exit inner loop but don't update GS yet
			}
			else if(retval == PAD_DOWN)	{
				updateflag = 1; //exit inner loop and activate presets
			}
			
		}	//---------- End of inner while loop ----------
	}	//---------- End of outer while loop ----------

	//----------------------------------------------------------------------------
	// S C R E E N S H O T   S T U F F
	//----------------------------------------------------------------------------
/*
	//---------- Start of Screenshot stuff ----------
	char* name = "host:GSM.tga";
	printf("Screenshoting to %s file...", name);
	ps2_screenshot_file(name, 0, gsGlobal->Width, gsGlobal->Height, GS_PSM_CT32);
	printf("done!\n");
	//---------- End of Screenshot stuff ----------
*/

	//----------------------------------------------------------------------------
	// E X I T   O P T I O N S   S T U F F
	//----------------------------------------------------------------------------
	updateflag = 2; // This statement makes GSM coming back to the outer loop without updating GS If something goes wrong!
	halfWidth = gsGlobal->Width / 2;
	if(exit_option_toggle == 999){	//None exit method chosen yet
		gsKit_clear(gsGlobal, Black);
		gsFontM->Align = GSKIT_FALIGN_CENTER;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, RedFont, "None exit method chosen yet!");  // Failed to run ELF
		gsFontM->Align = GSKIT_FALIGN_LEFT;
		Draw_Screen();
		delay(10);
		goto outer_loop_restart;
	}
	if(exit_option_toggle == 555){	//Exit method loaded from CNF FILE
		exit_option_toggle = GSM->exit_option;
	}
	sprintf(tempstr, "%s", exit_option[exit_option_toggle].elf_path);
	strcpy(elf_path, tempstr);
	
	if(exit_option_toggle == 0) {
		printf("Exiting to PS2 BROWSER...\n");
		gsKit_clear(gsGlobal, Black);
		gsFontM->Align = GSKIT_FALIGN_CENTER;
		gsKit_fontm_print_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Exiting to PS2 BROWSER...");
		gsFontM->Align = GSKIT_FALIGN_LEFT;
		Draw_Screen();
		delay(10);
		gsKit_vram_clear(gsGlobal);
		gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
		CleanUp();

		__asm__ __volatile__(
		".set noreorder\n"
		"li $3, 0x04\n"
		"syscall\n"
		"nop\n"
		".set reorder\n"
		);
	   	SleepThread();					// Should never get here
   		return 0;
	}
	else{
		if(File_Exist(elf_path)){
		//if(File_Exist("BWLINUX/PS2LINK.ELF\0")){
			char elf_path2[0x40];
			sprintf(elf_path2, "mc0:%s", elf_path);
			elf_path2[2] += MC_index;
			printf("Loading %s...\n", elf_path2);
			gsKit_clear(gsGlobal, Black);
			gsFontM->Align = GSKIT_FALIGN_CENTER;
			gsKit_fontm_printf_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, DeepSkyBlueFont, "Loading %s...", elf_path2);
			gsFontM->Align = GSKIT_FALIGN_LEFT;
			Draw_Screen();
			delay(10);
			gsKit_vram_clear(gsGlobal);
			gsKit_deinit_global(gsGlobal); // Free all memory allocated by gsGlobal structures
			CleanUp();
			RunLoaderElf(elf_path2, party);	// Run ELF
			//RunLoaderElf("mc0:BWLINUX/PS2LINK.ELF\0",party);
	   		SleepThread();					// Should never get here
   			return 0;
		} else {
				printf("Can't run %s...\n", elf_path);
				gsKit_clear(gsGlobal, Black);
				gsFontM->Align = GSKIT_FALIGN_CENTER;
				gsKit_fontm_printf_scaled(gsGlobal, gsFontM, halfWidth, 210, 1, 0.6f, RedFont, "Can't run %s...", elf_path);  // Failed to run ELF
				gsFontM->Align = GSKIT_FALIGN_LEFT;
				Draw_Screen();
				delay(10);
				goto outer_loop_restart;
		}
	}
	printf("Should never get here\n");
	SleepThread();					// Should never get here
	return 0;
}

// End of program, leave a blank line afterwards to let 'make' line-command EE-GCC (Emotion Engine - Gnu Compiler Collection) happy ;-)
