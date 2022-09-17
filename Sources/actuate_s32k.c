/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     actuate_s32k.c
*
* @date     March-28-2017
*
* @brief    Header file for actuator module
*
*******************************************************************************/
/******************************************************************************
| Includes
-----------------------------------------------------------------------------*/
#include "S32K144.h"
#include "actuate_s32k.h"
#include "peripherals_config.h"
#include "gflib.h"
#include "gmclib.h"
#include "gdflib.h"
#include "pdb_driver.h"
#include "ftm_pwm_driver.h"

/******************************************************************************
| External declarations
-----------------------------------------------------------------------------*/

/******************************************************************************
| Defines and macros            (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Typedefs and structures       (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Global variable definitions   (scope: module-exported)
-----------------------------------------------------------------------------*/
tU32 pwmCycleCnt = 0;    //it's incremented in each PWM cycle. Different PWM edge values are loaded depending on this value is even or odd

tU16 pdbPretrigDelay[5] =
{
	FTM_PERIOD_MOD-600,
	FTM_PERIOD_MOD-400,
	FTM_PERIOD_MOD,
	FTM_PERIOD_MOD+400,
	FTM_PERIOD_MOD+600
};	                     //pretrigger delay initial values for PDB1 channel 0 Delay 0 ~ Delay 4 registers

tU16 pdbTriggerOffset = 40;	    //40cnt=0.5uS, this offset duration is the ADC sampling time (not including the conversion time) for one channel;

/******************************************************************************
| Global variable definitions   (scope: module-local)
-----------------------------------------------------------------------------*/
PWM_3PHASE_EDGES_TYPE   pwmEdgesFoc;		        //PWM A B C edges in cnt, calculated by FOC generated PWM duties;
PWM_3PHASE_EDGES_TYPE   pwmEdgesFtm;		        //PWM A B C edges in cnt, same value as pwmEdgesFoc, used to update to FTM3 CnV registers;
SWLIBS_3Syst_U16        pwmDutyCnt;		            //PWM A B C duties in cnt;
SWLIBS_3Syst_U16        pwmCenterPulseHalfWidthCnt;	//PWM center pulse half-width in cnt;

uint32_t                minZeroPulseCnt = 80;		//80cnt = 1uS at 80MHz bus clock; it's the center pulse half width;
uint32_t                minSamplingPulseCnt = 120;	//min sample time 1.5uS; it's the min time from on Phase Voltage edge to a phase current stable suitable for sampling;
										            //ADC sample time is 12 cycle, so it's actually 0.3uS; (12/40MHz=0.3uS)
uint32_t                minSumPulseCnt = 200;		//80 + 120;

/******************************************************************************
| Function prototypes           (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Function implementations      (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Function implementations      (scope: module-exported)
-----------------------------------------------------------------------------*/

/**************************************************************************//*!
@brief Unmask PWM output and set 50% dytucyle 

@param[in,out]  

@return
******************************************************************************/
tBool ACTUATE_EnableOutput(void)
{
	SWLIBS_3Syst_FLT fltpwm;

	tBool statePWM;

	// Apply 0.5 duty cycle
	fltpwm.fltArg1 = 0.5F;
	fltpwm.fltArg2 = 0.5F;
	fltpwm.fltArg3 = 0.5F;
	
	statePWM = ACTUATE_SetDutycycle(&fltpwm, 2);
	ACTUATE_PwmUpdateBuffer();

    // Enable PWM
	FTM_DRV_MaskOutputChannels(INST_FLEXTIMER_PWM3, 0x0, true);

	return(statePWM);
}


/**************************************************************************//*!
@brief Mask PWM output and set 50% dytucyle 

@param[in,out]  

@return
******************************************************************************/
tBool ACTUATE_DisableOutput(void)
{
	SWLIBS_3Syst_FLT fltpwm;

	tBool statePWM;

	// Apply 0.5 duty cycle
	fltpwm.fltArg1 = 0.5F;
	fltpwm.fltArg2 = 0.5F;
	fltpwm.fltArg3 = 0.5F;
	
	statePWM = ACTUATE_SetDutycycle(&fltpwm, 2);
	ACTUATE_PwmUpdateBuffer();

    /* Disable PWM */
	FTM_DRV_MaskOutputChannels(INST_FLEXTIMER_PWM3, 0x3F, true);

	return(statePWM);
}

