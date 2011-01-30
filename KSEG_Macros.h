//----------------------------------------------------------------------------
//File name:   KSEG_Macros.h
//----------------------------------------------------------------------------
//Do not add comments to the macro lines, as that can disturb the preprocessor
//-----     -----     -----     -----     -----     -----     -----     -----
#define KMODE_MASK 0xFFFFFFE6
#define UMODE_BITS 0x00000019
//-----     -----     -----     -----     -----     -----     -----     -----
//Str_Exp Macro below helps us stringify expanded arguments of other macros
#define Str_Exp(stringparts) #stringparts

//-----     -----     -----     -----     -----     -----     -----     -----
//KDef Macro below defines typed symbols for KSeg space with a given address
#define KDef(type, name, address)\
	extern volatile type name;\
	__asm__(Str_Exp(name = address\n\t))

//-----     -----     -----     -----     -----     -----     -----     -----
//KRel Macro below defines typed symbols for KSeg space with a given offset
//relative to KSeg. The pure symbol is absolute address, and is available
//both in asm and in C, but a "rel_" prefix gives the offset, for asm only.
#define KRel(type, name, offset)\
	extern volatile type name;\
	__asm__(Str_Exp(rel_##name = offset\n\t));\
	__asm__(Str_Exp(name = offset+KSeg\n\t))

//-----     -----     -----     -----     -----     -----     -----     -----
//KFun_start macro below starts an asm block for a KSEG function
#define KFun_start(name)\
	void beg_##name();\
	void run_##name();\
	void end_##name();\
	__asm__(\
	".set noreorder\n\t"\
	Str_Exp(beg_##name:\n\t)\

//-----     -----     -----     -----     -----     -----     -----     -----
#define KFun_entry(name)\
	Str_Exp(run_##name:\n\t)

//-----     -----     -----     -----     -----     -----     -----     -----
//KFun_end macro below ends an asm block for a KSEG function
#define KFun_end(name)\
	Str_Exp(end_##name:\n\t)\
	".set reorder\n\t"\
);\
void *KSEG_##name##_p;\
void *KSEG_##name##_entry_p;\
int size_##name;\
void KFun_prep_##name(){\
	KSEG_##name##_p = KSEG_next_func_p;\
	size_##name = end_##name - beg_##name;\
	KSEG_next_func_p += ((size_##name + 15)& -16);\
	KSEG_##name##_entry_p = KSEG_##name##_p + (run_##name - beg_##name);\
}//ends KFun_prep##name of KFun_end

//-----     -----     -----     -----     -----     -----     -----     -----
//KFun_install copies the code of an asm function into KSEG memory space
#define KFun_install(name)\
	KFun_prep_##name();\
	size = KSEG_memcpy(beg_##name, KSEG_##name##_p, size_##name);

/*
#define KFun_install(name)\
	KFun_prep_##name();\
	scr_printf(Str_Exp(Copying resident name function to KSEG0...));\
	size = KSEG_memcpy(beg_##name, KSEG_##name##_p, size_##name);\
	scr_printf("0x%08x bytes\n", size);
*/

//-----     -----     -----     -----     -----     -----     -----     -----
//GS_Rel Macro below defines typed symbols for GS registers with a given offset
//relative to the GS register base. Symbol with "GS_reg_" prefix is absolute,
//available both in asm and in C, but a "GS_rel_" prefix gives the offset,
//for asm only.
#define GS_Rel(type, name, offset)\
	extern type GS_reg_##name;\
	__asm__(Str_Exp(GS_rel_##name = offset\n\t));\
	__asm__(Str_Exp(GS_reg_##name = offset+GS_BASE\n\t))

//----------------------------------------------------------------------------
typedef struct
{	u32	xb_magic;
	u32	xb_id;
	u32	xb_next;
	u32	xb_code[];
}	XBRA_header;

#define XB_magic 0
#define XB_id 4
#define XB_next 8
#define XB_code 12

	__asm__("XBRA_MAGIC = 0x41524258\n\t");
	__asm__("GSM__MAGIC = 0x5F4D5347\n\t");

#define XBRA_MAGIC 0x41524258
#define GSM__MAGIC 0x5F4D5347

//----------------------------------------------------------------------------
//End of file: KSEG_Macros.h
//----------------------------------------------------------------------------
