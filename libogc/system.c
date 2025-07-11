/*-------------------------------------------------------------

system.c -- OS functions and initialization

Copyright (C) 2004 - 2025
Michael Wiedenbauer (shagkur)
Dave Murphy (WinterMute)
Extrems' Corner.org

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1.	The origin of this software must not be misrepresented; you
must not claim that you wrote the original software. If you use
this software in a product, an acknowledgment in the product
documentation would be appreciated but is not required.

2.	Altered source versions must be plainly marked as such, and
must not be misrepresented as being the original software.

3.	This notice may not be removed or altered from any source
distribution.

-------------------------------------------------------------*/

//#define DEBUG_SYSTEM

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <sys/iosupport.h>

#include "asm.h"
#include "irq.h"
#include "exi.h"
#if defined(HW_RVL)
#include "ipc.h"
#include "ios.h"
#include "stm.h"
#include "es.h"
#include "conf.h"
#include "wiilaunch.h"
#endif
#include "cache.h"
#include "video.h"
#include "system.h"
#include "sys_state.h"
#include "lwp_threads.h"
#include "lwp_priority.h"
#include "lwp_watchdog.h"
#include "lwp_wkspace.h"
#include "libversion.h"

#define SYSMEM1_SIZE				0x01800000
#if defined(HW_RVL)
#define SYSMEM2_SIZE				0x04000000
#endif
#define KERNEL_HEAP					(1*1024*1024)

// DSPCR bits
#define DSPCR_DSPRESET			    0x0800        // Reset DSP
#define DSPCR_DSPDMA				    0x0200        // ARAM dma in progress, if set
#define DSPCR_DSPINTMSK			    0x0100        // * interrupt mask   (RW)
#define DSPCR_DSPINT			    0x0080        // * interrupt active (RWC)
#define DSPCR_ARINTMSK			    0x0040
#define DSPCR_ARINT				    0x0020
#define DSPCR_AIINTMSK			    0x0010
#define DSPCR_AIINT				    0x0008
#define DSPCR_HALT				    0x0004        // halt DSP
#define DSPCR_PIINT				    0x0002        // assert DSP PI interrupt
#define DSPCR_RES				    0x0001        // reset DSP

#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))
#define _SHIFTR(v, s, w)	\
    ((u32)(((u32)(v) >> (s)) & ((0x01 << (w)) - 1)))

struct _sramcntrl {
	u8 srambuf[64];
	u32 offset;
	s32 enabled;
	s32 locked;
	s32 sync;
} sramcntrl ATTRIBUTE_ALIGN(32);

typedef struct _yay0header {
	unsigned int id ATTRIBUTE_PACKED;
	unsigned int dec_size ATTRIBUTE_PACKED;
	unsigned int links_offset ATTRIBUTE_PACKED;
	unsigned int chunks_offset ATTRIBUTE_PACKED;
} yay0header;

static u16 sys_fontenc = 0xffff;
static u32 sys_fontcharsinsheet = 0;
static u8 *sys_fontwidthtab = NULL;
static u8 *sys_fontimage = NULL;
static sys_fontheader *sys_fontdata = NULL;

static lwp_queue sys_reset_func_queue;
static u32 system_initialized = 0;

static void *__sysarena1lo = NULL;
static void *__sysarena1hi = NULL;

#if defined(HW_RVL)
static void *__sysarena2lo = NULL;
static void *__sysarena2hi = NULL;
static void *__ipcbufferlo = NULL;
static void *__ipcbufferhi = NULL;
#endif

static void __RSWDefaultHandler(void);
static resetcallback __RSWCallback = NULL;
#if defined(HW_RVL)
static void __POWDefaultHandler(void);
static powercallback __POWCallback = NULL;

static u32 __sys_resetdown = 0;
#endif

static vu16* const _viReg = (u16*)0xCC002000;
static vu32* const _piReg = (u32*)0xCC003000;
static vu16* const _memReg = (u16*)0xCC004000;
static vu16* const _dspReg = (u16*)0xCC005000;

#if defined(HW_DOL)
void __SYS_DoHotReset(u32 reset_code) __attribute__((noreturn));
#endif
void __SYS_ReadROM(void *buf,u32 len,u32 offset);

static s32 __sram_sync(void);
static s32 __sram_writecallback(s32 chn,s32 dev);
static s32 __mem_onreset(s32 final);

extern void __lwp_thread_coreinit(void);
extern void __lwp_sysinit(void);
extern void __lwp_syswd_init(void);
extern void __exception_init(void);
extern void __exception_closeall(void);
extern void __systemcall_init(void);
extern void __decrementer_init(void);
extern void __lwp_mutex_init(void);
extern void __lwp_cond_init(void);
extern void __lwp_mqbox_init(void);
extern void __lwp_sema_init(void);
extern void __exi_init(void);
extern void __si_init(void);
extern void __irq_init(void);
extern void __memlock_init(void);
extern void __libc_init(int);

const void *__libogc_malloc_lock = __syscall_malloc_lock;
const void *__libogc_malloc_unlock = __syscall_malloc_unlock;

extern void __realmode(void(*)(void));
extern void __configMEM1_16MB(void);
extern void __configMEM1_24MB(void);
extern void __configMEM1_32MB(void);
extern void __configMEM1_48MB(void);
extern void __configMEM1_64MB(void);
#if defined(HW_RVL)
extern void __configMEM2_64MB(void);
extern void __configMEM2_128MB(void);
#elif defined(HW_DOL)
extern void __reset(u32 reset_code) __attribute__((noreturn));
#endif

extern u32 __IPC_ClntInit(void);
extern u32 __PADDisableRecalibration(s32 disable);

extern void __console_init_ex(void *conbuffer,int tgt_xstart,int tgt_ystart,int tgt_stride,int con_xres,int con_yres,int con_stride);


const void *__libogc_lock_init = __syscall_lock_init;
const void *__libogc_lock_init_recursive = __syscall_lock_init_recursive;
const void *__libogc_lock_close = __syscall_lock_close;
const void *__libogc_lock_acquire = __syscall_lock_acquire;
const void *__libogc_lock_try_acquire = __syscall_lock_try_acquire;
const void *__libogc_lock_release = __syscall_lock_release;
const void *__libogc_exit = __syscall_exit;
extern void *_sbrk_r(struct _reent *ptr, ptrdiff_t incr);
const void *__libogc_sbrk_r = _sbrk_r;
const void *__libogc_gettod_r = __syscall_gettod_r;
const void *__libogc_clock_gettime = __syscall_clock_gettime;
const void *__libogc_clock_settime = __syscall_clock_settime;
const void *__libogc_clock_getres = __syscall_clock_getres;
const void *__libogc_nanosleep = __syscall_nanosleep;

extern u8 __isIPL[];
extern u8 __Arena1Lo[], __Arena1Hi[];
#if defined(HW_RVL)
extern u8 __Arena2Lo[], __Arena2Hi[];
extern u8 __ipcbufferLo[], __ipcbufferHi[];
#endif

void *__argvArena1Lo = (void*)0xdeadbeef;

static u32 __sys_inIPL = (u32)__isIPL;

static const u32 _dsp_initcode[] =
{
	0x029F0010,0x029F0033,0x029F0034,0x029F0035,
	0x029F0036,0x029F0037,0x029F0038,0x029F0039,
	0x12061203,0x12041205,0x00808000,0x0088FFFF,
	0x00841000,0x0064001D,0x02180000,0x81001C1E,
	0x00441B1E,0x00840800,0x00640027,0x191E0000,
	0x00DEFFFC,0x02A08000,0x029C0028,0x16FC0054,
	0x16FD4348,0x002102FF,0x02FF02FF,0x02FF02FF,
	0x02FF02FF,0x00000000,0x00000000,0x00000000
};

