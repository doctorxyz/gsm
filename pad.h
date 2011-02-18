#ifndef PAD_H
#define PAD_H

#include <tamtypes.h>
#include <libpad.h>

/* pad.c */
#define PAD_JOY   0xFF0000	//Any joystick axis
#define PAD_R3_V0 0x010000	//RJoy Up (Forward)
#define PAD_R3_V1 0x020000	//RJoy Down (Back)
#define PAD_R3_H0 0x040000	//RJoy Left
#define PAD_R3_H1 0x080000	//RJoy Right
#define PAD_L3_V0 0x100000	//LJoy Up (Forward)
#define PAD_L3_V1 0x200000	//LJoy Down (Back)
#define PAD_L3_H0 0x400000	//LJoy Left
#define PAD_L3_H1 0x800000	//LJoy Right
//NB: The low 16 bits are defined in <libpad.h>

extern u32 joy_flags; //The latest joystick flags, same as in paddata>>16
extern u32 joy_value;	//The latest joystick value, valid when paddata > 0xFFFF
extern u32 new_pad;   //All button bits NEW in the latest scan
extern u32 paddata;   //All button bits for buttons still pressed

int setupPad(void);
int readpad(void);
int readpad_no_KB(void);
int readpad_noRepeat(void);
void waitPadReady(int port, int slot);
void waitAnyPadReady(void);

#endif //PAD_H
