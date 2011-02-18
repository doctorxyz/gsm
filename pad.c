//---------------------------------------------------------------------------
// File name:   pad.c
//---------------------------------------------------------------------------

#include "pad.h"
#include "timer.h"

static char padBuf_t[2][256] __attribute__((aligned(64)));
struct padButtonStatus buttons_t[2];
s32 padtype_t[2] = {-1, -1}; //Flag controller ports not yet opened
u32 paddata, paddata_t[2];
u32 old_pad = 0, old_pad_t[2] = {0, 0};
u32 new_pad, new_pad_t[2];
u32 joy_flags = 0;
u32 joy_value = 0;
//---------------------------------------------------------------------------
// Wait for specific PAD, but also accept disconnected state
void waitPadReady(int port, int slot)
{
	int state, lastState;
	char stateString[16];

	state = padGetState(port, slot);
	lastState = -1;
	while((state != PAD_STATE_DISCONN)
		&& (state != PAD_STATE_STABLE)
		&& (state != PAD_STATE_FINDCTP1)){
		if (state != lastState)
			padStateInt2String(state, stateString);
		lastState = state;
		state=padGetState(port, slot);
	}
}
//---------------------------------------------------------------------------
// Wait for any PAD, but also accept disconnected states
void waitAnyPadReady(void)
{
	int state_1, state_2;

	state_1 = padGetState(0, 0);
	state_2 = padGetState(1, 0);
	while((state_1 != PAD_STATE_DISCONN) && (state_2 != PAD_STATE_DISCONN)
		&& (state_1 != PAD_STATE_STABLE) && (state_2 != PAD_STATE_STABLE)
		&& (state_1 != PAD_STATE_FINDCTP1) && (state_2 != PAD_STATE_FINDCTP1)){
		state_1 = padGetState(0, 0);
		state_2 = padGetState(1, 0);
	}
}
//---------------------------------------------------------------------------
// setup one PAD
int setup1Pad(port)
{
	int i, state, modes;

	if(padtype_t[port] < 0){                          //if port not yet opened
		if(padPortOpen(port, 0, &padBuf_t[port][0]) != 1) //if open still fails
			return 0;                                         //then abort attempt
	}

	padtype_t[port] = 0; //mark port open, but assume no controller connected
	waitPadReady(port, 0);
	state = padGetState(port, 0);
	if(state != PAD_STATE_DISCONN){ //if anything connected to this port
		modes = padInfoMode(port, 0, PAD_MODETABLE, -1);
		if (modes != 0){ //modes != 0, so it may be a dualshock type
			for(i=0; i<modes; i++){
				if (padInfoMode(port, 0, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK){
					padtype_t[port] = 2; //flag normal PS2 controller
					break;
				}
			} //ends for (modes)
		} else { //modes == 0, so this is a digital controller
			padtype_t[port] = 1; //flag digital controller
		}
	}
	if(padtype_t[port] == 2)                                      //if DS
		padSetMainMode(port, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK); //Set DS
	else                                                          //else
		padSetMainMode(port, 0, PAD_MMODE_DIGITAL, PAD_MMODE_UNLOCK); //Set Dig
	waitPadReady(port, 0);                                        //Completion
	return 1;
}//ends setup1Pad(port)
//---------------------------------------------------------------------------
// setup any PAD
int setupPad(void)
{
	int ret, port;

	padInit(0);

	ret = 0;
	for(port=0; port<2; port++){
		ret |= setup1Pad(port);
	} //ends for (port)
	return ret;
}//ends setupPad()
//---------------------------------------------------------------------------
// read PAD, without KB, and allow no auto-repeat. This is needed in code
// that is used regardless of VSync cycles, and where KB is not wanted.
//---------------------------------------------------------------------------
int readpad_noKBnoRepeat(void)
{
	int port, state, ret[2];

	for(port=0; port<2; port++){
		if((state=padGetState(port, 0))==PAD_STATE_STABLE
			||(state == PAD_STATE_FINDCTP1)){
			//Deal with cases where pad state is valid for padRead
			ret[port] = padRead(port, 0, &buttons_t[port]);
			if (ret[port] != 0){
				paddata_t[port] = 0xffff ^ buttons_t[port].btns;
				new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
				old_pad_t[port] = paddata_t[port];
			}
		}else{
			//Deal with cases where pad state is not valid for padRead
			new_pad_t[port]=0;
		}  //ends 'if' testing for state valid for padRead
	}  //ends for
	new_pad = new_pad_t[0]|new_pad_t[1];
	paddata = paddata_t[0]|paddata_t[1];
	return (ret[0]|ret[1]);
}
//------------------------------
//endfunc readpad_noKBnoRepeat
//---------------------------------------------------------------------------
// read PAD, but ignore KB. This is needed in code with own KB handlers,
// such as the virtual keyboard input routines for 'Rename' and 'New Dir'
// In addition, this routine also scans the analog joysticks
// A new addition is to also recheck for gamepad after failed access
//---------------------------------------------------------------------------
int readpad_no_KB(void)
{
	static u64 rpt_time[2]={0,0};
	static int rpt_count[2];
	static u64 work_time[2]={0, 0};
	int port, ret[2];
	int state = 0;

	for(port=0; port<2; port++){
		if((padtype_t[port] != 0)
			&&(((state=padGetState(port, 0))==PAD_STATE_STABLE)
				||(state == PAD_STATE_FINDCTP1)
				)
			)
		{	//Deal with cases where pad state is valid for padRead
			work_time[port] = Timer();
			ret[port] = padRead(port, 0, &buttons_t[port]);
			if (ret[port] != 0){
				*((u16 *) &paddata_t[port]) = 0xffff ^ buttons_t[port].btns;
				if(padtype_t[port] == 2){//DualShock
					if(buttons_t[port].rjoy_h >= 0xbf){
						joy_flags = PAD_R3_H1;                   //RJ Right
						joy_value=buttons_t[port].rjoy_h-0xbf;
					}else if(buttons_t[port].rjoy_h <= 0x40){
						joy_flags = PAD_R3_H0;                   //RJ Left
						joy_value=-(buttons_t[port].rjoy_h-0x40);
					}else if(buttons_t[port].rjoy_v <= 0x40){
						joy_flags = PAD_R3_V0;                   //RJ Up
						joy_value=-(buttons_t[port].rjoy_v-0x40);
					}else if(buttons_t[port].rjoy_v >= 0xbf){
						joy_flags = PAD_R3_V1;                   //RJ Down
						joy_value=buttons_t[port].rjoy_v-0xbf;
					}else if(buttons_t[port].ljoy_h >= 0xbf){
						joy_flags = PAD_L3_H1;                   //LJ Right
						joy_value=buttons_t[port].ljoy_h-0xbf;
					}else if(buttons_t[port].ljoy_h <= 0x40){
						joy_flags = PAD_L3_H0;                   //LJ Left
						joy_value=-(buttons_t[port].ljoy_h-0x40);
					}else if(buttons_t[port].ljoy_v <= 0x40){
						joy_flags = PAD_L3_V0;                   //LJ Up
						joy_value=-(buttons_t[port].ljoy_v-0x40);
					}else if(buttons_t[port].ljoy_v >= 0xbf){
						joy_flags = PAD_L3_V1;                   //LJ Down
						joy_value=buttons_t[port].ljoy_v-0xbf;
					}else{
						joy_flags = 0;                  //if no joy, zero joy_flags
						joy_value = 0;                  //as well as the joy_value
					}
					paddata_t[port] = (paddata_t[port] & 0xFFFF) | joy_flags;
				}
				new_pad_t[port] = paddata_t[port] & ~old_pad_t[port];
				if(old_pad_t[port] == paddata_t[port]){
					//no change of pad data
					if(Timer() > rpt_time[port]){
						new_pad_t[port]=paddata_t[port]; //Accept repeated buttons as new
						rpt_time[port] = Timer() + 40; //Min delay = 40ms => 25Hz repeat
						if(rpt_count[port]++ < 20)
							rpt_time[port] += 43; //Early delays = 83ms => 12Hz repeat
					}
				}else{
					//pad data has changed !
					rpt_count[port] = 0;
					rpt_time[port] = Timer()+400; //Init delay = 400ms
					old_pad_t[port] = paddata_t[port];
				}
			}
		}else{
			//Deal with cases where pad state is not valid for padRead
			new_pad_t[port]=0;
			old_pad_t[port]=0;
			
			if((padtype_t[port] == 0) || (state == PAD_STATE_DISCONN)){
				if(Timer() > work_time[port]+2000){ //if > 2 seconds since last work
					setup1Pad(port);
					work_time[port] = Timer(); //fake work_time for this port
				}//ends if(Timer())
			}//ends if
		}//ends else clause for state invalid for padRead
	}//ends for
	new_pad = new_pad_t[0]|new_pad_t[1];
	paddata = paddata_t[0]|paddata_t[1];
	return (ret[0]|ret[1]);
}
//------------------------------
//endfunc readpad_no_KB
//---------------------------------------------------------------------------
// readpad will call readpad_no_KB, and if no new pad buttons are found, it
// will also attempt reading data from a USB keyboard, and map this as a
// virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad(void)
{
	int	ret;

	if((ret=readpad_no_KB()) && new_pad)
		return ret;

	return 0;
}
//------------------------------
//endfunc readpad
//---------------------------------------------------------------------------
// readpad_noRepeat calls readpad_noKBnoRepeat, and if no new pad buttons are
// found, it also attempts reading data from a USB keyboard, and map this as
// a virtual gamepad. (Very improvised and sloppy, but it should work fine.)
//---------------------------------------------------------------------------
int readpad_noRepeat(void)
{
	int	ret;

	if((ret=readpad_noKBnoRepeat()) && new_pad)
		return ret;

	return 0;
}
//------------------------------
//endfunc readpad_noRepeat
//---------------------------------------------------------------------------
// End of file: pad.c
//---------------------------------------------------------------------------