static sys_resetinfo mem_resetinfo = {
	{},
	__mem_onreset,
	127
};

static const char *__sys_versiondate;
static const char *__sys_versionbuild;

static void (*reload)(void) = (void(*)(void))0x80001800;

static bool __stub_found(void)
{
#if defined(HW_DOL)
	u32 *sig = (u32*)0x80001804;
	if ((*sig++ == 0x53545542 || *sig++ == 0x53545542) && *sig == 0x48415858)
		return true;
#else
	u64 *sig = (u64*)0x80001804;
	if (*sig == 0x5354554248415858ULL) // 'STUBHAXX'
		return true;
#endif
	return false;
}

void __reload(void)
{
	if(__stub_found()) {
		__exception_closeall();
		reload();
	}
#if defined(HW_DOL)
	__SYS_DoHotReset(0);
#else
	STM_RebootSystem();
#endif
}

void __syscall_exit(int rc)
{
	if(__stub_found()) {
		SYS_ResetSystem(SYS_SHUTDOWN, 0, FALSE);
		__lwp_thread_stopmultitasking(reload);
	}
#if defined(HW_DOL)
	SYS_ResetSystem(SYS_HOTRESET, 0, FALSE);
#else
	SYS_ResetSystem(SYS_RETURNTOMENU, 0, FALSE);
#endif
}

static s32 __mem_onreset(s32 final)
{
	if(final==TRUE) {
		_memReg[8] = 255;
		__UnmaskIrq(IM_MEM0|IM_MEM1|IM_MEM2|IM_MEM3);
	}
	return 1;
}

#if defined(HW_DOL)
void __SYS_DoHotReset(u32 reset_code)
{
	u32 level;

	_CPU_ISR_Disable(level);
	_viReg[1] = 0;
	ICFlashInvalidate();
	__reset(reset_code<<3);
}
#endif

static s32 __call_resetfuncs(s32 final)
{
	s32 ret;
	sys_resetinfo *info;
	lwp_queue *header = &sys_reset_func_queue;

	ret = 1;
	info = (sys_resetinfo*)header->first;
	while(info!=(sys_resetinfo*)__lwp_queue_tail(header)) {
		if(info->func && info->func(final)==0) ret |= (ret<<1);
		info = (sys_resetinfo*)info->node.next;
	}
	if(__sram_sync()==0) ret |= (ret<<1);

	if(ret&~0x01) return 0;
	return 1;
}

#if defined(HW_DOL)
static void __doreboot(u32 resetcode,s32 force_menu)
{
	u32 level;

	_CPU_ISR_Disable(level);

	*((u32*)0x817ffffc) = 0;
	*((u32*)0x817ffff8) = 0;
	*((u8*)0x800030e2) = 1;
}
#endif

static void __MEMInterruptHandler(u32 irq,frame_context *ctx)
{
	_memReg[16] = 0;
}

static void __RSWDefaultHandler(void)
{

}

#if defined(HW_RVL)
static void __POWDefaultHandler(void)
{
}
#endif

#if defined(HW_DOL)
static void __RSWHandler(u32 irq,frame_context *ctx)
{
	s64 now;
	static s64 hold_down = 0;

	hold_down = gettime();
	do {
		now = gettime();
		if(diff_usec(hold_down,now)>=100) break;
	} while(!(_piReg[0]&0x10000));

	if(!(_piReg[0]&0x10000)) {
		if(__RSWCallback) {
			__RSWCallback();
		}
	}
	_piReg[0] = 2;
}
#endif

#if defined(HW_RVL)
static void __STMEventHandler(u32 event)
{
	s32 ret;
	u32 level;

	if(event==STM_EVENT_RESET) {
		ret = SYS_ResetButtonDown();
		if(ret) {
			_CPU_ISR_Disable(level);
			__sys_resetdown = 1;
			if(__RSWCallback) {
				__RSWCallback();
			}
			_CPU_ISR_Restore(level);
		}
	}

	if(event==STM_EVENT_POWER) {
		_CPU_ISR_Disable(level);
		__POWCallback();
		_CPU_ISR_Restore(level);
	}
}
#endif

void *__attribute__((weak)) __myArena1Lo = NULL;
void *__attribute__((weak)) __myArena1Hi = NULL;
#if defined(HW_RVL)
void *__attribute__((weak)) __myArena2Lo = NULL;
void *__attribute__((weak)) __myArena2Hi = NULL;
#endif

static void __sysarena_init(void)
{
	if (__myArena1Lo == NULL && __argvArena1Lo != (void*)0xdeadbeef)
		__myArena1Lo = __argvArena1Lo;
#if defined(HW_DOL)
	if (__myArena1Lo == NULL && *(void**)0x800000F4 != NULL)
		__myArena1Lo = *(void**)0x80000030;
	if (__myArena1Hi == NULL) __myArena1Hi = *(void**)0x80000038;
	if (__myArena1Hi == NULL) __myArena1Hi = *(void**)0x800000EC;
	if (__myArena1Hi == NULL) __myArena1Hi = *(void**)0x80000034;
#elif defined(HW_RVL)
	if (__myArena1Lo == NULL) __myArena1Lo = *(void**)0x8000310C;
	if (__myArena1Hi == NULL) __myArena1Hi = *(void**)0x80003110;
	if (__myArena2Lo == NULL) __myArena2Lo = *(void**)0x80003124;
	if (__myArena2Hi == NULL) __myArena2Hi = *(void**)0x80003128;
#endif

	if (__myArena1Lo == NULL) __myArena1Lo = __Arena1Lo;
	if (__myArena1Hi == NULL) __myArena1Hi = __Arena1Hi;
#if defined(HW_RVL)
	if (__myArena2Lo == NULL) __myArena2Lo = __Arena2Lo;
	if (__myArena2Hi == NULL) __myArena2Hi = __Arena2Hi;
#endif

	SYS_SetArena1Lo(__myArena1Lo);
	SYS_SetArena1Hi(__myArena1Hi);
#if defined(HW_RVL)
	SYS_SetArena2Lo(__myArena2Lo);
	SYS_SetArena2Hi(__myArena2Hi);
#endif
}

#if defined(HW_RVL)
static void __ipcbuffer_init(void)
{
	__ipcbufferlo = *(void**)0x80003130;
	__ipcbufferhi = *(void**)0x80003134;

	if((__ipcbufferhi - __ipcbufferlo) == 0) {
		__ipcbufferlo = __ipcbufferLo;
		__ipcbufferhi = __ipcbufferHi;
	}
}
#endif

static void __memprotect_init(void)
{
	u32 level,size;

	_CPU_ISR_Disable(level);

	__MaskIrq((IM_MEM0|IM_MEM1|IM_MEM2|IM_MEM3));

	_memReg[16] = 0;
	_memReg[8] = 255;

	IRQ_Request(IRQ_MEM0,__MEMInterruptHandler);
	IRQ_Request(IRQ_MEM1,__MEMInterruptHandler);
	IRQ_Request(IRQ_MEM2,__MEMInterruptHandler);
	IRQ_Request(IRQ_MEM3,__MEMInterruptHandler);
	IRQ_Request(IRQ_MEMADDRESS,__MEMInterruptHandler);

	SYS_RegisterResetFunc(&mem_resetinfo);

	size = SYS_GetSimulatedMem1Size();
	if(size<=0x01000000) __realmode(__configMEM1_16MB);
	else if(size<=0x01800000) __realmode(__configMEM1_24MB);
	else if(size<=0x02000000) __realmode(__configMEM1_32MB);
	else if(size<=0x03000000) __realmode(__configMEM1_48MB);
	else if(size<=0x04000000) __realmode(__configMEM1_64MB);

#if defined(HW_RVL)
	size = SYS_GetSimulatedMem2Size();
	if(size<=0x04000000) __realmode(__configMEM2_64MB);
	else if(size<=0x08000000) __realmode(__configMEM2_128MB);
#endif

	__UnmaskIrq(IM_MEMADDRESS);		//only enable memaddress irq atm

	_CPU_ISR_Restore(level);
}

