/*
#
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
#
*/

#include <syscallnr.h>

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

/*------------------------------------------------------------------*/
/* Replace SetGsCrt in kernel. (Graphics Synthesizer Mode Selector) */
/*------------------------------------------------------------------*/
void Install_Hook_SetGsCrt(void)
{

	ee_kmode_enter();
	if (GetSyscallHandler(__NR_SetGsCrt) != &Hook_SetGsCrt) {
		Old_SetGsCrt = GetSyscallHandler(__NR_SetGsCrt);
		SetSyscall(__NR_SetGsCrt, Hook_SetGsCrt);
	}
	ee_kmode_exit();

}

/*-----------------------------------------------------------------*/
/* Restore original SetGsCrt. (Graphics Synthesizer Mode Selector) */
/*-----------------------------------------------------------------*/
void Remove_Hook_SetGsCrt(void)
{

	ee_kmode_enter();
	if (GetSyscallHandler(__NR_SetGsCrt) == &Hook_SetGsCrt) {
		SetSyscall(__NR_SetGsCrt, Old_SetGsCrt);
	}
	ee_kmode_exit();

}

/*----------------------------------------------------------------------------------------------------*/
/* Install Display Handler in place of the Core Debug Exception Handler (V_DEBUG handler)             */
/* Exception Vector Address for Debug Level 2 Exception when Stadus.DEV bit is 0 (normal): 0x80000100 */
/* 'Level 2' is a generalization of Error Level (from previous MIPS processors)                       */
/* When this exception is recognized, control is transferred to the applicable service routine;       */
/* in our case the service routine is 'GSHandler'!                                                    */
/*----------------------------------------------------------------------------------------------------*/
void Install_GSHandler(void)
{
	ee_kmode_enter();
	*(volatile u32 *)0x80000100 = MAKE_J((int)GSHandler);
	*(volatile u32 *)0x80000104 = 0;
	__asm__ __volatile__ (
	"sync.l\n"						// Wait until the preceding loads are completed
	"sync.p\n"						//  Await instruction completion
	);
	ee_kmode_exit();

/*
	"li $a0, 0x12000000\n"	// Address base for trapping
	"li $a1, 0x1FFFFF1F\n"	// Address mask for trapping
	//We trap writes to 0x12000000 + 0x00,0x20,0x40,0x60,0x80,0xA0,0xC0,0xE0
	//We only want 0x20, 0x60, 0x80, 0xA0, but can't mask for that combination
	//But the trapping range is now extended to match all kernel access segments
*/

	// Set Data Address Write Breakpoint
	// Trap writes to GS registers, so as to control their values
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"
	
	"li $a0, 0x12000000\n"	// Address base for trapping
	"li $a1, 0x1FFFFE1F\n"	// Address mask for trapping	//DOCTORXYZ
	//We trap writes to 0x12000000 + 0x00,0x20,0x40,0x60,0x80,0xA0,0xC0,0xE0,0x100,0x120,0x140,0x160,0x180,0x1A0,0x1C0,0x1E0	//DOCTORXYZ
	//We only want 0x20, 0x60, 0x80, 0xA0, 0x100, but can't mask for that combination //DOCTORXYZ
	//But the trapping range is now extended to match all kernel access segments

	"di\n"									// Disable Interupts
	"sync.l\n"								// Wait until the preceding loads are completed

	"li $k0, 0x8000\n"
	"mtbpc $k0\n"			// All breakpoints off (BED = 1)

	"sync.p\n"						// Await instruction completion

	"mtdab	$a0\n"
	"mtdabm	$a1\n"

	"sync.p\n"						// Await instruction completion

	"mfbpc $k1\n"
	"sync.p\n"						// Await instruction completion

	"li $k0, 0x20200000\n"			// Data write breakpoint on (DWE, DUE = 1)
	"or $k1, $k1, $k0\n"
	"xori $k1, $k1, 0x8000\n"		// DEBUG exception trigger on (BED = 0)
	"mtbpc $k1\n"

	"sync.p\n"						//  Await instruction completion

	"ei\n"						// Enable Interupts
	"nop\n"
	
	".set at\n"
	".set reorder\n"
	);
}


void Remove_GSHandler(void)
{
	__asm__ __volatile__ (
	".set noreorder\n"
	".set noat\n"
	
	"di\n"									// Disable Interupts
	"sync.l\n"								// Wait until the preceding loads are completed

	"li $k0, 0x8000\n"
	"mtbpc $k0\n"			// All breakpoints off (BED = 1)

	"sync.p\n"						// Await instruction completion

	"ei\n"						// Enable Interupts
	"nop\n"
	
	".set at\n"
	".set reorder\n"
	);
}


/*--------------------------------------------*/
/* Disable Graphics Synthesizer Mode Selector */
/*--------------------------------------------*/
void EnableGSM()
{
	// Install Hook SetGsCrt
	Install_Hook_SetGsCrt();
	// Install Display Handler
	Install_GSHandler();
}

/*--------------------------------------------*/
/* Disable Graphics Synthesizer Mode Selector */
/*--------------------------------------------*/

void DisableGSM()
{
	// Remove Hook SetGsCrt
	Remove_Hook_SetGsCrt();
	// Remove Display Handler
	Remove_GSHandler();
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

