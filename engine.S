#
# Copyright 2009-2010, doctorxyz & dlanor
# Licenced under Academic Free License version 3.0
# Review GSM README & LICENSE files for further details.
#

#include <ee_cop0_defs.h>
#include <syscallnr.h>

KSEG=0x00080000
GS_BASE=0x12000000
GS_SMODE2=GS_BASE+0x0020
GS_SYNCV=GS_BASE+0x0060
GS_DISPLAY1=GS_BASE+0x0080
GS_DISPLAY2=GS_BASE+0x00A0
GS_BGCOLOUR=GS_BASE+0x00E0

.p2align 4	# align 16 bytes (IMPORTANT!!!)
.set push
.set noreorder # disable assembler reorder mode, so the code will not be optimized or changed in any way, giving complete instruction order control to the programmer
.set noat # disable assembler from using register $1 (known as the assembler temporary, or $at register) to hold intermediate values when performing macro expansions
.set nomacro # warning if any statement expands to more than one machine instruction

#####################################################
# .gsm_engine section
#####################################################

.section .gsm_engine, "a"

############
# Variables
############


.globl MIPS_Regs
MIPS_Regs:			.space	0x200


.globl Source_INTERLACE
Source_INTERLACE:	.word 0
.globl Source_MODE
Source_MODE:		.word 0
.globl Source_FFMD
Source_FFMD:		.word 0

.globl Source_SMODE2
Source_SMODE2:		.dword 0
.globl Source_DISPLAY
Source_DISPLAY:		.dword 0
.globl Source_SYNCV
Source_SYNCV:		.dword 0

.globl DOUBLE_HEIGHT_adaptation
DOUBLE_HEIGHT_adaptation:			.byte 0			# Flags Height Doubling at INT&FFMD

.p2align 4	# align 16 bytes (IMPORTANT!!!)

.globl Calculated_DISPLAY
Calculated_DISPLAY:		.dword 0

.globl Target_INTERLACE
Target_INTERLACE:	.word 0
.globl Target_MODE
Target_MODE:		.word 0x50
.globl Target_FFMD
Target_FFMD:		.word 1

.globl Target_SMODE2
Target_SMODE2:		.dword ((1<<1)|0)
.globl Target_DISPLAY
Target_DISPLAY:		.dword 0x001DF59F008230E8
.globl Target_SYNCV
Target_SYNCV:		.dword 0x00C78C0001E00006

.globl automatic_adaptation
automatic_adaptation:		.byte 0			# Byte flag
.globl DISPLAY_fix
DISPLAY_fix:		.byte 0			# Byte flag
.globl SMODE2_fix
SMODE2_fix:		.byte 0			# Byte flag
.globl SYNCV_fix
SYNCV_fix:		.byte 0			# Byte flag

.p2align 4	# align 16 bytes (IMPORTANT!!!)

.globl Old_SetGsCrt
Old_SetGsCrt:		.word 0

###############################
# Opcode emulation jump tables
###############################
#
# They differentiate between different kinds of access that may have triggered the debug trap we use.
# This way the number of cases does not affect the time delay for testing.

# First we have a table with jump offsets for opcode dependency
op_t:
.word ignore, ignore,  ignore, ignore				# 00-03
.word ignore, ignore,  ignore, ignore				# 04-07
.word ignore, ignore,  ignore, ignore				# 08-0B
.word ignore, ignore,  ignore, ignore				# 0C-0F
.word ignore, ignore,  ignore, ignore				# 10-13
.word ignore, ignore,  ignore, ignore				# 14-17
.word ignore, ignore,  ldl_op, ldr_op				# 18-1B
.word ignore, ignore,  lq_op,  sq_op				# 1C-1F
.word lb_op,  lh_op,   lwl_op, lw_op				# 20-23
.word lbu_op, lhu_op,  lwr_op, lwu_op				# 24-27
.word sb_op,  sh_op,   swl_op, sw_op				# 28-2B
.word sdl_op, sdr_op,  swr_op, ignore				# 2C-2F
.word ignore, ignore,  ignore, ignore				# 30-33
.word ignore, ignore,  ignore, ld_op				# 34-37
.word ignore, ignore,  ignore, ignore				# 38-3B
.word ignore, ignore,  ignore, sd_op				# 3C-3F

# Table 1 for branch opcodes when trapping branch delay slot
BD_t1:
.word B_com0, B_com1, B_J, B_JAL				# 00-03
.word B_BEQ, B_BNE, B_BLEZ, B_BGTZ				# 04-07
.word B_skip, B_skip, B_skip, B_skip				# 08-0B
.word B_skip, B_skip, B_skip, B_skip				# 0C-0F
.word B_BC0x, B_BC1x, B_skip, B_skip				# 10-13
.word B_BEQL, B_BNEL, B_BLEZL, B_BGTZL				# 14-17
.word B_skip, B_skip, B_skip, B_skip				# 18-1B
.word B_skip, B_skip, B_skip, B_skip				# 1C-1F
.word B_skip, B_skip, B_skip, B_skip				# 20-23
.word B_skip, B_skip, B_skip, B_skip				# 24-27
.word B_skip, B_skip, B_skip, B_skip				# 28-2B
.word B_skip, B_skip, B_skip, B_skip				# 2C-2F
.word B_skip, B_skip, B_skip, B_skip				# 30-33
.word B_skip, B_skip, B_skip, B_skip				# 34-37
.word B_skip, B_skip, B_skip, B_skip				# 38-3B
.word B_skip, B_skip, B_skip, B_skip				# 3C-3F