static __inline__ u32 __get_fontsize(void *buffer)
{
	u8 *ptr = (u8*)buffer;

	if(ptr[0]=='Y' && ptr[1]=='a' && ptr[2]=='y') return (((u32*)ptr)[1]);
	else return 0;
}

static u32 __read_rom(void *buf,u32 len,u32 offset)
{
	u32 ret;
	u32 loff;

	if(EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,NULL)==0) return 0;
	if(EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED32MHZ)==0) {
		EXI_Unlock(EXI_CHANNEL_0);
		return 0;
	}

	ret = 0;
	loff = offset<<6;
	if(EXI_ImmEx(EXI_CHANNEL_0,&loff,4,EXI_WRITE)==0) ret |= 0x01;
	if(EXI_DmaEx(EXI_CHANNEL_0,buf,len,EXI_READ)==0) ret |= 0x02;
	if(EXI_Deselect(EXI_CHANNEL_0)==0) ret |= 0x04;
	EXI_Unlock(EXI_CHANNEL_0);

	if(ret) return 0;
	return 1;
}

static u32 __getrtc(u32 *gctime)
{
	u32 ret;
	u32 cmd;

	if(EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,NULL)==0) return 0;
	if(EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED8MHZ)==0) {
		EXI_Unlock(EXI_CHANNEL_0);
		return 0;
	}

	ret = 0;
	cmd = 0x20000000;
	if(EXI_ImmEx(EXI_CHANNEL_0,&cmd,4,EXI_WRITE)==0) ret |= 0x01;
	if(EXI_ImmEx(EXI_CHANNEL_0,gctime,4,EXI_READ)==0) ret |= 0x02;
	if(EXI_Deselect(EXI_CHANNEL_0)==0) ret |= 0x04;
	EXI_Unlock(EXI_CHANNEL_0);

	if(ret) return 0;
	return 1;
}

static u32 __sram_read(void *buffer)
{
	u32 command,ret;

	DCInvalidateRange(buffer,64);

	if(EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,NULL)==0) return 0;
	if(EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED8MHZ)==0) {
		EXI_Unlock(EXI_CHANNEL_0);
		return 0;
	}

	ret = 0;
	command = 0x20000100;
	if(EXI_ImmEx(EXI_CHANNEL_0,&command,4,EXI_WRITE)==0) ret |= 0x01;
	if(EXI_Dma(EXI_CHANNEL_0,buffer,64,EXI_READ,NULL)==0) ret |= 0x02;
	if(EXI_Sync(EXI_CHANNEL_0)==0) ret |= 0x04;
	if(EXI_Deselect(EXI_CHANNEL_0)==0) ret |= 0x08;
	EXI_Unlock(EXI_CHANNEL_0);

	if(ret) return 0;
	return 1;
}

static u32 __sram_write(void *buffer,u32 loc,u32 len)
{
	u32 cmd,ret;

	if(EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,__sram_writecallback)==0) return 0;
	if(EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED8MHZ)==0) {
		EXI_Unlock(EXI_CHANNEL_0);
		return 0;
	}

	ret = 0;
	cmd = 0xa0000100+(loc<<6);
	if(EXI_ImmEx(EXI_CHANNEL_0,&cmd,4,EXI_WRITE)==0) ret |= 0x01;
	if(EXI_ImmEx(EXI_CHANNEL_0,buffer,len,EXI_WRITE)==0) ret |= 0x02;
	if(EXI_Deselect(EXI_CHANNEL_0)==0) ret |= 0x04;
	EXI_Unlock(EXI_CHANNEL_0);

	if(ret) return 0;
	return 1;
}

static s32 __sram_writecallback(s32 chn,s32 dev)
{
	sramcntrl.sync = __sram_write(sramcntrl.srambuf+sramcntrl.offset,sramcntrl.offset,(64-sramcntrl.offset));
	if(sramcntrl.sync) sramcntrl.offset = 64;

	return 1;
}

static s32 __sram_sync(void)
{
	return sramcntrl.sync;
}

void __sram_init(void)
{
	sramcntrl.enabled = 0;
	sramcntrl.locked = 0;
	sramcntrl.sync = __sram_read(sramcntrl.srambuf);

	sramcntrl.offset = 64;

	SYS_SetGBSMode(SYS_GetGBSMode());
}

static void DisableWriteGatherPipe(void)
{
	mthid2((mfhid2()&~0x40000000));
}

static void __buildchecksum(u16 *buffer,u16 *c1,u16 *c2)
{
	u32 i;

	*c1 = 0;
	*c2 = 0;
	for(i=0;i<4;i++) {
		*c1 += buffer[6+i];
		*c2 += buffer[6+i]^-1;
	}
}

static void* __locksram(u32 loc)
{
	u32 level;

	_CPU_ISR_Disable(level);
	if(!sramcntrl.locked) {
		sramcntrl.enabled = level;
		sramcntrl.locked = 1;
		return (void*)((u32)sramcntrl.srambuf+loc);
	}
	_CPU_ISR_Restore(level);
	return NULL;
}

static u32 __unlocksram(u32 write,u32 loc)
{
	syssram *sram = (syssram*)sramcntrl.srambuf;

	if(write) {
		if(!loc) {
			if(sram->lang>SYS_LANG_DUTCH) sram->lang = SYS_LANG_ENGLISH;
			if((sram->flags&0x03)>SYS_VIDEO_MPAL) sram->flags = (sram->flags&~0x03)|(SYS_VIDEO_NTSC&0x03);
			__buildchecksum((u16*)sramcntrl.srambuf,&sram->checksum,&sram->checksum_inv);
		}
		if(loc<sramcntrl.offset) sramcntrl.offset = loc;

		sramcntrl.sync = __sram_write(sramcntrl.srambuf+sramcntrl.offset,sramcntrl.offset,(64-sramcntrl.offset));
		if(sramcntrl.sync) sramcntrl.offset = 64;
	}
	sramcntrl.locked = 0;
	_CPU_ISR_Restore(sramcntrl.enabled);
	return sramcntrl.sync;
}

//returns the size of font
static u32 __read_font(void *buffer)
{
	if(SYS_GetFontEncoding()==SYS_FONTENC_SJIS) __SYS_ReadROM(buffer,315392,1769216);
	else __SYS_ReadROM(buffer,12288,2084608);
	return __get_fontsize(buffer);
}

static void __expand_font(const u8 *src,u8 *dest)
{
	s32 cnt;
	u32 idx;
	u8 val1,val2;
	u8 *data = (u8*)sys_fontdata+44;

	if(sys_fontdata->sheet_format==0x0000) {
		cnt = (sys_fontdata->sheet_fullsize/2)-1;

		while(cnt>=0) {
			idx = _SHIFTR(src[cnt],6,2);
			val1 = data[idx];

			idx = _SHIFTR(src[cnt],4,2);
			val2 = data[idx];

			dest[(cnt<<1)+0] =((val1&0xf0)|(val2&0x0f));

			idx = _SHIFTR(src[cnt],2,2);
			val1 = data[idx];

			idx = _SHIFTR(src[cnt],0,2);
			val2 = data[idx];

			dest[(cnt<<1)+1] =((val1&0xf0)|(val2&0x0f));

			cnt--;
		}
	}
	DCStoreRange(dest,sys_fontdata->sheet_fullsize);
}