/**************************************************************************//*!
@brief Set PWM dytycyle, the dutycycle will by updated on next reload event

@param	fltpwm,                 input, pwm duty in float format, calculated by FOC
		sector,                 input, sector number used to sort the 3 phase duties;
		pwmEdgesFoc, 		    output, it's a global array, contains 4 edges values of 3 phases, total 12 integer numbers;
								This function will not update the FTM PWM registers.
								This function just update the buffer (pwmEdgesFoc) in SRAM, and let FTM Reload ISR to write these values into FTM PWM registers;
		pdbPretrigDelay[5],	    output, PDB pre-triggers, it's a global array, contains 5 delay timers for PDB1 pre-trigger 0 ~ 4;
								This function will also update the PDB delay registers with the values in pdbPretrigDelay[];
		pwmDutyCnt,			    output, global variable, it's the 3 phase duties in FTM cnt;

@return
******************************************************************************/
__attribute__((section (".code_ram"))) 		// inserting function to the RAM section
tBool ACTUATE_SetDutycycle(SWLIBS_3Syst_FLT *fltpwm, tU16 sector)
{
	tBool   state_pwm = true;
	tU32    diffUV, diffVW, diffWU, temp;

	pwmDutyCnt.u16Arg1 = MLIB_Mul(fltpwm->fltArg1, FTM_PERIOD_MOD);
	pwmDutyCnt.u16Arg2 = MLIB_Mul(fltpwm->fltArg2, FTM_PERIOD_MOD);
	pwmDutyCnt.u16Arg3 = MLIB_Mul(fltpwm->fltArg3, FTM_PERIOD_MOD);

	switch (sector) {
	case 1:		//duty A > duty B > duty C
		diffUV = pwmDutyCnt.u16Arg1 - pwmDutyCnt.u16Arg2;
		diffVW = pwmDutyCnt.u16Arg2 - pwmDutyCnt.u16Arg3;

		pwmCenterPulseHalfWidthCnt.u16Arg3 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg2 = ((diffVW)<minSamplingPulseCnt)?(minSumPulseCnt - diffVW):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg2 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg1 = ((diffUV)<(temp - minZeroPulseCnt))?(temp - diffUV):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseB.u16Edge1 - pdbTriggerOffset;	//PhB edge 1 for PhA current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseC.u16Edge1 - pdbTriggerOffset;	//PhC edge 1 for PhC current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseB.u16Edge4 - pdbTriggerOffset;		//PhB edge 4 for PhC current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseA.u16Edge4 - pdbTriggerOffset;		//PhA edge 4 for PhA current sampling

		break;

	case 2:		//duty B > duty A > duty C
		diffUV = pwmDutyCnt.u16Arg2 - pwmDutyCnt.u16Arg1;
		diffWU = pwmDutyCnt.u16Arg1 - pwmDutyCnt.u16Arg3;

		pwmCenterPulseHalfWidthCnt.u16Arg3 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg1 = ((diffWU)<minSamplingPulseCnt)?(minSumPulseCnt - diffWU):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg1 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg2 = ((diffUV)<(temp - minZeroPulseCnt))?(temp - diffUV):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseA.u16Edge1 - pdbTriggerOffset;	//PhA edge 1 for PhB current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseC.u16Edge1 - pdbTriggerOffset;	//PhC edge 1 for PhC current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseA.u16Edge4 - pdbTriggerOffset;		//PhA edge 4 for PhC current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseB.u16Edge4 - pdbTriggerOffset;		//PhB edge 4 for PhB current sampling


		break;

	case 3:		//duty B > duty C > duty A
		diffWU = pwmDutyCnt.u16Arg3 - pwmDutyCnt.u16Arg1;
		diffVW = pwmDutyCnt.u16Arg2 - pwmDutyCnt.u16Arg3;

		pwmCenterPulseHalfWidthCnt.u16Arg1 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg3 = ((diffWU)<minSamplingPulseCnt)?(minSumPulseCnt - diffWU):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg3 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg2 = ((diffVW)<(temp - minZeroPulseCnt))?(temp - diffVW):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseC.u16Edge1 - pdbTriggerOffset;	//PhC edge 1 for PhB current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseA.u16Edge1 - pdbTriggerOffset;	//PhA edge 1 for PhA current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseC.u16Edge4 - pdbTriggerOffset;		//PhC edge 4 for PhA current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseB.u16Edge4 - pdbTriggerOffset;		//PhB edge 4 for PhB current sampling

		break;

	case 4:		//duty C > duty B > duty A
		diffUV = pwmDutyCnt.u16Arg2 - pwmDutyCnt.u16Arg1;
		diffVW = pwmDutyCnt.u16Arg3 - pwmDutyCnt.u16Arg2;

		pwmCenterPulseHalfWidthCnt.u16Arg1 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg2 = ((diffUV)<minSamplingPulseCnt)?(minSumPulseCnt - diffUV):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg2 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg3 = ((diffVW)<(temp - minZeroPulseCnt))?(temp - diffVW):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseB.u16Edge1 - pdbTriggerOffset;	//PhB edge 1 for PhC current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseA.u16Edge1 - pdbTriggerOffset;	//PhA edge 1 for PhA current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseB.u16Edge4 - pdbTriggerOffset;		//PhB edge 4 for PhA current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseC.u16Edge4 - pdbTriggerOffset;		//PhC edge 4 for PhC current sampling

		break;

	case 5:		//duty C > duty A > duty B
		diffUV = pwmDutyCnt.u16Arg1 - pwmDutyCnt.u16Arg2;
		diffWU = pwmDutyCnt.u16Arg3 - pwmDutyCnt.u16Arg1;

		pwmCenterPulseHalfWidthCnt.u16Arg2 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg1 = ((diffUV)<minSamplingPulseCnt)?(minSumPulseCnt - diffUV):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg1 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg3 = ((diffWU)<(temp - minZeroPulseCnt))?(temp - diffWU):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseA.u16Edge1 - pdbTriggerOffset;	//PhA edge 1 for PhC current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseB.u16Edge1 - pdbTriggerOffset;	//PhB edge 1 for PhB current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseA.u16Edge4 - pdbTriggerOffset;		//PhA edge 4 for PhB current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseC.u16Edge4 - pdbTriggerOffset;		//PhC edge 4 for PhC current sampling

		break;

	case 6:		//duty A > duty C > duty B
		diffWU = pwmDutyCnt.u16Arg1 - pwmDutyCnt.u16Arg3;
		diffVW = pwmDutyCnt.u16Arg3 - pwmDutyCnt.u16Arg2;

		pwmCenterPulseHalfWidthCnt.u16Arg2 = minZeroPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg3 = ((diffVW)<minSamplingPulseCnt)?(minSumPulseCnt - diffVW):minZeroPulseCnt;
		temp         				       = pwmCenterPulseHalfWidthCnt.u16Arg3 + minSamplingPulseCnt;
		pwmCenterPulseHalfWidthCnt.u16Arg1 = ((diffWU)<(temp - minZeroPulseCnt))?(temp - diffWU):minZeroPulseCnt;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = pwmEdgesFoc.EdgesPhaseC.u16Edge1 - pdbTriggerOffset;	//PhC edge 1 for PhA current sampling
		pdbPretrigDelay[1] = pwmEdgesFoc.EdgesPhaseB.u16Edge1 - pdbTriggerOffset;	//PhB edge 1 for PhB current sampling
		pdbPretrigDelay[2] = FTM_PERIOD_MOD - (pdbTriggerOffset>>1);		        //DC bus voltage sampling
		pdbPretrigDelay[3] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseC.u16Edge4 - pdbTriggerOffset;		//PhC edge 4 for PhB current sampling
		pdbPretrigDelay[4] = FTM_PERIOD_MOD + pwmEdgesFoc.EdgesPhaseA.u16Edge4 - pdbTriggerOffset;		//PhA edge 4 for PhA current sampling

		break;

	default:
		pwmCenterPulseHalfWidthCnt.u16Arg1 = 80;
		pwmCenterPulseHalfWidthCnt.u16Arg2 = 80;
		pwmCenterPulseHalfWidthCnt.u16Arg3 = 80;

		//Calculate Phase A B C PWM edges for consecutive two periods;
		//PhaseA 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1 - pwmDutyCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg1;
		//PhaseA 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseA.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg1;
		pwmEdgesFoc.EdgesPhaseA.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg1 + pwmDutyCnt.u16Arg1;

		//PhaseB 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2 - pwmDutyCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg2;
		//PhaseB 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseB.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg2;
		pwmEdgesFoc.EdgesPhaseB.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg2 + pwmDutyCnt.u16Arg2;

		//PhaseC 1st 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge1 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3 - pwmDutyCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge2 = FTM_PERIOD_MOD - pwmCenterPulseHalfWidthCnt.u16Arg3;
		//PhaseC 2nd 25uS PWM Edges;
		pwmEdgesFoc.EdgesPhaseC.u16Edge3 = pwmCenterPulseHalfWidthCnt.u16Arg3;
		pwmEdgesFoc.EdgesPhaseC.u16Edge4 = pwmCenterPulseHalfWidthCnt.u16Arg3 + pwmDutyCnt.u16Arg3;

		pdbPretrigDelay[0] = FTM_PERIOD_MOD-600;
		pdbPretrigDelay[1] = FTM_PERIOD_MOD-400;
		pdbPretrigDelay[2] = FTM_PERIOD_MOD;
		pdbPretrigDelay[3] = FTM_PERIOD_MOD+400;
		pdbPretrigDelay[4] = FTM_PERIOD_MOD+600;

		break;
}

	ACTUATE_PdbUpdatePretrigDelay();

	state_pwm = false;

	return(state_pwm);
}

