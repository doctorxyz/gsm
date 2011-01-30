//----- Start of code section for horizontal fields of DISPLAYx -----
// t4:PW:MAGH2  t5:Max_Width-1 t6:Max_Width:DX2 t7:DW2 v0:result_acc
//-------------------------------------------------------------------
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | MAGH | 26:23 | int 0: 4:0 | magnification in horizontal direction | 0xF   |
// '------^-------^------------^---------------------------------------^-------^
	"dsrl $t0,$a1,23\n\t"       //t0=request value aligned for MAGH
	"andi $t0,$t0,0x0F\n\t"     //t0=MAGH1 masked
	"addi $t0,$t0,1\n\t"        //t0=XScale1 = MAGH1+1
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | DW   | 43:32 | int 0:12:0 | display area width - 1 (VCK units)    | 0xFFF |
// '------^-------^------------^---------------------------------------^-------^
	"dsrl32	$t1,$a1,0\n\t"      //t1=request value aligned for DW
	"andi	$t1,$t1,0x0FFF\n\t"   //t1=DW1 masked
	"addi	$t1,$t1,1\n\t"        //t1=(DW1+1)
	"divu $zero,$t1,$t0\n\t"    //LO=PW1 = (DW1+1)/XScale
	"dsrl32 $t0,$v1,0\n\t"      //t0=forcing template aligned for DW1
	"andi $t5,$t0,0x0FFF\n\t"   //t5=Max_Width-1 masked
	"mflo	$t4\n\t"              //t4=PW
	"addi $t6,$t5,1\n\t"         //t6=Max_Width
	"nop\n\t"
	"divu	$zero,$t6,$t4\n\t"    //LO=XScale2 = Max_Width/PW
	"nop\n\t"
	"nop\n\t"
	"mflo $t0\n\t"              //t0=XScale2
	"bne $t0,$zero,11f\n\t"     //if(!XScale2) //XScale2 invalid zero
	"nop\n\t"                   //{
	"or $t7,$zero,$t5\n\t"      //	t7=DW2 = Max_Width-1
	"sub $t0,$t6,$t4\n\t"       //	t0=(Max_Width-PW)
	"li $t4,0\n\t"              //	t4=MAGH2 = 0
	"beq $zero,$zero,13f\n\t"		//}
	"nop\n\t"                   //else //XScale2 nonzero
"11:\n\t"                     //{
	"addi $t1,$t0,-16\n\t"      //	t1=XScale2-16
	"bgtzl $t1,12f\n\t"         //	if(t1>0)
	"or $t0,$zero,16\n\t"       //		t0=XScale2=16;
"12:\n\t"
	"mult $t4,$t0\n\t"          //	LO=(PW*XScale2)
	"nop\n\t"
	"nop\n\t"
	"mflo $t1\n\t"              //	t1=(PW*XScale2)
	"addi $t7,$t1,-1\n\t"       //	t7=DW2 = (PW*XScale2)-1
	"addi $t4,$t0,-1\n\t"       //	t4=MAGH2 = XScale2-1
	"sub $t0,$t5,$t7\n\t"       //	t0=(Max_Width-1-DW2)
"13:\n\t"                     //}
	"dsra $t0,$t0,1\n\t"        //t0=t0/2 = half_excess_width (can be negative)
// .------.-------.------------.---------------------------------------.-------.
// | Name | Pos.  | Format     | Contents                              | Mask  |
// |      |       |            |                                       |       |
// |------+-------+------------+---------------------------------------+-------+
// | DX   | 11:0  | int 0:12:0 | x pos in display area (VCK units)     | 0xFFF |
// '------^-------^------------^---------------------------------------^-------^
	"andi $t1,$v1,0x0FFF\n\t"   //t1=Min_DX masked
	"add $t6,$t0,$t1\n\t"       //t6=DX2
	"bltzl $t6,14f\n\t"         //if(DX2<0)
	"and $t6,$t6,$zero\n\t"     //	DX2=0;
"14:\n\t"
	"sub $t0,$t1,$t6\n\t"       //t0=Min_DX-DX2 
	"bgtzl $t0,15f\n\t"         //if(Min_DX > DX2)
	"add $t7,$t7,$t0\n\t"       //	t7 += Min_DX-DX2  //DW2 adjusted
"15:\n\t"
	"andi $t7,$t7,0x0FFF\n\t"   //t7=DW2 masked
	"andi $t4,$t4,0x000F\n\t"   //t4=MAGH2 masked
	"andi $t6,$t6,0x0FFF\n\t"   //t6=DX2 masked
	"dsll32 $t0,$t7,0\n\t"      //t0=DW2 aligned for DISPLAYx
	"or $v0,$v0,$t0\n\t"        //Accumulate DW field to DISPLAYx value in v0
	"dsll $t0,$t4,23\n\t"       //t0=MAGH2 aligned for DISPLAYx
	"or $v0,$v0,$t0\n\t"        //Accumulate MAGH field to DISPLAYx value in v0
	"or $v0,$v0,$t6\n\t"        //Accumulate DX field to DISPLAYx value in v0
//----- End of code section for horizontal fields of DISPLAYx -----