static void __dsp_bootstrap(void)
{
	u16 status;
	u32 tick;

	memcpy(SYS_GetArenaHi()-128,(void*)0x81000000,128);
	memcpy((void*)0x81000000,_dsp_initcode,128);
	DCFlushRange((void*)0x81000000,128);

	_dspReg[9] = 0x43;
	_dspReg[5] = (DSPCR_DSPRESET|DSPCR_DSPINT|DSPCR_ARINT|DSPCR_AIINT|DSPCR_HALT);
	_dspReg[5] |= DSPCR_RES;
	while(_dspReg[5]&DSPCR_RES);

	_dspReg[0] = 0;
	while((_SHIFTL(_dspReg[2],16,16)|(_dspReg[3]&0xffff))&0x80000000);

	((vu32*)_dspReg)[8] = 0x01000000;
	((vu32*)_dspReg)[9] = 0;
	((vu32*)_dspReg)[10] = 32;

	status = _dspReg[5];
	while(!(status&DSPCR_ARINT)) status = _dspReg[5];
	_dspReg[5] = status;

	tick = gettick();
	while((gettick()-tick)<2194);

	((vu32*)_dspReg)[8] = 0x01000000;
	((vu32*)_dspReg)[9] = 0;
	((vu32*)_dspReg)[10] = 32;

	status = _dspReg[5];
	while(!(status&DSPCR_ARINT)) status = _dspReg[5];
	_dspReg[5] = status;

	_dspReg[5] &= ~DSPCR_DSPRESET;
	while(_dspReg[5]&0x400);

	_dspReg[5] &= ~DSPCR_HALT;
	while(!(_dspReg[2]&0x8000));
	status = _dspReg[3];

	_dspReg[5] |= DSPCR_HALT;
	_dspReg[5] = (DSPCR_DSPRESET|DSPCR_DSPINT|DSPCR_ARINT|DSPCR_AIINT|DSPCR_HALT);
	_dspReg[5] |= DSPCR_RES;
	while(_dspReg[5]&DSPCR_RES);

	memcpy((void*)0x81000000,SYS_GetArenaHi()-128,128);
#ifdef _SYS_DEBUG
	printf("__audiosystem_init(finish)\n");
#endif
}

void __dsp_shutdown(void)
{
	u32 tick;

	_dspReg[5] = (DSPCR_DSPRESET|DSPCR_HALT);
	_dspReg[27] &= ~0x8000;
	while(_dspReg[5]&0x400);
	while(_dspReg[5]&0x200);

	_dspReg[5] = (DSPCR_DSPRESET|DSPCR_DSPINT|DSPCR_ARINT|DSPCR_AIINT|DSPCR_HALT);
	_dspReg[0] = 0;
	while((_SHIFTL(_dspReg[2],16,16)|(_dspReg[3]&0xffff))&0x80000000);

	tick = gettick();
	while((gettick()-tick)<44);

	_dspReg[5] |= DSPCR_RES;
	while(_dspReg[5]&DSPCR_RES);
}

static void decode_szp(void *src,void *dest)
{
	u32 i,k,link;
	u8 *dest8,*tmp;
	u32 loff,coff,roff;
	u32 size,cnt,cmask,bcnt;
	yay0header *header;

	dest8 = (u8*)dest;
	header = (yay0header*)src;
	size = header->dec_size;
	loff = header->links_offset;
	coff = header->chunks_offset;

	roff = sizeof(yay0header);
	cmask = 0;
	cnt = 0;
	bcnt = 0;

	do {
		if(!bcnt) {
			cmask = *(u32*)(src+roff);
			roff += 4;
			bcnt = 32;
		}

		if(cmask&0x80000000) {
			dest8[cnt++] = *(u8*)(src+coff);
			coff++;
		} else {
			link = *(u16*)(src+loff);
			loff += 2;

			tmp = dest8+(cnt-(link&0x0fff)-1);
			k = link>>12;
			if(k==0) {
				k = (*(u8*)(src+coff))+18;
				coff++;
			} else k += 2;

			for(i=0;i<k;i++) {
				dest8[cnt++] = tmp[i];
			}
		}
		cmask <<= 1;
		bcnt--;
	} while(cnt<size);
}

syssram* __SYS_LockSram(void)
{
	return (syssram*)__locksram(0);
}

syssramex* __SYS_LockSramEx(void)
{
	return (syssramex*)__locksram(sizeof(syssram));
}

u32 __SYS_UnlockSram(u32 write)
{
	return __unlocksram(write,0);
}

u32 __SYS_UnlockSramEx(u32 write)
{
	return __unlocksram(write,sizeof(syssram));
}

u32 __SYS_SyncSram(void)
{
	return __sram_sync();
}

u32 __SYS_CheckSram(void)
{
	u16 checksum,checksum_inv;
	syssram *sram = (syssram*)sramcntrl.srambuf;

	__buildchecksum((u16*)sramcntrl.srambuf,&checksum,&checksum_inv);

	if(sram->checksum!=checksum || sram->checksum_inv!=checksum_inv) return 0;
	return 1;
}

void __SYS_ReadROM(void *buf,u32 len,u32 offset)
{
	u32 cpy_cnt;

	while(len>0) {
		cpy_cnt = 1024-(offset%1024);
		cpy_cnt = (len>cpy_cnt)?cpy_cnt:len;
		while(__read_rom(buf,cpy_cnt,offset)==0);
		offset += cpy_cnt;
		buf += cpy_cnt;
		len -= cpy_cnt;
	}
}

u32 __SYS_GetRTC(u32 *gctime)
{
	u32 cnt,ret;
	u32 time1,time2;

	cnt = 0;
	ret = 0;
	while(cnt<16) {
		if(__getrtc(&time1)==0) ret |= 0x01;
		if(__getrtc(&time2)==0) ret |= 0x02;
		if(ret) return 0;
		if(time1==0xffffffff) return 0;
		if(time2==0xffffffff) return 0;
		if(time1==time2) {
			*gctime = time1;
			return 1;
		}
		cnt++;
	}
	return 0;
}

u32 __SYS_SetRTC(u32 gctime)
{
	u32 cmd,ret;

	if(EXI_Lock(EXI_CHANNEL_0,EXI_DEVICE_1,NULL)==0) return 0;
	if(EXI_Select(EXI_CHANNEL_0,EXI_DEVICE_1,EXI_SPEED8MHZ)==0) {
		EXI_Unlock(EXI_CHANNEL_0);
		return 0;
	}

	ret = 0;
	cmd = 0xa0000000;
	if(EXI_ImmEx(EXI_CHANNEL_0,&cmd,4,EXI_WRITE)==0) ret |= 0x01;
	if(EXI_ImmEx(EXI_CHANNEL_0,&gctime,4,EXI_WRITE)==0) ret |= 0x02;
	if(EXI_Deselect(EXI_CHANNEL_0)==0) ret |= 0x04;
	EXI_Unlock(EXI_CHANNEL_0);

	if(ret) return 0;
	return 1;
}

void __SYS_SetTime(s64 time)
{
	u32 level;
	s64 now;
	s64 *pBootTime = (s64*)0x800030d8;

	_CPU_ISR_Disable(level);
	now = gettime();
	now -= time;
	now += *pBootTime;
	*pBootTime = now;
	settime(time);
	_CPU_ISR_Restore(level);
}

s64 __SYS_GetSystemTime(void)
{
	u32 level;
	s64 now;
	s64 *pBootTime = (s64*)0x800030d8;

	_CPU_ISR_Disable(level);
	now = gettime();
	now += *pBootTime;
	_CPU_ISR_Restore(level);
	return now;
}

