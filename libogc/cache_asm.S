/*-------------------------------------------------------------

cache_asm.S -- Cache interface

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

#include <asm.h>

	.globl DCFlashInvalidate
DCFlashInvalidate:
	mfhid0	r3
	ori		r3,r3,0x0400
	mthid0	r3
	blr

	.globl DCEnable
DCEnable:
	sync
	mfhid0	r3
	ori		r3,r3,0x4000
	mthid0	r3
	blr

	.globl DCDisable
DCDisable:
	sync
	mfhid0	r3
	rlwinm	r3,r3,0,18,16
	mthid0	r3
	blr

	.globl DCFreeze
DCFreeze:
	sync
	mfhid0	r3
	ori		r3,r3,0x1000
	mthid0	r3
	blr

	.globl DCUnfreeze
DCUnfreeze:
	mfhid0	r3
	rlwinm	r3,r3,0,20,18
	mthid0	r3
	blr

	.globl DCTouchLoad
DCTouchLoad:
	dcbt	r0,r3
	blr

	.globl DCBlockZero
DCBlockZero:
	dcbz	r0,r3
	blr

	.globl DCBlockStore
DCBlockStore:
	dcbst	r0,r3
	blr

	.globl DCBlockFlush
DCBlockFlush:
	dcbf	r0,r3
	blr

	.globl DCBlockInvalidate
DCBlockInvalidate:
	dcbi	r0,r3
	blr

	.globl DCInvalidateRange
DCInvalidateRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbi	r0,r3
	addi	r3,r3,32
	bdnz	1b
	blr

	.globl DCFlushRange
DCFlushRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbf	r0,r3
	addi	r3,r3,32
	bdnz	1b
	sc
	blr

	.globl DCStoreRange
DCStoreRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbst	r0,r3
	addi	r3,r3,32
	bdnz	1b
	sc
	blr

	.globl DCFlushRangeNoSync
DCFlushRangeNoSync:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbf	r0,r3
	addi	r3,r3,32
	bdnz	1b
	blr

	.globl DCStoreRangeNoSync
DCStoreRangeNoSync:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbst	r0,r3
	addi	r3,r3,32
	bdnz	1b
	blr

	.globl DCZeroRange
DCZeroRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbz	r0,r3
	addi	r3,r3,32
	bdnz	1b
	blr

	.globl DCTouchRange
DCTouchRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	dcbt	r0,r3
	addi	r3,r3,32
	bdnz	1b
	blr

	.globl ICInvalidateRange
ICInvalidateRange:
	cmplwi	r4,0
	blelr
	clrlwi	r5,r3,27
	add		r4,r4,r5
	addi	r4,r4,31
	srwi	r4,r4,5
	mtctr	r4
1:	icbi	r0,r3
	addi	r3,r3,32
	bdnz	1b
	sync
	isync
	blr

	.globl ICFlashInvalidate
ICFlashInvalidate:
	mfhid0	r3
	ori		r3,r3,0x0800
	mthid0	r3
	blr

	.globl ICEnable
ICEnable:
	isync
	mfhid0	r3
	ori		r3,r3,0x8000
	mthid0	r3
	blr

	.globl ICDisable
ICDisable:
	isync
	mfhid0	r3
	rlwinm	r3,r3,0,17,15
	mthid0	r3
	blr

	.globl ICFreeze
ICFreeze:
	isync
	mfhid0	r3
	ori		r3,r3,0x2000
	mthid0	r3
	blr

	.globl ICUnfreeze
ICUnfreeze:
	mfhid0	r3
	rlwinm	r3,r3,0,19,17
	mthid0	r3
	blr

	.globl ICBlockInvalidate
ICBlockInvalidate:
	icbi	r0,r3
	blr

	.globl ICSync
ICSync:
	isync
	blr

	.globl __L2Init
__L2Init:
	mflr	r0
	stw		r0,4(sp)
	stwu	sp,-16(sp)
	stw		r31,12(sp)
	mfmsr	r31
	sync
	li		r3,48
	mtmsr	r3
	sync
	bl		L2Disable
	bl		L2GlobalInvalidate
	mtmsr	r31
	lwz		r0,20(sp)
	lwz		r31,12(sp)
	addi	sp,sp,16
	mtlr	r0
	blr

	.globl L2Enable
L2Enable:
	mfl2cr	r3
	oris	r0,r3,0x8000
	rlwinm	r3,r0,0,11,9
	mtl2cr	r3
	blr

	.globl L2Disable
L2Disable:
	sync
	mfl2cr	r3
	clrlwi	r3,r3,1
	mtl2cr	r3
	sync
	blr

	.globl L2GlobalInvalidate
L2GlobalInvalidate:
	mflr	r0
	stw		r0,4(sp)
	stwu	sp,-8(sp)
	bl		L2Disable
	mfl2cr	r3
	oris	r3,r3,0x0020
	mtl2cr	r3
1:	mfl2cr	r3
	clrlwi.	r0,r3,31
	bne		1b
	mfl2cr	r3
	rlwinm	r3,r3,0,11,9
	mtl2cr	r3
2:	mfl2cr	r3
	clrlwi.	r0,r3,31
	bne		2b
	lwz		r0,12(sp)
	addi	sp,sp,8
	mtlr	r0
	blr

	.globl L2SetDataOnly
L2SetDataOnly:
	cmpwi	r3,0
	beq		1f
	mfl2cr	r3
	oris	r3,r3,0x0040
	mtl2cr	r3
	blr
1:	mfl2cr	r3
	rlwinm	r3,r3,0,10,8
	mtl2cr	r3
	blr

	.globl L2SetWriteThrough
L2SetWriteThrough:
	cmpwi	r3,0
	beq		1f
	mfl2cr	r3
	oris	r3,r3,0x0008
	mtl2cr	r3
	blr
1:	mfl2cr	r3
	rlwinm	r3,r3,0,13,11
	mtl2cr	r3
	blr

	.globl __LCEnable
__LCEnable:
	mfmsr	r5
	ori		r5,r5,0x1000
	mtmsr	r5
	lis		r3,0x8000
	li		r4,1024
	mtctr	r4
1:	dcbt	r0,r3
	dcbst	r0,r3
	addi	r3,r3,32
	bdnz	1b
	mfhid2	r4
	oris	r4,r4,0x100f
	mthid2	r4
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	lis		r3,0xe000
	ori		r3,r3,0x0002
	mtdbatl	3,r3
	ori		r3,r3,0x01fe
	mtdbatu	3,r3
	isync
	lis		r3,0xe000
	li		r6,512
	mtctr	r6
	li		r6,0
2:	dcbz_l	r6,r3
	addi	r3,r3,32
	bdnz	2b
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	blr

	.globl LCDisable
LCDisable:
	lis		r3,0xe000
	li		r4,512
	mtctr	r4
1:	dcbi	r0,r3
	addi	r3,r3,32
	bdnz	1b
	mfhid2	r4
	rlwinm	r4,r4,0,4,2
	mthid2	r4
	blr

	.globl LCIsEnable
LCIsEnable:
	mfhid2	r4
	extrwi	r3,r4,1,3
	blr

	.globl LCAllocOneTag
LCAllocOneTag:
	cmpwi	r3,0
	beq		1f
	dcbi	r0,r4
1:	dcbz_l	r0,r4
	blr

	.globl LCAllocTags
LCAllocTags:
	mflr	r6
	cmplwi	r5,0
	ble		2f
	mtctr	r5
	cmpwi	r3,0
	beq		3f
1:	dcbi	r0,r4
	dcbz_l	r0,r4
	addi	r4,r4,32
	bdnz	1b
	b		2f
3:	dcbz_l	r0,r4
	addi	r4,r4,32
	bdnz	3b
2:	mtlr	r6
	blr

	.globl LCLoadBlocks
LCLoadBlocks:
	extrwi		r6,r5,5,25
	clrlwi		r4,r4,3
	or			r6,r6,r4
	mtdmau		r6
	clrlslwi	r6,r5,30,2
	or			r6,r6,r3
	ori			r6,r6,0x0012
	mtdmal		r6
	blr

	.globl LCStoreBlocks
LCStoreBlocks:
	extrwi		r6,r5,5,25
	clrlwi		r3,r3,3
	or			r6,r6,r3
	mtdmau		r6
	clrlslwi	r6,r5,30,2
	or			r6,r6,r4
	ori			r6,r6,0x0002
	mtdmal		r6
	blr