# Table 2 for branch sub-opcodes when trapping branch delay slot
BD_t2:
.word B_BLTZ, B_BGEZ, B_BLTZL, B_BGEZL				# 00-03
.word B_skip, B_skip, B_skip, B_skip				# 04-07
.word B_skip, B_skip, B_skip, B_skip				# 08-0B
.word B_skip, B_skip, B_skip, B_skip				# 0C-0F
.word B_BLTZAL, B_BGEZAL, B_BLTZALL, B_BGEZALL			# 10-13
.word B_skip, B_skip, B_skip, B_skip				# 14-17
.word B_skip, B_skip, B_skip, B_skip				# 18-1B
.word B_skip, B_skip, B_skip, B_skip				# 1C-1F

############
# Functions
############

#
# Hook_SetGsCrt:
#
.globl Hook_SetGsCrt
.ent	Hook_SetGsCrt
Hook_SetGsCrt:
addiu $sp, $sp, -0x0010					# reserve 16 bytes stack space (1 reg)
sd $ra, 0($sp)				# Push return address on stack
lui $v0,(KSEG >> 16)				# Base Address
sw		$a0, %lo(Source_INTERLACE)($v0)
sw		$a1, %lo(Source_MODE)($v0)
sw		$a2, %lo(Source_FFMD)($v0)
and	$a3, $a0,$a2						# a3 = Interlace & Field
andi $a3, $a3,1							# a3 &= 1 limited to 1 bit
sb		$a3, %lo(DOUBLE_HEIGHT_adaptation)($v0)
ld		$a0, %lo(Target_SMODE2)($v0)
sb		$a0, %lo(SMODE2_fix)($v0)
lw		$a0, %lo(Target_INTERLACE)($v0)
lw		$a1, %lo(Target_MODE)($v0)
lw		$a2, %lo(Target_FFMD)($v0)
lw		$a3, %lo(Old_SetGsCrt)($v0)					# a3 -> original SetGsCrt function
sync.l				# The opcodes sync.l and sync.p made a lot of games compatible
sync.p				# They ensure all values are stable before the following call
jalr $a3							# Call original SetGsCrt
nop 
ld $ra, 0($sp)				# Pull return address from stack 
jr		$ra								# Return to caller
addiu $sp, $sp, 0x0010				# Restore sp during return

.end	Hook_SetGsCrt

#
# GSHandler:
# When the processor takes a level 2 exception, the processor switches to
# the kernel mode, by setting Status.ERL to 1.
#
.globl GSHandler
.ent	GSHandler
GSHandler:
sync.l
sync.p
sq $k0, -0x10($zero)					# Store registers reserved for kernel
sq $k1, -0x20($zero)					# usage in interrupt/trap handling 
				# Save all MIPS registers except zero($0) k0($26) and k1($27))
				# RA NB: NO!!! ALL registers are needed in this array, for evaluations
				# RA NB: Even the $zero register is needed, as it may be used in conditionals

lui $k0,(KSEG >> 16)			# Store MIPS_Regs via k0
sq $zero, 0($k0)					# $zero
sq	$1, 0x10($k0)					# at
sq	$2, 0x20($k0)					# v0
sq	$3, 0x30($k0)					# v1
sq	$4, 0x40($k0)					# a0
sq	$5, 0x50($k0)					# a1
sq	$6, 0x60($k0)					# a2
sq	$7, 0x70($k0)					# a3
sq	$8, 0x80($k0)
sq	$9, 0x90($k0)
sq	$10, 0xA0($k0)
sq	$11, 0xB0($k0)
sq	$12, 0xC0($k0)
sq	$13, 0xD0($k0)
sq	$14, 0xE0($k0)
sq	$15, 0xF0($k0)
sq	$16, 0x100($k0)
sq	$17, 0x110($k0)
sq	$18, 0x120($k0)
sq	$19, 0x130($k0)
sq	$20, 0x140($k0)
sq	$21, 0x150($k0)
sq	$22, 0x160($k0)
sq	$23, 0x170($k0)
sq	$24, 0x180($k0)
sq	$25, 0x190($k0)
				# 0x1A0 must be set later, to initial $k0 value
				# 0x1B0 must be set later, to initial $k1 value
sq	$28, 0x1C0($k0)
sq	$29, 0x1D0($k0)
sq	$30, 0x1E0($k0)
sq	$31, 0x1F0($k0)

lq $t0, -0x10($zero)					# t0 = entry k0
lq	$t1, -0x20($zero)				# t1 = entry k1
sq	$t0, 0x1A0($k0)					# store entry k0 in register array
sq	$t1, 0x1B0($k0)					# store entry k1 in register array
	