void __SYS_SetBootTime(void)
{
	u32 gctime;
#if defined(HW_RVL)
	u32 bias;
#endif

	if(__SYS_GetRTC(&gctime)==1) {
		if(getenv("TZ")==NULL) {
#if defined(HW_RVL)
			if(CONF_GetCounterBias(&bias)==0) gctime += bias;
#else
			gctime += SYS_GetCounterBias();
#endif
		}
		__SYS_SetTime(secs_to_ticks(gctime));
	}
}

u32 __SYS_LoadFont(void *src,void *dest)
{
	if(__read_font(src)==0) return 0;

	decode_szp(src,dest);

	sys_fontdata = (sys_fontheader*)dest;
	sys_fontwidthtab = (u8*)dest+sys_fontdata->width_table;
	sys_fontcharsinsheet = sys_fontdata->sheet_column*sys_fontdata->sheet_row;

	/* TODO: implement SJIS handling */
	return 1;
}

#if defined(HW_RVL)
void* __SYS_GetIPCBufferLo(void)
{
	return __ipcbufferlo;
}

void* __SYS_GetIPCBufferHi(void)
{
	return __ipcbufferhi;
}

#endif

void _V_EXPORTNAME(void)
{ __sys_versionbuild = _V_STRING; __sys_versiondate = _V_DATE_; }

#if defined(HW_RVL)
void __SYS_DoPowerCB(void)
{
	u32 level;
	powercallback powcb;

	_CPU_ISR_Disable(level);
	powcb = __POWCallback;
	__POWCallback = __POWDefaultHandler;
	powcb();
	_CPU_ISR_Restore(level);
}
#endif

void __SYS_InitCallbacks(void)
{
#if defined(HW_RVL)
	__POWCallback = __POWDefaultHandler;
	__sys_resetdown = 0;
#endif
	__RSWCallback = __RSWDefaultHandler;
}

void __attribute__((weak)) __SYS_PreInit(void)
{

}

void SYS_Init(void)
{
	u32 level;

	if(system_initialized) return;
	system_initialized = 1;

	_CPU_ISR_Disable(level);
	__SYS_PreInit();

	_V_EXPORTNAME();

	__sysarena_init();
#if defined(HW_RVL)
	__ipcbuffer_init();
#endif
	__lwp_wkspace_init(KERNEL_HEAP);
	__lwp_queue_init_empty(&sys_reset_func_queue);
	__lwp_syswd_init();
	__sys_state_init();
	__lwp_priority_init();
	__lwp_watchdog_init();
	__exception_init();
	__systemcall_init();
	__decrementer_init();
	__irq_init();
	__exi_init();
	__sram_init();
	__si_init();
	__lwp_thread_coreinit();
	__lwp_sysinit();
	__memlock_init();
	__lwp_mqbox_init();
	__lwp_sema_init();
	__lwp_mutex_init();
	__lwp_cond_init();
	__dsp_bootstrap();

	if(!__sys_inIPL)
		__memprotect_init();

	DisableWriteGatherPipe();
	__SYS_InitCallbacks();
#if defined(HW_RVL)
	__IPC_ClntInit();
#elif defined(HW_DOL)
	IRQ_Request(IRQ_PI_RSW,__RSWHandler);
	__MaskIrq(IRQMASK(IRQ_PI_RSW));
#endif
	__libc_init(1);
	__lwp_thread_startmultitasking();
	_CPU_ISR_Restore(level);
}

// This function gets called inside the main thread, prior to the application's main() function
void SYS_PreMain(void)
{
#if defined(HW_DOL)
	__SYS_SetBootTime();
#elif defined(HW_RVL)
	u32 i;

	for (i = 0; i < 32; ++i)
		IOS_Close(i);

	__IOS_LoadStartupIOS();
	__IOS_InitializeSubsystems();
	STM_RegisterEventHandler(__STMEventHandler);
	CONF_Init();
	__SYS_SetBootTime();
	WII_Initialize();
#endif
}

u32 SYS_ResetButtonDown(void)
{
	return _SHIFTR(_piReg[0],16,1)^1;
}

#if defined(HW_DOL)
void SYS_ResetSystem(s32 reset,u32 reset_code,s32 force_menu)
{
	u32 ret = 0;
	syssram *sram;

	__dsp_shutdown();

	if(reset==SYS_SHUTDOWN) {
		ret = __PADDisableRecalibration(TRUE);
	}

	while(__call_resetfuncs(FALSE)==0);

	if((reset==SYS_HOTRESET && force_menu==TRUE)
		|| reset==SYS_RETURNTOMENU) {
		sram = __SYS_LockSram();
		sram->flags |= 0x40;
		__SYS_UnlockSram(TRUE);
		while(!__SYS_SyncSram());
	}

	__exception_closeall();
	__call_resetfuncs(TRUE);

	LCDisable();

	__lwp_thread_dispatchdisable();
	if(reset==SYS_HOTRESET || reset==SYS_RETURNTOMENU) {
		__SYS_DoHotReset(reset_code);
	} else if(reset==SYS_RESTART) {
		__lwp_thread_closeall();
		__lwp_thread_dispatchunnest();
		__doreboot(reset_code,force_menu);
	}

	__lwp_thread_closeall();

	memset((void*)0x80000040,0,140);
	memset((void*)0x800000D4,0,20);
	memset((void*)0x800000F4,0,4);
	memset((void*)0x80003000,0,192);
	memset((void*)0x800030C8,0,12);
	memset((void*)0x800030E2,0,1);

	__PADDisableRecalibration(ret);
}
#elif defined(HW_RVL)
void SYS_ResetSystem(s32 reset,u32 reset_code,s32 force_menu)
{
	u32 ret = 0;

	__dsp_shutdown();

	if(reset==SYS_SHUTDOWN) {
		ret = __PADDisableRecalibration(TRUE);
	}

	while(__call_resetfuncs(FALSE)==0);

	switch(reset) {
		case SYS_HOTRESET:
			if(force_menu==TRUE) {
				WII_ReturnToMenu();
			} else {
				STM_RebootSystem();
			}
			break;
		case SYS_RETURNTOMENU:
			WII_ReturnToMenu();
			break;
		case SYS_POWEROFF:
			if(CONF_GetShutdownMode() == CONF_SHUTDOWN_IDLE) {
				ret = CONF_GetIdleLedMode();
				if(ret <= 2) STM_SetLedMode(ret);
				STM_ShutdownToIdle();
			} else {
				STM_ShutdownToStandby();
			}
			break;
		case SYS_POWEROFF_STANDBY:
			STM_ShutdownToStandby();
			break;
		case SYS_POWEROFF_IDLE:
			ret = CONF_GetIdleLedMode();
			if(ret <= 2) STM_SetLedMode(ret);
			STM_ShutdownToIdle();
			break;
	}

	//TODO: implement SYS_RESTART
	// either restart failed or this is SYS_SHUTDOWN

	__IOS_ShutdownSubsystems();

	__exception_closeall();
	__call_resetfuncs(TRUE);

	LCDisable();

	__lwp_thread_dispatchdisable();
	__lwp_thread_closeall();

	memset((void*)0x80000040,0,140);
	memset((void*)0x800000D4,0,20);
	memset((void*)0x800000F4,0,4);
	memset((void*)0x80003000,0,192);
	memset((void*)0x800030C8,0,12);
	memset((void*)0x800030E2,0,1);

	__PADDisableRecalibration(ret);
}
#endif

void SYS_RegisterResetFunc(sys_resetinfo *info)
{
	u32 level;
	sys_resetinfo *after;
	lwp_queue *header = &sys_reset_func_queue;

	_CPU_ISR_Disable(level);
	for(after=(sys_resetinfo*)header->first;after->node.next!=NULL && info->prio>=after->prio;after=(sys_resetinfo*)after->node.next);
	__lwp_queue_insertI(after->node.prev,&info->node);
	_CPU_ISR_Restore(level);
}

