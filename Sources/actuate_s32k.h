/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     actuate_s32k.h
*
* @date     March-28-2017
*
* @brief    Header file for actuator module
*
*******************************************************************************/
#ifndef _ACTUATE_S32K_H_
#define _ACTUATE_S32K_H_

#include "S32K144.h"
#include "gflib.h"
#include "gmclib.h"
#include "gdflib.h"

/******************************************************************************
| Defines and macros            (scope: module-local)
-----------------------------------------------------------------------------*/
#define FTM_PERIOD_MOD	2000

/******************************************************************************
| Typedefs and structures       (scope: module-local)
-----------------------------------------------------------------------------*/
typedef struct
{
	tU16	u16Edge1;
	tU16	u16Edge2;
	tU16	u16Edge3;
	tU16	u16Edge4;
}PWM_EDGES_TYPE;

typedef struct
{
	PWM_EDGES_TYPE EdgesPhaseA;
	PWM_EDGES_TYPE EdgesPhaseB;
	PWM_EDGES_TYPE EdgesPhaseC;
}PWM_3PHASE_EDGES_TYPE;

typedef struct
{
	tU16	u16Arg1;
	tU16	u16Arg2;
	tU16	u16Arg3;
}SWLIBS_3Syst_U16;

/******************************************************************************
| Exported Variables
-----------------------------------------------------------------------------*/
extern tU16 pdbPretrigDelay[5];	//pretrigger delay initial values for PDB1 channel 0 Delay 0 ~ Delay 4 registers
extern tU16 pdbTriggerOffset;	//this offset duration is the ADC sampling time (not including the conversion time) for one channel;
extern tU32 pwmCycleCnt;		//it's incremented in each PWM cycle. Different PWM edge values are loaded depending on this value is even or odd

/******************************************************************************
| Exported function prototypes
-----------------------------------------------------------------------------*/
extern tBool 	ACTUATE_EnableOutput(void);
extern tBool 	ACTUATE_DisableOutput(void);
extern tBool 	ACTUATE_SetDutycycle(SWLIBS_3Syst_FLT *fltpwm, tU16 sector);
extern void 	ACTUATE_PwmUpdateRegisters(void);
extern void 	ACTUATE_PdbUpdatePretrigDelay(void);
extern void 	ACTUATE_PwmUpdateBuffer(void);

/******************************************************************************
| Inline functions
-----------------------------------------------------------------------------*/

#endif /* _ACTUATES_S32K_H_ */