/*****************************************************************************************
*
* @brief	Update PDB delay registers values with the array pdbPretrigDelay[];
*
* @param	pdbPretrigDelay[], input, these values are calculated in previous PWM edges calculation;
* 			And LDOK register bit is written to make the update effective.
*
* @return  none
*
 ****************************************************************************************/
void ACTUATE_PdbUpdatePretrigDelay(void)
{
    PDB1->CH[0].DLY[0] = PDB_DLY_DLY(pdbPretrigDelay[0]);
    PDB1->CH[0].DLY[1] = PDB_DLY_DLY(pdbPretrigDelay[1]);
    PDB1->CH[0].DLY[2] = PDB_DLY_DLY(pdbPretrigDelay[2]);
    PDB1->CH[0].DLY[3] = PDB_DLY_DLY(pdbPretrigDelay[3]);
    PDB1->CH[0].DLY[4] = PDB_DLY_DLY(pdbPretrigDelay[4]);
    REG_BIT_SET32(&(PDB1->SC), PDB_SC_LDOK_MASK);
}

/*****************************************************************************************
*
* @brief	Copy PWM edges values from pwmEdgesFoc (buffer for save FOC calculation results)
*                                   to pwmEdgesFtm (buffer for FTM PWM registers update);
*
* @param	pwmEdgesFoc, input, it's calculated PWM edge values;
* 			pwmEdgesFtm, output, it's the data source to update FTM CnV registers;
* 			Either of the above array contains two groups of values, one for Odd PWM cycle, the other for Even PWM cycle;
*
* @return  none
*
 ****************************************************************************************/