void SYS_UnregisterResetFunc(sys_resetinfo *info) {
	u32 level;
	lwp_node *n;

	_CPU_ISR_Disable(level);
	for (n = sys_reset_func_queue.first; n->next; n = n->next) {
		if (n == &info->node) {
			__lwp_queue_extractI(n);
			break;
		}
	}
	_CPU_ISR_Restore(level);
}

void SYS_SetArena1Lo(void *newLo)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__sysarena1lo = newLo;
	_CPU_ISR_Restore(level);
}

void* SYS_GetArena1Lo(void)
{
	u32 level;
	void *arenalo;

	_CPU_ISR_Disable(level);
	arenalo = __sysarena1lo;
	_CPU_ISR_Restore(level);

	return arenalo;
}

void SYS_SetArena1Hi(void *newHi)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__sysarena1hi = newHi;
	_CPU_ISR_Restore(level);
}

void* SYS_GetArena1Hi(void)
{
	u32 level;
	void *arenahi;

	_CPU_ISR_Disable(level);
	arenahi = __sysarena1hi;
	_CPU_ISR_Restore(level);

	return arenahi;
}

u32 SYS_GetArena1Size(void)
{
	u32 level,size;

	_CPU_ISR_Disable(level);
	size = ((u32)__sysarena1hi - (u32)__sysarena1lo);
	_CPU_ISR_Restore(level);

	return size;
}

void* SYS_AllocArenaMem1Lo(u32 size,u32 align)
{
	u32 level;
	void *arenalo,*ptr;

	_CPU_ISR_Disable(level);
	arenalo = __sysarena1lo;
	ptr = (void*)(((u32)arenalo+(align-1))&~(align-1));
	arenalo = (void*)(((u32)ptr+size+(align-1))&~(align-1));

	if(__sysarena1hi<arenalo) {
		_CPU_ISR_Restore(level);
		return NULL;
	}
	__sysarena1lo = arenalo;
	_CPU_ISR_Restore(level);
	return ptr;
}

void* SYS_AllocArenaMem1Hi(u32 size,u32 align)
{
	u32 level;
	void *arenahi,*ptr;

	_CPU_ISR_Disable(level);
	arenahi = (void*)(((u32)__sysarena1hi)&~(align-1));
	arenahi = ptr = (void*)(((u32)arenahi-size)&~(align-1));

	if(__sysarena1lo>arenahi) {
		_CPU_ISR_Restore(level);
		return NULL;
	}
	__sysarena1hi = arenahi;
	_CPU_ISR_Restore(level);
	return ptr;
}

#if defined(HW_DOL)
u32 SYS_GetPhysicalMem1Size(void)
{
	u32 size;
	size = *((u32*)0x80000028);
	if(!size) size = SYSMEM1_SIZE;
	return size;
}

u32 SYS_GetSimulatedMem1Size(void)
{
	u32 size;
	size = *((u32*)0x800000f0);
	if(!size) size = *((u32*)0x80000028);
	if(!size) size = SYSMEM1_SIZE;
	return size;
}
#elif defined(HW_RVL)
u32 SYS_GetPhysicalMem1Size(void)
{
	u32 size;
	size = *((u32*)0x80003100);
	if(!size) size = SYSMEM1_SIZE;
	return size;
}

u32 SYS_GetSimulatedMem1Size(void)
{
	u32 size;
	size = *((u32*)0x80003104);
	if(!size) size = SYSMEM1_SIZE;
	return size;
}

void SYS_SetArena2Lo(void *newLo)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__sysarena2lo = newLo;
	_CPU_ISR_Restore(level);
}

void* SYS_GetArena2Lo(void)
{
	u32 level;
	void *arenalo;

	_CPU_ISR_Disable(level);
	arenalo = __sysarena2lo;
	_CPU_ISR_Restore(level);

	return arenalo;
}

void SYS_SetArena2Hi(void *newHi)
{
	u32 level;

	_CPU_ISR_Disable(level);
	__sysarena2hi = newHi;
	_CPU_ISR_Restore(level);
}

void* SYS_GetArena2Hi(void)
{
	u32 level;
	void *arenahi;

	_CPU_ISR_Disable(level);
	arenahi = __sysarena2hi;
	_CPU_ISR_Restore(level);

	return arenahi;
}

u32 SYS_GetArena2Size(void)
{
	u32 level,size;

	_CPU_ISR_Disable(level);
	size = ((u32)__sysarena2hi - (u32)__sysarena2lo);
	_CPU_ISR_Restore(level);

	return size;
}

void* SYS_AllocArenaMem2Lo(u32 size,u32 align)
{
	u32 level;
	void *arenalo,*ptr;

	_CPU_ISR_Disable(level);
	arenalo = __sysarena2lo;
	ptr = (void*)(((u32)arenalo+(align-1))&~(align-1));
	arenalo = (void*)(((u32)ptr+size+(align-1))&~(align-1));

	if(__sysarena2hi<arenalo) {
		_CPU_ISR_Restore(level);
		return NULL;
	}
	__sysarena2lo = arenalo;
	_CPU_ISR_Restore(level);
	return ptr;
}

void* SYS_AllocArenaMem2Hi(u32 size,u32 align)
{
	u32 level;
	void *arenahi,*ptr;

	_CPU_ISR_Disable(level);
	arenahi = (void*)(((u32)__sysarena2hi)&~(align-1));
	arenahi = ptr = (void*)(((u32)arenahi-size)&~(align-1));

	if(__sysarena2lo>arenahi) {
		_CPU_ISR_Restore(level);
		return NULL;
	}
	__sysarena2hi = arenahi;
	_CPU_ISR_Restore(level);
	return ptr;
}

u32 SYS_GetPhysicalMem2Size(void)
{
	u32 size;
	size = *((u32*)0x80003118);
	if(!size) size = SYSMEM2_SIZE;
	return size;
}

u32 SYS_GetSimulatedMem2Size(void)
{
	u32 size;
	size = *((u32*)0x8000311c);
	if(!size) size = SYSMEM2_SIZE;
	return size;
}
#endif

void SYS_ProtectRange(u32 chan,void *addr,u32 bytes,u32 cntrl)
{
	u16 rcntrl;
	u32 pstart,pend,level;

	if(chan<SYS_PROTECTCHANMAX) {
		pstart = ((u32)addr)&~0x3ff;
		pend = ((((u32)addr)+bytes)+1023)&~0x3ff;
		DCFlushRange((void*)pstart,(pend-pstart));

		_CPU_ISR_Disable(level);

		__UnmaskIrq(IRQMASK(chan));
		_memReg[chan<<2] = _SHIFTR(pstart,10,16);
		_memReg[(chan<<2)+1] = _SHIFTR(pend,10,16);

		rcntrl = _memReg[8];
		rcntrl = (rcntrl&~(_SHIFTL(3,(chan<<1),2)))|(_SHIFTL(cntrl,(chan<<1),2));
		_memReg[8] = rcntrl;

		if(cntrl==SYS_PROTECTRDWR)
			__MaskIrq(IRQMASK(chan));

		_CPU_ISR_Restore(level);
	}
}

bool SYS_IsDMAAddress(const void *addr,u32 align)
{
	if((u32)addr&(align-1)) return false;
#if defined(HW_RVL)
	if((u32)addr>=0xD0000000 && (u32)addr<0xE0000000) addr = MEM_K1_TO_K0(addr);
	if((u32)addr>=0x90000000 && (u32)addr<*(u32*)0x80003120) return true;
	if((u32)addr&0x03) return false;
	if((u32)addr>=0xC0000000 && (u32)addr<0xC4000000) addr = MEM_K1_TO_K0(addr);
	if((u32)addr>=0x80000000 && (u32)addr<*(u32*)0x80003108) return true;
#elif defined(HW_DOL)
	if((u32)addr>=0xC0000000 && (u32)addr<0xC4000000) return true;
	if((u32)addr>=0x80000000 && (u32)addr<0x84000000) return true;
#endif
	return false;
}