#
# The read/write ErrorEPC register holds the virtual address at which instruction
# processing can resume after servicing an error. This address can be:
# - The virtual address of the instruction that caused the exception
# - The virtual address of the immediately preceding branch or jump instruction
# (when the instruction is in a branch delay slot, and the BD2 bit in the Cause
# register is set).
#
mfc0 $k1, $13					# k1 = Cause bits of last exception (COP0 reg 13)
srl $k1, 30					# k1 is aligned for BD2 (Flags branch delay slot used)
						# 1 -> delay slot, 0 -> normal
andi $k1, 1					# k1 = BD2
sll $k1, 2					# k1 = BD2*4
mfc0 $k0, $30					# k0 = ErrorPC (COP0 reg 30) -> MIPS instruction
addu $k0, $k1					# Add 4 to opcode address for Branch Delay Slot
						# Next get rt (target register) and write address
						# but first check that the instruction is one we patch
lw $v0, 0($k0)					# v0 = MIPS instruction that caused trap

srl $v1,$v0,26			# v1 = opcode (range 0x00-0x3F)
andi $v1,$v1, 0x003F		# v1 = pure opcode number
sll $v1,$v1,2			# v1 = op_num*2 (word offset for jump table)

lui $a2,(KSEG >> 16)		# a2 -> .gsm_engine
la $a3, op_t			# a3 -> op_t

addu $a0,$v1,$a3		# a0 -> active entry in op_t
lw $a1,0($a0)			# a1 = opcode handler from op_t

jr $a1				# jump to separate opcode handlers
nop				# with v0=instruction, a2->KSeg, a3->op_t
	
#  For the present we ignore read operations (should never happen. Not trapped)
ldl_op:
ldr_op:
lq_op:
lb_op:
lh_op:
lwl_op:
lw_op:
lbu_op:
lhu_op:
lwr_op:
lwu_op:
lq_op:
ld_op:
ignore:					# We just ignore weird opcodes that we don't implement
beqzl $zero,exit_GSHandler
nop

#  For the present we treat all write operations as 'sd'
sq_op:
sb_op:
sh_op:
swl_op:
sw_op:
sdl_op:
sdr_op:
swr_op:
sd_op:
have_some_write:							# Opcode is a write, so we must check further
srl $a1, $v0, 16			
andi $a1, $a1, 0x1f					# a1 = unscaled rt reg index
srl $a0, $v0, 21
andi $a0, $a0, 0x1f					# a0 = unscaled base reg index

sll $k0, $a0, 4						# k0 = raw base_ix << 4 (scaled base_ix reg index)

addu $v1, $a2, $k0					# v1 = &MIPS_Regs[base_ix]; (if type = u128)
lw		$a3, 0($v1)						# a3 = base register value
andi $k1, $v0, 0xFFFF				# k1 = offset field of instruction
addu $a3, $a3, $k1					# a3 = address which triggered breakpoint

sll $k0, $a1, 4						# k0 = raw rt_ix << 4 (scaled rt_ix reg index)
addu $v0, $a2, $k0					# v0 = &MIPS_Regs[rt_ix];
ld $a1, 0($v0)						# a1 = value in rt

#  NB: The trapping method forces us to trap some GS registers we don't want.
#      It is crucial that the writing of those registers proceeds undisturbed.
#      This is handled by the final test case below, at label "not_wanted_reg".

#  Here a1=source_data, a2->.gsm_engine, a3=dest_address
#  NB: Since address is changed to offset by ANDI, it is valid for all segments
#  NB: We avoid masking a3 itself though, in case this is an unwanted register
#  NB: Remasking for KSEG1 should be done in each handler for wanted registers

andi $v0,$a3,0xFFFF						# v0 = dest offset from GS_BASE
addi $v1,$v0, %lo(GS_BASE - GS_SMODE2)
beqzl $v1,have_SMODE2_write					# in case of  dest == GS_reg_SMODE2
nop
addi $v1,$v0, %lo(GS_BASE - GS_DISPLAY2)
beqzl $v1,have_DISPLAYX_write					# in case of  dest == GS_reg_DISPLAY2
nop
addi $v1,$v0, %lo(GS_BASE - GS_DISPLAY1)
beqzl $v1,have_DISPLAYX_write					# in case of  dest == GS_reg_DISPLAY1
nop
addi $v1,$v0, %lo(GS_BASE - GS_SYNCV)
beqzl $v1,have_SYNCV_write					# in case of  dest == GS_reg_SYNCV
nop

not_wanted_reg:							# Register unwanted, so perform op unchanged
sd $a1,0($a3)							# Store source data unchanged to destination
beqzl $zero,exit_GSHandler
nop

