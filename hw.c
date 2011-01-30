//static unsigned int VRendf = 0;
//static unsigned int VRendid = 0;
static unsigned int VRcount = 0;
//static unsigned int VRstartid = 0;

// ----------------------------
// ----------------------------


void VRstart_handler()
{
	VRcount++;
	return;
}

// clears flag and waits until it gets reset (blocking call)
// numvrs = number of vertical retraces to wait for
void WaitForNextVRstart(int numvrs)
{
	VRcount=0;

	while (VRcount<numvrs);

	return;
}

// has start-of-Vertical-Retrace occurred since the flag was last cleared ?
// (non-blocking call)
// int TestVRstart();
int TestVRstart()
{
	return VRcount;
}


// clear the start-of-Vertical-Retrace flag
// void ClearVRcount();
void ClearVRcount()
{
    VRcount=0;
	return;
}


// ----------------------------
// ----------------------------

// DMA stuff


// Duke's DmaReset !
void DmaReset()
{
	__asm__("	sw  $0, 0x1000a080");
	__asm__("	sw  $0, 0x1000a000");
	__asm__("	sw  $0, 0x1000a030");
	__asm__("	sw  $0, 0x1000a010");
	__asm__("	sw  $0, 0x1000a050");
	__asm__("	sw  $0, 0x1000a040");
	__asm__("	li  $2, 0xff1f");
	__asm__("	sw  $2, 0x1000e010");
	__asm__("	sw  $0, 0x1000e000");
	__asm__("	sw  $0, 0x1000e020");
	__asm__("	sw  $0, 0x1000e030");
	__asm__("	sw  $0, 0x1000e050");
	__asm__("	sw  $0, 0x1000e040");
	__asm__("	lw  $2, 0x1000e000");
	__asm__("	ori $3,$2,1");
	__asm__("	nop");
	__asm__("	sw  $3, 0x1000e000");
	__asm__("	nop");

	return;
};


// the same as Duke's "SendPrim"
void SendDma02(void* DmaTag)
{
	
	__asm__("	li $3, 0x1000a000");	__asm__("	sw $4, 0x0030($3)");
	__asm__("	sw $0, 0x0020($3)");
	__asm__("	lw $2, 0x0000($3)");
	__asm__("	ori $2, 0x0105");
	__asm__("	sw $2, 0x0000($3)");

	return;
}

// Duke's Dma02Wait !
void Dma02Wait()
{
	__asm__("	addiu $29, -4");
	__asm__("	sw $8, 0($29)");

	__asm__("Dma02Wait.poll:");
	__asm__("	lw $8, 0x1000a000");
	__asm__("	nop");
	__asm__("	andi $8, $8, 0x0100");
	__asm__("	bnez $8, Dma02Wait.poll");
	__asm__("	nop");

	__asm__("	lw $8, 0($29)");
	__asm__("	addiu $29, 4");

	return;
}
