//----- Start of code section for vertical fields of DISPLAYx -------
// t4:PH1:MAGV2  t5:Max_height-1 t6:Max_height:DY2 t7:DH2 v0:result_acc
//---------------------------------------------------------------------
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | MAGV | 28:27 | int 0: 2:0 | magnification in vertical direction   | 0x3   |
// '------^-------^------------^---------------------------------------^-------^
	"dsrl $t0,$a1,27\n\t"       //t0=request value aligned for MAGV
	"andi $t0,$t0,0x03\n\t"     //t0=MAGV1 masked
	"addi $t0,$t0,1\n\t"        //t0=YScale1 = MAGV1+1
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | DH   | 54:44 | int 0:11:0 | display area height - 1 (pixel units) | 0x7FF |
// '------^-------^------------^---------------------------------------^-------^
	"dsrl32	$t1,$a1,12\n\t"     //t1=request value aligned for DH
	"andi	$t1,$t1,0x07FF\n\t"   //t1=DH1 masked
	"addi	$t1,$t1,1\n\t"        //t1=(DH1+1) = PH1
	"divu $zero,$t1,$t0\n\t"    //LO=PH1 = (DH1+1)/YScale1
	"dsrl32 $t0,$v1,12\n\t"     //t0=forcing template aligned for DH
	"andi $t5,$t0,0x07FF\n\t"   //t5=Max_Height-1 masked
	"mflo	$t4\n\t"              //t4=PH1
	"addi $t6,$t5,1\n\t"        //t6=Max_Height
	"nop\n\t"
	"divu	$zero,$t6,$t4\n\t"    //LO=YScale2 = Max_Height/PH1
	"nop\n\t"
	"nop\n\t"
	"mflo $t0\n\t"              //t0=YScale2
	"bne $t0,$zero,21f\n\t"     //if(!YScale2) //YScale2 invalid
	"nop\n\t"                   //{
	"or $t7,$zero,$t5\n\t"      //	t7=DH2 = Max_Height-1
	"sub $t0,$t6,$t4\n\t"       //	t0=(Max_Height-PH1)
	"li $t4,0\n\t"              //	t4=MAGV2 = 0
	"beq $zero,$zero,23f\n\t"		//}
	"nop\n\t"                   //else //YScale2 non-zero
"21:\n\t"                     //{
	"addi $t1,$t0,-4\n\t"       //	t1=YScale2-4
	"bgtzl $t1,22f\n\t"         //	if(t1>0)
	"or $t0,$zero,4\n\t"        //		t0=YScale2=4;
"22:\n\t"
	"mult $t4,$t0\n\t"          //	LO=(PH1*YScale2)
	"nop\n\t"
	"nop\n\t"
	"mflo $t1\n\t"              //	t1=(PH1*YScale2)
	"addi $t7,$t1,-1\n\t"       //	t7=DH2 = (PH1*YScale2)-1

	"addi $t4,$t0,-1\n\t"       //	t4=MAGV2 = YScale2-1
	"sub $t0,$t5,$t7\n\t"       //	t0=(Max_Height-1-DH2)
"23:\n\t"                     //}
	"dsra $t0,$t0,1\n\t"        //t0=t0/2 = half_excess_height (can be negative)
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | DY   | 22:12 | int 0:11:0 | y pos in display area (raster units)  | 0x7FF |
// '------^-------^------------^---------------------------------------^-------^
	"dsrl	$t1,$v1,12\n\t"       //t1=forcing template aligned for DY
	"andi $t1,$t1,0x07FF\n\t"   //t1=Min_DY masked
	"add $t6,$t0,$t1\n\t"       //t6=DY2
	"bltzl $t6,24f\n\t"         //if(DY2<0)
	"and $t6,$t6,$zero\n\t"     //	DY2=0;
"24:\n\t"
	"sub $t0,$t1,$t6\n\t"       //t0=Min_DY-DY2 
	"bgtzl $t0,25f\n\t"         //if(Min_DY > DY2)
	"add $t7,$t7,$t0\n\t"       //	t7 += Min_DY-DY2  //DH2 adjusted
"25:\n\t"

	"lb		$t0,rel_Adapt_DoubleHeight($a2)\n\t"	//if doubled height not needed
	"beq	$t0,$zero,26f\n\t"										//	Calculation is complete
	"ld		$t0,rel_Target_SMode2($a2)\n\t"
	"andi	$t0,$t0,1\n\t"												//if target mode interlaced
	"bne	$t0,$zero,26f\n\t"										//	Calculation is complete
	"nop\n\t"
	"beql	$t4,$zero,26f\n\t"		//if MAGV==0
	"addi	$t4,$t4,1\n\t"				//	go use MAGV += 1
	"addi $t4,$t4,2\n\t"				//MAGV += 2 (Because scale was 2 or larger)
	"addi $t0,$t4,-4\n\t"				//Compare MAGV with 4 (too large ?)
	"bgezl	$t0,26f\n\t"				//if MAGV too large
	"ori	$t4,$zero,3\n\t"			//	go use MAGV = 3
"26:\n\t"

	"andi $t7,$t7,0x07FF\n\t"   //t7=DH2 masked
	"andi $t4,$t4,0x0003\n\t"   //t4=MAGV2 masked
	"andi $t6,$t6,0x07FF\n\t"   //t6=DY2 masked
	"dsll32 $t0,$t7,12\n\t"     //t0=DH2 aligned for DISPLAYx
	"or $v0,$v0,$t0\n\t"        //Accumulate DH field to DISPLAYx value in v0
	"dsll $t0,$t4,27\n\t"       //t0=MAGV2 aligned for DISPLAYx
	"or $v0,$v0,$t0\n\t"        //Accumulate MAGV field to DISPLAYx value in v0
	"dsll $t0,$t6,12\n\t"       //t0=DY2 aligned for DISPLAYx
	"or $v0,$v0,$t0\n\t"        //Accumulate DY field to DISPLAYx value in v0
//----- End of code section for vertical fields of DISPLAYx -----