#  ----------------------------	
# SMODE2
# .----.---.---------.-----------------------------------.
# |Name|Pos|Format   |Contents                           |
# +----+---+---------+-----------------------------------|
# |INT | 0 |int 0:1:0|Interlace Mode Setting             |
# |    |   |         |0 Non-Interlace Mode               |
# |    |   |         |1 Interlace Mode                   |
# |FFMD| 1 |int 0:1:0|Setting in Interlace Mode          |
# |    |   |         |0 FIELD Mode(Read every other line)|
# |    |   |         |1 FRAME Mode(Read every line)      |
# |DPMS|3:2|int 0:2:0|VESA DPMS Mode Setting             |
# |    |   |         |00 On          10 Suspend          |
# |    |   |         |01 Stand-by    11 Off              |
# ^----^---^---------^-----------------------------------.
have_SMODE2_write:
lui $v0,0xB200												# v0 = GS base address in KSEG1
andi $a3,$a3,0xFFFF										# a3 = GS register offset
or	$a3,$a3,$v0												# a3 = GS register address in KSEG1
sd $a1, %lo(Source_SMODE2)($a2)					# Source_SMODE2 = a1
lb $v0, %lo(SMODE2_fix)($a2)					# v0 = SMODE2_fix
bnel $v0,$zero,store_v0_as_SMODE2				# in case of  Separate SMODE2 fix disabled
or $v0,$zero,$a1											# 	go use v0=a1 for SMODE2
srl $v0,$a1,1												# v0 = a1 aligned for FFMD in bit0
and	$v0,$v0,$a1											# v0 bit 0 = INT & FFMD
andi $v0,$v0,1												# v0 bit 0 = INT & FFMD isolated
sb $v0, %lo(DOUBLE_HEIGHT_adaptation)($a2)				# store Adapt_DoubleHeight flag
beqz $v0,1f														# in case of  no DoubleHeight need
ld $v0, %lo(Target_SMODE2)($a2)				# go use Target_SMODE2 as adapted SMode2
																							# otherwise just set v0 = Target_SMODE2
andi $a1,$a1,2												# a1 = FFMD of Source_SMODE2
andi $v0,$v0,0xFFFD										# v0 = Target_SMODE2 without FFMD
or $v0,$v0,$a1											# v0 = Target_SMODE2 + Source FFMD
1: 												# Here v0 is adapted SMode2 value
sb $v0, %lo(SMODE2_fix)($a2)						# Remember this adaption for later
store_v0_as_SMODE2:
sync.l				# The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
sync.p				# These ones give a break to ee take a breath after patching and before enter original SetGsCrt
beqzl	$zero,exit_GSHandler					# Now go exit
sd $v0,0($a3)														# after storing GS_reg_SMODE2

#  ----------------------------
# SYNCV
# .----.-----.----------.
# |Name|Pos. |Format    |
# |----+-----+----------+
# |VFP | 9:0 |int 0:10:0|
# |VFPE|19:10|int 0:10:0|
# |VBP |31:20|int 0:12:0|
# |VBPE|41:32|int 0:12:0|
# |VDP |52:42|int 0:11:0|
# |VS  |??:53|int 0:??:0|
# '----^-----^----------^
have_SYNCV_write:
lui $v0,0xB200												# v0 = GS base address in KSEG1
andi $a3,$a3,0xFFFF										# a3 = GS register offset
or	$a3,$a3,$v0												# a3 = GS register address in KSEG1
sd $a1, %lo(Source_SYNCV)($a2)						# Source_SYNCV = a1
lb $v0, %lo(SYNCV_fix)($a2)					# v0 = SYNCV_fix
bnel $v0,$zero,store_v0_as_SYNCV				# in case of  Separate SYNCV fix disabled
or $v0,$zero,$a1											# 	go use v0=a1 for SYNCV
ld $v0, %lo(Target_SYNCV)($a2)						# v0 = Target_SYNCV
beql $v0,$zero,exit_GSHandler				# in case of  Target_SYNCV is zero
sd $a1,0($a3)														# 	go use Source_SYNCV

store_v0_as_SYNCV:
sync.l				# The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
sync.p				# These ones give a break to ee take a breath after patching and before enter original SetGsCrt
beqzl	$zero,exit_GSHandler					# Now go exit
sd $v0,0($a3)														# after storing GS_SYNCV

#  ----------------------------
have_DISPLAYX_write:				# Here a1=source_data, a2->.gsm_engine, a3=dest_adress
lui $v0,0xB200												# v0 = GS base address in KSEG1
andi $a3,$a3,0xFFFF										# a3 = GS register offset
or	$a3,$a3,$v0												# a3 = GS register address in KSEG1
sd $a1, %lo(Source_DISPLAY)($a2)				# request DISPLAYx value = a1
ld $v1, %lo(Target_DISPLAY)($a2)						# v1=forcing DISPLAYx template

#  Source_DISPLAY == Requested   DX, DY, MAGH, MAGV, DW and DH values
#  Target_DISPLAY == Modded(forced) DX, DY, MAGH, MAGV, DW and DH values
#  Both are 64 bit units with encoded bit fields like GS DISPLAYx registers

#  Patch to adapt request to enforced mode in v1 MUST preserve a1, a2, a3

lb $v0, %lo(automatic_adaptation)($a2)
bnel $v0,$zero,91f									# in case of (automatic_adaptation)
or $a1,$zero,$v1									# 	simulate request same as forced mode

91:
li	$v0,0									# preclear v0 as result DISPLAYx accumulator

#  Here a0=free, a1=Source_DISPLAY, a2->.gsm_engine, a3=dest_address
#  Also v0=result_accumulator, v1=Target_DISPLAY, t0-t7=free


# ------------------------------------------------------------------------------------------------------