void ACTUATE_PwmUpdateBuffer(void)
{
	tU32 i;
	tU16 *src, *dst;
	src = &pwmEdgesFoc.EdgesPhaseA.u16Edge1;
	dst = &pwmEdgesFtm.EdgesPhaseA.u16Edge1;
	for(i = 0; i<12; i++)
	{
		*dst++ = *src++;
	}
}

/*****************************************************************************************
*
* @brief	Update PWM C0V ~ C5V register values according to PWM Duty values calculated by PWM edge calculation;
*
*
* @param	pwmEdgesFtm, input, it's used to update FTM PWM registers,
* 			it's used in every FTM reload ISR, and is also used whenever need update FTM PWM registers;
*
* @return	none
*
* @details	Copy the values from variable pwmEdgesFtm to FTM registers C0V~C5V;
* 			And write PWMLOAD register to make the update effective;
* 			There are two sets of values, one for even PWM cycle, the other for odd PWM cycle;
* 			Use a cycle counter pwmCycleCnt to identify currently it's in even cycle or odd cycle.
*
 ****************************************************************************************/
void ACTUATE_PwmUpdateRegisters(void)
{
	if(pwmCycleCnt & 1)
	{
		FTM3->CONTROLS[0].CnV = pwmEdgesFtm.EdgesPhaseA.u16Edge1;
		FTM3->CONTROLS[1].CnV = pwmEdgesFtm.EdgesPhaseA.u16Edge2;
		FTM3->CONTROLS[2].CnV = pwmEdgesFtm.EdgesPhaseB.u16Edge1;
		FTM3->CONTROLS[3].CnV = pwmEdgesFtm.EdgesPhaseB.u16Edge2;
		FTM3->CONTROLS[4].CnV = pwmEdgesFtm.EdgesPhaseC.u16Edge1;
		FTM3->CONTROLS[5].CnV = pwmEdgesFtm.EdgesPhaseC.u16Edge2;
	}
	else
	{
		FTM3->CONTROLS[0].CnV = pwmEdgesFtm.EdgesPhaseA.u16Edge3;
		FTM3->CONTROLS[1].CnV = pwmEdgesFtm.EdgesPhaseA.u16Edge4;
		FTM3->CONTROLS[2].CnV = pwmEdgesFtm.EdgesPhaseB.u16Edge3;
		FTM3->CONTROLS[3].CnV = pwmEdgesFtm.EdgesPhaseB.u16Edge4;
		FTM3->CONTROLS[4].CnV = pwmEdgesFtm.EdgesPhaseC.u16Edge3;
		FTM3->CONTROLS[5].CnV = pwmEdgesFtm.EdgesPhaseC.u16Edge4;
	}
	FTM3->PWMLOAD |=  (1UL << FTM_PWMLOAD_LDOK_SHIFT);
}


/* End of file */
