/*
#
# Graphics Synthesizer Mode Selector (a.k.a. GSM) - Force (set and keep) a GS Mode, then load & exec a PS2 ELF
#-------------------------------------------------------------------------------------------------------------
# Copyright 2009, 2010, 2011 doctorxyz & dlanor
# Licenced under Academic Free License version 2.0
# Review LICENSE file for further details.
#
*/

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

	/* Initialise a lot of things */
	SifInitRpc(0);
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

	// Install and enable GSM
	EnableGSM();

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