# ----- Start of code section for horizontal fields of DISPLAYx -----
# t4:PW:MAGH2  t5:Max_Width-1 t6:Max_Width:DX2 t7:DW2 v0:result_acc
# -------------------------------------------------------------------
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | MAGH | 26:23 | int 0: 4:0 | magnification in horizontal direction | 0xF   |
# '------^-------^------------^---------------------------------------^-------^
dsrl $t0,$a1,23       # t0=request value aligned for MAGH
andi $t0,$t0,0x0F     # t0=MAGH1 masked
addi $t0,$t0,1        # t0=XScale1 = MAGH1+1
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | DW   | 43:32 | int 0:12:0 | display area width - 1 (VCK units)    | 0xFFF |
# '------^-------^------------^---------------------------------------^-------^
dsrl32 $t1,$a1,0      # t1=request value aligned for DW
andi $t1,$t1,0x0FFF   # t1=DW1 masked
addi $t1,$t1,1        # t1=(DW1+1)
divu $zero,$t1,$t0    # LO=PW1 = (DW1+1)/XScale
dsrl32 $t0,$v1,0      # t0=forcing template aligned for DW1
andi $t5,$t0,0x0FFF   # t5=Max_Width-1 masked
mflo $t4              # t4=PW
addi $t6,$t5,1         # t6=Max_Width
nop
divu	$zero,$t6,$t4    # LO=XScale2 = Max_Width/PW
nop
nop
mflo $t0              # t0=XScale2
bne $t0,$zero,11f     # in case of (!XScale2) # XScale2 invalid zero
nop                   # {
or $t7,$zero,$t5      # 	t7=DW2 = Max_Width-1
sub $t0,$t6,$t4       # 	t0=(Max_Width-PW)
li $t4,0              # 	t4=MAGH2 = 0
beq $zero,$zero,13f		# }
nop                   # otherwise # XScale2 nonzero

11:                     # {
addi $t1,$t0,-16      # 	t1=XScale2-16
bgtzl $t1,12f         # 	if(t1>0)
or $t0,$zero,16       # 		t0=XScale2=16;

12:
mult $t4,$t0          # 	LO=(PW*XScale2)
nop
nop
mflo $t1              # 	t1=(PW*XScale2)
addi $t7,$t1,-1       # 	t7=DW2 = (PW*XScale2)-1
addi $t4,$t0,-1       # 	t4=MAGH2 = XScale2-1
sub $t0,$t5,$t7       # 	t0=(Max_Width-1-DW2)

13:                     # }
dsra $t0,$t0,1        # t0=t0/2 = half_excess_width (can be negative)
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | DX   | 11:0  | int 0:12:0 | x pos in display area (VCK units)     | 0xFFF |
# '------^-------^------------^---------------------------------------^-------^
andi $t1,$v1,0x0FFF   # t1=Min_DX masked
add $t6,$t0,$t1       # t6=DX2
bltzl $t6,14f         # in case of (DX2<0)
and $t6,$t6,$zero     # 	DX2=0;

14:
sub $t0,$t1,$t6       # t0=Min_DX-DX2 
bgtzl $t0,15f         # in case of (Min_DX > DX2)
add $t7,$t7,$t0       # 	t7 += Min_DX-DX2  # DW2 adjusted

15:
andi $t7,$t7,0x0FFF   # t7=DW2 masked
andi $t4,$t4,0x000F   # t4=MAGH2 masked
andi $t6,$t6,0x0FFF   # t6=DX2 masked
dsll32 $t0,$t7,0      # t0=DW2 aligned for DISPLAYx
or $v0,$v0,$t0        # Accumulate DW field to DISPLAYx value in v0
dsll $t0,$t4,23       # t0=MAGH2 aligned for DISPLAYx
or $v0,$v0,$t0        # Accumulate MAGH field to DISPLAYx value in v0
or $v0,$v0,$t6        # Accumulate DX field to DISPLAYx value in v0
# ----- End of code section for horizontal fields of DISPLAYx -----


# ----- Start of code section for vertical fields of DISPLAYx -------
# t4:PH1:MAGV2  t5:Max_height-1 t6:Max_height:DY2 t7:DH2 v0:result_acc
# ---------------------------------------------------------------------
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | MAGV | 28:27 | int 0: 2:0 | magnification in vertical direction   | 0x3   |
# '------^-------^------------^---------------------------------------^-------^
dsrl $t0,$a1,27       # t0=request value aligned for MAGV
andi $t0,$t0,0x03     # t0=MAGV1 masked
addi $t0,$t0,1        # t0=YScale1 = MAGV1+1
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | DH   | 54:44 | int 0:11:0 | display area height - 1 (pixel units) | 0x7FF |
# '------^-------^------------^---------------------------------------^-------^
dsrl32	$t1,$a1,12     # t1=request value aligned for DH
andi	$t1,$t1,0x07FF   # t1=DH1 masked
addi	$t1,$t1,1        # t1=(DH1+1) = PH1
divu $zero,$t1,$t0    # LO=PH1 = (DH1+1)/YScale1
dsrl32 $t0,$v1,12     # t0=forcing template aligned for DH
andi $t5,$t0,0x07FF   # t5=Max_Height-1 masked
mflo	$t4              # t4=PH1
addi $t6,$t5,1        # t6=Max_Height
nop
divu	$zero,$t6,$t4    # LO=YScale2 = Max_Height/PH1
nop
nop
mflo $t0              # t0=YScale2
bne $t0,$zero,21f     # in case of (!YScale2) # YScale2 invalid
nop                   # {
or $t7,$zero,$t5      # 	t7=DH2 = Max_Height-1
sub $t0,$t6,$t4       # 	t0=(Max_Height-PH1)
li $t4,0              # 	t4=MAGV2 = 0
beq $zero,$zero,23f		# }
nop                   # otherwise # YScale2 non-zero

