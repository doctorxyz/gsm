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

