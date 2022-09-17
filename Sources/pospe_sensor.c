/***************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     pospe_sensor.c
*
* @date     March-28-2017
*
* @brief    Header file for position sensor processing
*
*******************************************************************************/
/******************************************************************************
| Includes
-----------------------------------------------------------------------------*/
#include "pospe_sensor.h"

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

/******************************************************************************
| Global variable definitions   (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Function prototypes           (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Function implementations      (scope: module-local)
-----------------------------------------------------------------------------*/

/******************************************************************************
| Function implementations      (scope: module-exported)
-----------------------------------------------------------------------------*/

/******************************************************************************
@brief   tBool POSPE_GetPospeElEnc(encoderPospe_t *ptr)
           - read encoder edges to get rotor position and speed

@param   this   Pointer to the current object.

@return  tBool
******************************************************************************/
tBool POSPE_GetPospeElEnc(encoderPospe_t *ptr)
{
	tBool statusPass;
	statusPass = TRUE;

	static tFrac16  f16CntValue, f16ModValue;
	static tFrac32 	f32ThRotMe, f32ThRotEl, f32ThRotMe_FTM;

    /* read encoder edges to get mechanical position */
	f16CntValue =  (FTM2->CNT & 0xFFFF);
	f16ModValue =  (FTM2->MOD & 0xFFFF);

	// Mechanical rotor position acquired from FTM2 - in fix point <-1,1)
	f32ThRotMe_FTM = MLIB_ConvertPU_F32FLT(MLIB_Div((tFloat)f16CntValue, (tFloat)f16ModValue));

	AMCLIB_TrackObsrv(ptr->thRoErr, &(ptr->thRotMec), &(ptr->wRotMec.raw), &(ptr->TrackObsrv));

	// Mechanical and electrical angular speed calculation - float
	ptr->wRotEl.raw = MLIB_Mul(ptr->wRotMec.raw, MOTOR_PP);

    // Mechanical rotor position - transformation in to the range <-1,1> - fix point
    f32ThRotMe 				 		= MLIB_ConvertPU_F32FLT(MLIB_Div(ptr->thRotMec,FLOAT_PI));
    // Rotor position - transformation from mechanical to electrical  - fix point
    f32ThRotEl				 		= MLIB_ShL_F32(MLIB_MulSat_F32(f32ThRotMe, MOTOR_PP_GAIN), MOTOR_PP_SHIFT);

    // Electrical rotor position - <-pi, pi> range - floating point
    ptr->thRotEl.filt    			= MLIB_Mul(MLIB_ConvertPU_FLTF32(f32ThRotEl), FLOAT_PI);

    // Theta error = (Theta_eTimer - Theta_est), calculated in fix point due to the precision
  	ptr->thRoErr 					= MLIB_Mul(MLIB_ConvertPU_FLTF32(MLIB_Sub_F32(f32ThRotMe_FTM, f32ThRotMe)), FLOAT_PI);

    return(statusPass);
}

/******************************************************************************
@brief   POSPE_ClearPospeElEnc(encoderPospe_t *ptr)
           - clear internal variables

@param   this   Pointer to the current object.

@return  tBool
******************************************************************************/
tBool POSPE_ClearPospeElEnc(encoderPospe_t *ptr)
{
	tBool statusPass;
    statusPass = TRUE;

    // Encoder's mechanical position and speed init
    ptr->thRotMec									= 0.0F;
    ptr->thRoErr									= 0.0F;
    ptr->wRotMec.raw								= 0.0F;
    ptr->wRotMec.filt								= 0.0F;

    // Encoder's electrical position and speed init
    ptr->thRotEl.raw								= 0.0F;
    ptr->thRotEl.filt								= 0.0F;
    ptr->wRotEl.raw									= 0.0F;
    ptr->wRotEl.filt								= 0.0F;

    AMCLIB_TrackObsrvInit(&(ptr->TrackObsrv));

    return(statusPass);
}