21:                     # {
addi $t1,$t0,-4       # 	t1=YScale2-4
bgtzl $t1,22f         # 	if(t1>0)
or $t0,$zero,4        # 		t0=YScale2=4;

22:
mult $t4,$t0          # 	LO=(PH1*YScale2)
nop
nop
mflo $t1              # 	t1=(PH1*YScale2)
addi $t7,$t1,-1       # 	t7=DH2 = (PH1*YScale2)-1

addi $t4,$t0,-1       # 	t4=MAGV2 = YScale2-1
sub $t0,$t5,$t7       # 	t0=(Max_Height-1-DH2)

23:                     # }
dsra $t0,$t0,1        # t0=t0/2 = half_excess_height (can be negative)
# .------.-------.------------.---------------------------------------.-------.
# | Name | Pos.  | Format     | Contents                              | Mask  |
# |      |       |            |                                       |       |
# |------+-------+------------+---------------------------------------+-------+
# | DY   | 22:12 | int 0:11:0 | y pos in display area (raster units)  | 0x7FF |
# '------^-------^------------^---------------------------------------^-------^
dsrl	$t1,$v1,12       # t1=forcing template aligned for DY
andi $t1,$t1,0x07FF   # t1=Min_DY masked
add $t6,$t0,$t1       # t6=DY2
bltzl $t6,24f         # in case of (DY2<0)
and $t6,$t6,$zero     # 	DY2=0;

24:
sub $t0,$t1,$t6       # t0=Min_DY-DY2 
bgtzl $t0,25f         # in case of (Min_DY > DY2)
add $t7,$t7,$t0       # 	t7 += Min_DY-DY2  # DH2 adjusted

25:
lb		$t0, %lo(DOUBLE_HEIGHT_adaptation)($a2)	# in case of  doubled height not needed
beq	$t0,$zero,26f										# 	Calculation is complete

ld		$t0, %lo(Target_SMODE2)($a2)
andi	$t0,$t0,1												# in case of  target mode interlaced
bne	$t0,$zero,26f										# 	Calculation is complete
nop
beql	$t4,$zero,26f		# in case of  MAGV==0
addi	$t4,$t4,1				# 	go use MAGV += 1
addi $t4,$t4,2				# MAGV += 2 (Because scale was 2 or larger)
addi $t0,$t4,-4				# Compare MAGV with 4 (too large ?)
bgezl	$t0,26f				# in case of  MAGV too large
ori	$t4,$zero,3			# 	go use MAGV = 3

26:
andi $t7,$t7,0x07FF   # t7=DH2 masked
andi $t4,$t4,0x0003   # t4=MAGV2 masked
andi $t6,$t6,0x07FF   # t6=DY2 masked
dsll32 $t0,$t7,12     # t0=DH2 aligned for DISPLAYx
or $v0,$v0,$t0        # Accumulate DH field to DISPLAYx value in v0
dsll $t0,$t4,27       # t0=MAGV2 aligned for DISPLAYx
or $v0,$v0,$t0        # Accumulate MAGV field to DISPLAYx value in v0
dsll $t0,$t6,12       # t0=DY2 aligned for DISPLAYx
or $v0,$v0,$t0        # Accumulate DY field to DISPLAYx value in v0
# ----- End of code section for vertical fields of DISPLAYx -----




# ------------------------------------------------------------------------------------------------------

Adapt_Calculated:
sd $v0, %lo(Calculated_DISPLAY)($a2)  # Store new DISPLAYx value (for feedback)

#  End of Patch to adapt request with the resulting request in v0


lb $v1, %lo(DISPLAY_fix)($a2)					# v1 = DISPLAY_fix
bnel $v1,$zero,94f								# in case of (DISPLAY_fix)
ld $v0, %lo(Target_DISPLAY)($a2)				# 	use forced mode without adaption

94:
lui $a0, (GS_BASE >> 16)					# a0 -> GS_BASE
lb $t0, %lo(SMODE2_fix)($a2)				# t0 = adapted SMODE2 value
sync.l				# The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
sync.p				# These ones give a break to ee take a breath after patching and before enter original SetGsCrt
sd $t0, %lo(GS_SMODE2)($a0)						# store it in GS_reg_SMODE2

#sd $v0,0($a3)												# Store modified GS_reg_DISPLAYX
sd $v0, %lo(GS_DISPLAY2)($a0)								# Store modified GS_reg_DISPLAY2
sd $v0, %lo(GS_DISPLAY1)($a0)								# Store modified GS_reg_DISPLAY1

ld $t0, %lo(Target_SYNCV)($a2)				# t0 = Target_SYNCV
sync.l				# The addition of these two lines (sync.l and sync.p) made a lot of titles compatible with GSM!
sync.p				# These ones give a break to ee take a breath after patching and before enter original SetGsCrt
bnel $t0,$zero,96f										# in case of  Target_SYNCV is non-zero
sd $t0, %lo(GS_SYNCV)($a0)						# 	store it in GS_reg_SYNCV