void* SYS_AllocateFramebuffer(const GXRModeObj *rmode)
{
	void *fb;
	u32 size;

	size = VIDEO_GetFrameBufferSize(rmode);
	fb = memalign(PPC_CACHE_ALIGNMENT,size);
	if(fb) DCInvalidateRange(fb,size);
	return fb;
}

u16 SYS_GetFontEncoding(void)
{
	u16 ret;
	u32 tv;

	if(sys_fontenc!=0xffff) return sys_fontenc;

	ret = SYS_FONTENC_ANSI;
	tv = *((u32*)0x800000cc);
	if(tv==VI_NTSC && _viReg[55]&0x0002) ret = SYS_FONTENC_SJIS;
	sys_fontenc = ret;
	return ret;
}

u16 SYS_SetFontEncoding(u16 enc)
{
	u16 ret;

	ret = SYS_GetFontEncoding();
	if(enc<=SYS_FONTENC_SJIS) sys_fontenc = enc;
	return ret;
}

u32 SYS_InitFont(sys_fontheader *font_data)
{
	void *packed_data = NULL;

	if(!font_data) return 0;

	if(SYS_GetFontEncoding()==SYS_FONTENC_SJIS) {
		memset(font_data,0,SYS_FONTSIZE_SJIS);
		packed_data = (void*)((u32)font_data+868096);
	} else {
		memset(font_data,0,SYS_FONTSIZE_ANSI);
		packed_data = (void*)((u32)font_data+119072);
	}

	if(__SYS_LoadFont(packed_data,font_data)==1) {
		sys_fontimage = (u8*)((((u32)font_data+font_data->sheet_image)+31)&~31);
		__expand_font((u8*)font_data+font_data->sheet_image,sys_fontimage);
		return 1;
	}

	return 0;
}

void SYS_GetFontTexture(s32 c,void **image,s32 *xpos,s32 *ypos,s32 *width)
{
	u32 sheets,rem;

	*xpos = 0;
	*ypos = 0;
	*image = NULL;
	if(!sys_fontwidthtab || ! sys_fontimage) return;

	if(c<sys_fontdata->first_char || c>sys_fontdata->last_char) c = sys_fontdata->inval_char;
	else c -= sys_fontdata->first_char;

	sheets = c/sys_fontcharsinsheet;
	rem = c%sys_fontcharsinsheet;
	*image = sys_fontimage+(sys_fontdata->sheet_size*sheets);
	*xpos = (rem%sys_fontdata->sheet_column)*sys_fontdata->cell_width;
	*ypos = (rem/sys_fontdata->sheet_column)*sys_fontdata->cell_height;
	*width = sys_fontwidthtab[c];
}

void SYS_GetFontTexel(s32 c,void *image,s32 pos,s32 stride,s32 *width)
{
	u32 sheets,rem;
	u32 xoff,yoff;
	u32 xpos,ypos;
	u8 *img_start;
	u8 *ptr1,*ptr2;

	if(!sys_fontwidthtab || ! sys_fontimage) return;

	if(c<sys_fontdata->first_char || c>sys_fontdata->last_char) c = sys_fontdata->inval_char;
	else c -= sys_fontdata->first_char;

	sheets = c/sys_fontcharsinsheet;
	rem = c%sys_fontcharsinsheet;
	xoff = (rem%sys_fontdata->sheet_column)*sys_fontdata->cell_width;
	yoff = (rem/sys_fontdata->sheet_column)*sys_fontdata->cell_height;
	img_start = sys_fontimage+(sys_fontdata->sheet_size*sheets);

	ypos = 0;
	while(ypos<sys_fontdata->cell_height) {
		xpos = 0;
		while(xpos<sys_fontdata->cell_width) {
			ptr1 = img_start+(((sys_fontdata->sheet_width/8)<<5)*((ypos+yoff)/8));
			ptr1 = ptr1+(((xpos+xoff)/8)<<5);
			ptr1 = ptr1+(((ypos+yoff)%8)<<2);
			ptr1 = ptr1+(((xpos+xoff)%8)/2);

			ptr2 = image+((ypos/8)*(((stride<<1)/8)<<5));
			ptr2 = ptr2+(((xpos+pos)/8)<<5);
			ptr2 = ptr2+(((xpos+pos)%8)/2);
			ptr2 = ptr2+((ypos%8)<<2);

			*ptr2 = *ptr1;

			xpos += 2;
		}
		ypos++;
	}
	*width = sys_fontwidthtab[c];
}

resetcallback SYS_SetResetCallback(resetcallback cb)
{
	u32 level;
	resetcallback old;

	_CPU_ISR_Disable(level);
	old = __RSWCallback;
	__RSWCallback = cb;
#if defined(HW_DOL)
	if(__RSWCallback) {
		_piReg[0] = 2;
		__UnmaskIrq(IRQMASK(IRQ_PI_RSW));
	} else
		__MaskIrq(IRQMASK(IRQ_PI_RSW));
#endif
	_CPU_ISR_Restore(level);
	return old;
}

#if defined(HW_RVL)
powercallback SYS_SetPowerCallback(powercallback cb)
{
	u32 level;
	powercallback old;

	_CPU_ISR_Disable(level);
	old = __POWCallback;
	__POWCallback = cb;
	_CPU_ISR_Restore(level);
	return old;
}
#endif

void SYS_StartPMC(u32 mcr0val,u32 mcr1val)
{
	mtmmcr0(mcr0val);
	mtmmcr1(mcr1val);
}

void SYS_StopPMC(void)
{
	mtmmcr0(0);
	mtmmcr1(0);
}

void SYS_ResetPMC(void)
{
	mtpmc1(0);
	mtpmc2(0);
	mtpmc3(0);
	mtpmc4(0);
}

void SYS_DumpPMC(void)
{
	printf("<%u load/stores / %u miss cycles / %u cycles / %u instructions>\n",mfpmc1(),mfpmc2(),mfpmc3(),mfpmc4());
}

u32 SYS_GetCounterBias(void)
{
	u32 bias;
	syssram *sram;

	sram = __SYS_LockSram();
	if(!(sram->flags&0x08)) bias = 0;
	else bias = sram->counter_bias;
	__SYS_UnlockSram(0);
	return bias;
}

void SYS_SetCounterBias(u32 bias)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(sram->counter_bias!=bias || !(sram->flags&0x08)) {
		sram->counter_bias = bias;
		sram->flags |= 0x28;
		write = 1;
	}
	__SYS_UnlockSram(write);
}

s8 SYS_GetDisplayOffsetH(void)
{
	s8 offset;
	syssram *sram;

	sram = __SYS_LockSram();
	offset = sram->display_offsetH;
	__SYS_UnlockSram(0);
	return offset;
}

