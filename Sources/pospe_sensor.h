/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     pospe_sensor.h
*
* @date     March-28-2017
*
* @brief    Header file for position sensor processing
*
*******************************************************************************/
#ifndef POSPE_SENSOR_H_
#define POSPE_SENSOR_H_

/******************************************************************************
| Defines and macros            (scope: module-local)
-----------------------------------------------------------------------------*/
#include "flexTimer_qd2.h"
#include "gflib.h"
#include "amclib.h"
#include "PMSM_appconfig.h"

/******************************************************************************
| Typedefs and structures       (scope: module-local)
-----------------------------------------------------------------------------*/

typedef struct
{
    tFloat    raw;   /*! raw value */
    tFloat    filt;  /*! filtered value */
}pospeValue_t;

typedef struct
{
	tFloat								thRotMec;
	pospeValue_t						wRotMec;
	pospeValue_t						thRotEl;
	pospeValue_t						thRotElk1;
	pospeValue_t						wRotEl;
	tFloat                              thRoErr;
	pospeValue_t						thRotMecSin;
	pospeValue_t						thRotMecCos;
	pospeValue_t						thRotElSin;
	pospeValue_t						thRotElCos;
	AMCLIB_TRACK_OBSRV_T_FLT 			TrackObsrv;
	tFrac32								s32MotorPpScale;
	tFrac16								s16MotorPpScaleShift;
	tFloat								fltMotorPP;
	tFrac16								ftmCntValue;
	tFrac16								ftmModValue;
}encoderPospe_t;

extern tBool POSPE_GetPospeElEnc(encoderPospe_t *ptr);
extern tBool POSPE_ClearPospeElEnc(encoderPospe_t *ptr);

#endif /* POSPE_SENSOR_H_ */