96:
#li $t0, 0xFFFFFF 					# White
#sd $t0, %lo(GS_BGCOLOUR)($a0)		# Change BGCOLOR (for debugging purposes)
exit_GSHandler_complex:
#  ----- Here we restore some registers, used for complex calculations above
lq $10, 0xA0($a2)					# t2
lq $11, 0xB0($a2)					# t3
lq $12, 0xC0($a2)					# t4
lq $13, 0xD0($a2)					# t5
lq $14, 0xE0($a2)					# t6
lq $15, 0xF0($a2)					# t7

exit_GSHandler:
#  ----- Here we restore most registers used for all GSHandler traps
#  ----- Since only a few registers are used this way we only restore those
lui $k0,(KSEG >> 16)			# Restore MIPS_Regs via k0
lq $1, 0x10($k0)					# at
lq $2, 0x20($k0)					# v0
lq $3, 0x30($k0)					# v1
lq $4, 0x40($k0)					# a0
lq $5, 0x50($k0)					# a1
lq $6, 0x60($k0)					# a2
lq $7, 0x70($k0)					# a3

#  Past this point in GSHandler, use only k0,k1,t0,t1

mfc0 $k0, $13				# k0 = Cause of last exception
srl $k0, 30				# BD2 Flags debug exception in branch delay slot.
										# 1 -> delay slot, 0 -> normal
andi $k0, 1					# k0 = BD2 bit isolated
bnez $k0,	GSHandler_BranchDelaySlotException
				# 	Deal properly with Branch Delay Slot Exceptions (when needed)
nop

mfc0 $k0,$30						# k0 = ErrorEPC
addiu $k0,$k0,4					# k0 = ErrorEPC+4 (-> next opcode)
mtc0 $k0,$30						# store k0 in Error Exception Program Counter
#sync											# ensure COP0 register update before proceeding

b GSHandler_Final_Exit
nop

# ----------------------------
GSHandler_BranchDelaySlotException:
mfc0 $k0,$30		# k0 = Error Exception Program Counter
lw $k0,0($k0)		# k0 = instruction at EPC location (branch or jump)
srl $k1,$k0,26		# k1 = aligned for opcode (range 0x00-0x3F)
andi $k1,$k1,0x3F	# k1 = pure opcode number
sll $k1,$k1,2		# k1 = op_num*2 (offset for jump table)

la $t1, BD_t1		# t1 -> BD_t1
addu $k1,$k1,$t1	# k1 -> active entry in BD_t1
lw $t0,0($k1)		# t0 = opcode handler from BD_t1
la $t1, op_t		# t1 -> op_t

jr $t0				# jump to branch/jump opcode handlers
nop					# with k0=instruction, t1->op_t

# ----------------------------
B_com1:				# This group contains 8 different branch operations
srl $k1,$k0,16		# k1 = aligned for sub_opcode (range 0x00-0x1F)
andi $k1,$k1,0x3F	# k1 = pure sub_opcode number
sll $k1,$k1,2		# k1 = sub_op_num*2 (offset for jump table)

la $t1, BD_t2		# t1 -> BD_t2
addu $k1,$k1,$t1	# k1 -> active entry in BD_t2
lw $t0,0($k1)		# t0 = opcode handler from BD_t2
la $t1, op_t		# t1 -> op_t

jr $t0				# jump to branch/jump opcode handlers
nop					# with k0=instruction, t1->op_t

# ----------------------------
B_com0:						# opcode 0x00 includes both JR and JALR
li $t0,0xFC1F07FF		# t0 = bitmask for JALR
and $k1,$k0, $t0		# k1 = potential JALR instruction
li $t0,9				# t0 = JALR test constant
beq $k1,$t0,B_JR_JALR	# in case of  JALR identified, go deal with it
nop
li $t0,0xFC1FFFFF		# t0 = bitmask for JR
and $k1,$k0, $t0		# k1 = potential JR instruction
li $t0,8				# t0 = JR test constant
bne $k1,$t0,B_skip		# in case of  JR not identified, go skip this code 
nop

B_JR_JALR:					# JR or JALR found, so make register indirect jump
srl $k1,$k0, 21			# k1 = aligned for JR/JALR rs register number
andi $k1,0x1F			# k1 = register number
sll $t1,$k1, 4			# t1 = array index for saved register data
lui $t0,(KSEG >> 16)	# t0 -> saved register array
addu $t0,$t1			# t0 -> register data of JR/JALR rs register
lw $t0,0($t0)			# t0 = jump destination address
mtc0 $t0,$30			# store t0 in Error Exception Program Counter
#	sync				# ensure COP0 register update before proceeding
b GSHandler_Final_Exit
nop

	# ----------------------------

B_J:
B_JAL:
# Here we have a definite jump with absolute address/4 in instruction
li $t0, 0x3FFFFFF				# t0 = bitmask for jump destination bits
and $k1, $k0, $t0					# k1 = destination bits (== destination/4)
sll $t0, $k1, 2						# t0 = jump destination address
mtc0 $t0, $30							# store t0 in Error Exception Program Counter
#sync												# ensure COP0 register update before proceeding
b GSHandler_Final_Exit
nop