void SYS_SetDisplayOffsetH(s8 offset)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(sram->display_offsetH!=offset) {
		sram->display_offsetH = offset;
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetBootMode(void)
{
	u8 mode;
	syssram *sram;

	sram = __SYS_LockSram();
	mode = (sram->ntd&0x80);
	__SYS_UnlockSram(0);
	return mode;
}

void SYS_SetBootMode(u8 mode)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if((sram->ntd&0x80)!=mode) {
		sram->ntd = (sram->ntd&~0x80)|(mode&0x80);
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetEuRGB60(void)
{
	u8 enable;
	syssram *sram;

	sram = __SYS_LockSram();
	enable = _SHIFTR(sram->ntd,6,1);
	__SYS_UnlockSram(0);
	return enable;
}

void SYS_SetEuRGB60(u8 enable)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(_SHIFTR(sram->ntd,6,1)!=enable) {
		sram->ntd = (sram->ntd&~0x40)|(_SHIFTL(enable,6,1));
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetLanguage(void)
{
	u8 lang;
	syssram *sram;

	sram = __SYS_LockSram();
	lang = sram->lang;
	__SYS_UnlockSram(0);
	return lang;
}

void SYS_SetLanguage(u8 lang)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(sram->lang!=lang) {
		sram->lang = lang;
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetProgressiveScan(void)
{
	u8 enable;
	syssram *sram;

	sram = __SYS_LockSram();
	enable = _SHIFTR(sram->flags,7,1);
	__SYS_UnlockSram(0);
	return enable;
}

void SYS_SetProgressiveScan(u8 enable)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(_SHIFTR(sram->flags,7,1)!=enable) {
		sram->flags = (sram->flags&~0x80)|(_SHIFTL(enable,7,1));
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetSoundMode(void)
{
	u8 mode;
	syssram *sram;

	sram = __SYS_LockSram();
	mode = _SHIFTR(sram->flags,2,1);
	__SYS_UnlockSram(0);
	return mode;
}

void SYS_SetSoundMode(u8 mode)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if(_SHIFTR(sram->flags,2,1)!=mode) {
		sram->flags = (sram->flags&~0x04)|(_SHIFTL(mode,2,1));
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u8 SYS_GetVideoMode(void)
{
	u8 mode;
	syssram *sram;

	sram = __SYS_LockSram();
	mode = (sram->flags&0x03);
	__SYS_UnlockSram(0);
	return mode;
}

void SYS_SetVideoMode(u8 mode)
{
	u32 write;
	syssram *sram;

	write = 0;
	sram = __SYS_LockSram();
	if((sram->flags&0x03)!=mode) {
		sram->flags = (sram->flags&~0x03)|(mode&0x03);
		write = 1;
	}
	__SYS_UnlockSram(write);
}

u16 SYS_GetWirelessID(u32 chan)
{
	u16 id;
	syssramex *sramex;

	sramex = __SYS_LockSramEx();
	id = sramex->wirelessPad_id[chan];
	__SYS_UnlockSramEx(0);
	return id;
}

void SYS_SetWirelessID(u32 chan,u16 id)
{
	u32 write;
	syssramex *sramex;

	write = 0;
	sramex = __SYS_LockSramEx();
	if(sramex->wirelessPad_id[chan]!=id) {
		sramex->wirelessPad_id[chan] = id;
		write = 1;
	}
	__SYS_UnlockSramEx(write);
}

u16 SYS_GetGBSMode(void)
{
	u16 mode;
	syssramex *sramex;

	sramex = __SYS_LockSramEx();
	mode = sramex->gbs;
	__SYS_UnlockSramEx(0);
	return mode;
}

void SYS_SetGBSMode(u16 mode)
{
	u32 write;
	syssramex *sramex;

	write = 0;
	sramex = __SYS_LockSramEx();
	if(_SHIFTR(mode,10,5)>=20 || _SHIFTR(mode,6,2)==0x3 || (mode&0x3f)>=60) mode = 0;
	if(sramex->gbs!=mode) {
		sramex->gbs = mode;
		write = 1;
	}
	__SYS_UnlockSramEx(write);
}

#if defined(HW_DOL)
u32 SYS_GetConsoleType(void)
{
	u32 type;
	type = *((u32*)0x8000002c);
	if(!type) {
		type = SYS_CONSOLE_RETAIL_HW1;
		type += SYS_GetFlipperRevision();
	}
	return type;
}

u32 SYS_GetFlipperRevision(void)
{
	return _SHIFTR(_piReg[11],28,4);
}
#elif defined(HW_RVL)
u32 SYS_GetConsoleType(void)
{
	u32 type;
	u16 dev_code;

	type = 0;
	dev_code = *((u16*)0x800030e6);
	if(dev_code&0x8000) {
		switch(dev_code&~0x8000) {
			case 0x0002:
			case 0x0003:
			case 0x0203:
				type = SYS_CONSOLE_RETAIL_ES1_0;
				type += *((u32*)0x80003138);
				break;
			case 0x0201:
			case 0x0202:
				type = SYS_CONSOLE_NDEV_ES1_0;
				type += *((u32*)0x80003138);
				break;
			case 0x0300:
				type = SYS_CONSOLE_ARCADE;
				break;
		}
	} else {
		type = SYS_CONSOLE_RETAIL_ES1_0;
		type += *((u32*)0x80003138);
	}
	return type;
}

u32 SYS_GetHollywoodRevision(void)
{
	u32 rev;
	DCInvalidateRange((void*)0x80003138,8);
	rev = *((u32*)0x80003138);
	return rev;
}
#endif

u32 SYS_GetBusFrequency(void)
{
	u32 clock;
	clock = *((u32*)0x800000f8);
	if(!clock) clock = TB_BUS_CLOCK;
	return clock;
}

f32 SYS_GetCoreMultiplier(void)
{
	u32 pvr,hid1;

	static const f32 pll_cfg_table4[] = {
		 2.5,  7.5,  7.0,  1.0,
		 2.0,  6.5, 10.0,  4.5,
		 3.0,  5.5,  4.0,  5.0,
		 8.0,  6.0,  3.5,  0.0
	};
	static const f32 pll_cfg_table5[] = {
		 0.0,  0.0,  5.0,  1.0,  2.0,  2.5,  3.0,  3.5,  4.0,  4.5,
		 5.0,  5.5,  6.0,  6.5,  7.0,  7.5,  8.0,  8.5,  9.0,  9.5,
		10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0,
		20.0,  0.0
	};

	pvr = mfpvr();
	hid1 = mfhid1();
	switch(_SHIFTR(pvr,20,12)) {
		case 0x000:
			switch(_SHIFTR(pvr,16,4)) {
				case 0x8:
					switch(_SHIFTR(pvr,12,4)) {
						case 0x7:
							return pll_cfg_table5[_SHIFTR(hid1,27,5)];
						default:
							return pll_cfg_table4[_SHIFTR(hid1,28,4)];
					}
					break;
			}
			break;
		case 0x700:
			switch(_SHIFTR(pvr,16,4)) {
				case 0x1:
					return pll_cfg_table5[_SHIFTR(hid1,27,5)];
				default:
					return pll_cfg_table4[_SHIFTR(hid1,28,4)];
			}
			break;
	}
	return 0.0;
}

u32 SYS_GetCoreFrequency(void)
{
	u32 clock;
	clock = *((u32*)0x800000fc);
	if(!clock) {
		clock = SYS_GetBusFrequency();
		clock *= SYS_GetCoreMultiplier();
	}
	if(!clock) clock = TB_CORE_CLOCK;
	return clock;
}

s8 SYS_GetCoreTemperature(void)
{
	s32 i,ret;
	u32 pvr,thrm;

	pvr = mfpvr();
	if(_SHIFTR(pvr,16,16)!=0x8 || _SHIFTR(pvr,12,4)==0x7) return -1;
	if(!(mfthrm3()&1))
		mtthrm3((_SHIFTL(0x04,25,5)|_SHIFTL(8000,1,13)|1));

	i = 5;
	ret = 64;
	while(i--) {
		mtthrm2((_SHIFTL(ret,23,7)|1));
		do {
			thrm = mfthrm2();
		} while(!(thrm&0x40000000));
		if(thrm&0x80000000) ret += (2<<i);
		else ret -= (2<<i);
	}
	mtthrm2(0);
	return ret;
}
