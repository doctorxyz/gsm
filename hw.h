// hw.h

#ifndef _HW_H_
#define _HW_H_

#ifdef __cplusplus
extern "C" {
#endif



/* quite useful */
void WaitForNextVRstart(int numvrs);
int TestVRstart();
void ClearVRcount();


void DmaReset();
void SendDma02(void *DmaTag);
void Dma02Wait();

#ifdef __cplusplus
}
#endif

#endif