#  ----------------------------
#  'likely' type branches will only trap on delay slot if branch is taken,
#  so for those cases we do not need to make any further tests of conditions
B_likely:
B_BGEZL:
B_BGEZALL:
B_BLTZL:
B_BLTZALL:
B_BEQL:
B_BNEL:
B_BLEZL:
B_BGTZL:
B_taken:
#  Here we have a 'branch taken' operation with relative offset/4 in instruction
li $t0, 0xFFFF						# t0 = bitmask for branch offset bits
and $k1, $k0, $t0					# k1 = branch offset bits (== offset/4)
sll $k1, 2								# k1 = branch offset
mfc0 $t0, $30							# t0 = Error Exception Program Counter
addiu $t0, 4								# t0 = ErrorEPC+4 (-> address after branch op)
addu $t0, $k1							# t0 = jump destination address
mtc0 $t0, $30							# store t0 in Error Exception Program Counter
#sync												# ensure COP0 register update before proceeding
b GSHandler_Final_Exit
nop

#  ----------------------------
B_BLTZ:
B_BLTZAL:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = register number
sll $t1,$k1, 4						# t1 = array index for saved register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $t0,$t1							# t0 -> register data of rs register
lq $t1,0($t0)						# t1 = rs register data
#sync
bltz $t1,B_taken
nop

B_not_taken:
mfc0 $k0,$30						# k0 = ErrorEPC
addiu $k0,$k0,8					# k0 = ErrorEPC+8 pass branch_op and delay_slot
mtc0 $k0,$30						# store k0 in Error Exception Program Counter
#sync											# ensure COP0 register update before proceeding
b GSHandler_Final_Exit
nop
	
#  ----------------------------
B_BGEZ:
B_BGEZAL:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = register number
sll $t1,$k1, 4						# t1 = array index for saved register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $t0,$t1							# t0 -> register data of rs register
lq $t1,0($t0)						# t1 = rs register data
#sync
bgez $t1,B_taken
nop
b B_not_taken
nop

#  ----------------------------
B_BLEZ:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = register number
sll $t1,$k1, 4						# t1 = array index for saved register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $t0,$t1							# t0 -> register data of rs register
lq $t1,0($t0)						# t1 = rs register data
#sync
blez $t1,B_taken
nop
b B_not_taken
nop

#  ----------------------------
B_BGTZ:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = register number
sll $t1,$k1, 4						# t1 = array index for saved register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $t0,$t1							# t0 -> register data of rs register
lq $t1,0($t0)						# t1 = rs register data
#sync
bgtz $t1,B_taken
nop
b B_not_taken
nop

#  ----------------------------
B_BEQ:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = rs register number
sll $k1,$k1, 4						# k1 = array index for saved rs register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $k1,$t0							# k1 -> register data of rs register
lq $t1,0($k1)						# t1 = rs register data
#sync
srl $k1,$k0, 16					# k1 = aligned for rt register number
andi $k1,0x1F							# k1 = rt register number
sll $k1,$k1, 4						# k1 = array index for saved rt register data
addu $k1,$t0							# k1 -> register data of rt register
lq $t0,0($k1)						# t0 = rt register data
#sync
beq $t0,$t1,B_taken
nop
b B_not_taken
nop

#  ----------------------------
B_BNE:
srl $k1,$k0, 21					# k1 = aligned for rs register number
andi $k1,0x1F							# k1 = rs register number
sll $k1,$k1, 4						# k1 = array index for saved rs register data
lui $t0,(KSEG >> 16)			# t0 -> saved register array
addu $k1,$t0							# k1 -> register data of rs register
lq $t1,0($k1)						# t1 = rs register data
#sync
srl $k1,$k0, 16					# k1 = aligned for rt register number
andi $k1,0x1F							# k1 = rt register number
sll $k1,$k1, 4						# k1 = array index for saved rt register data
addu $k1,$t0							# k1 -> register data of rt register
lq $t0,0($k1)						# t0 = rt register data
#sync
bne $t0,$t1,B_taken
nop
b B_not_taken
nop

#  ----------------------------
B_BC0x:								# At present we do not implement COP0 branches
B_BC1x:								# At present we do not implement COP1 branches
B_skip:								# Unrecognized opcode, so just pass it by
mfc0 $k0, $30						# k0 = ErrorEPC
addiu $k0, $k0, 4				# k0 = ErrorEPC+4 (-> next opcode)
mtc0 $k0, $30						# store k0 in Error Exception Program Counter
#sync											# ensure COP0 register update before proceeding

GSHandler_Final_Exit:
lui $k0,(KSEG >> 16)			# k0 -> .gsm_engine
lq $8, 0x80($k0)					# t0
lq $9, 0x90($k0)					# t1

#mfc0 $k0, $12							# Set user mode, interrupts on
#ori $k0, $k0, 0x11
#mtc0 $k0, $12
#sync

lq $k0, -0x10($zero)					# Restore k0,k1 reserved for OS Kernel
lq $k1, -0x20($zero)
sync.p
sync.l
eret											# 	Return from exception
nop

end_GSHandler:
.end	GSHandler

.set pop

#####################################################
# .text section
#####################################################

.text

