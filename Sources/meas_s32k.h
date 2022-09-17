/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     meas_s32k.h
*
* @date     March-28-2017
*
* @brief    Header file for measurement module
*
*******************************************************************************/
#ifndef _MEAS_S32K_H_
#define _MEAS_S32K_H_

#include "PMSM_appconfig.h"
#include "s32k144.h"
#include "peripherals_config.h"
#include "gflib.h"
#include "gmclib.h"
#include "gdflib.h"

/******************************************************************************
| Defines and macros            (scope: module-local)
-----------------------------------------------------------------------------*/
#define I_DCB_MAX		25.0F

/******************************************************************************
| Typedefs and structures       (scope: module-local)
-----------------------------------------------------------------------------*/

typedef struct
{
    tFloat    raw;   /*! raw value */
    tFloat    filt;  /*! filtered value */
}meas_t;

/*------------------------------------------------------------------------*//*!
@brief  Structure containing measured raw values
*//*-------------------------------------------------------------------------*/
typedef struct
{
	meas_t    fltPhA;     // DC offset measured on phase A current
	meas_t    fltPhB;     // DC offset measured on phase B current
	meas_t    fltPhC;     // DC offset measured on phase C current
	meas_t    fltIdcb;    // DC offset measured on DC bus current
	meas_t    fltUdcb;    // DC offset measured on DC bus voltage
	meas_t    fltTemp;    // DC offset measured on temperature
}measResult_t;

typedef struct
{
    tFloat    				fltOffset;   /*! raw value */
    GDFLIB_FILTER_MA_T_FLT	filtParam;	 /*! filter parameters */
}offsetBasic_t;


/*------------------------------------------------------------------------*//*!
@brief  Structure containing variables for software DC offset calibration.
*//*-------------------------------------------------------------------------*/
typedef struct
{
    offsetBasic_t    fltPhA;         // DC offset measured on phase A current
    offsetBasic_t    fltPhB;         // DC offset measured on phase B current
    offsetBasic_t    fltPhC;         // DC offset measured on phase C current
    offsetBasic_t    fltIdcb;        // DC offset measured on DC bus current
    offsetBasic_t    fltUdcb;        // DC offset measured on DC bus voltage
    offsetBasic_t    fltTemp;        // DC offset measured on temperature
}offset_t;


/*------------------------------------------------------------------------*//*!
@brief  Structure containing variables to configure Calibration on the application
        level.
*//*-------------------------------------------------------------------------*/
typedef struct
{
	tU16     u16CalibSamples; // Number of samples taken for calibration
}calibParam_t;

/*------------------------------------------------------------------------*//*!
@brief  Union containing module operation flags.
*//*-------------------------------------------------------------------------*/
typedef union
{
    tU16 R;
    struct {
        tU16               :14;// RESERVED
        tU16 calibDone     :1; // DC offset calibration done
        tU16 calibInitDone :1; // initial setup for DC offset calibration done
    } B;
}calibFlags_t;

/*------------------------------------------------------------------------*//*!
@brief  Module structure containing measurement related variables.
*//*-------------------------------------------------------------------------*/
typedef struct
{
    measResult_t  		measured;
    offset_t     		offset;
    calibParam_t      	param;
    calibFlags_t      	flag;
	tU16 				calibCntr;
}measModule_t;

typedef struct ADC_RAW_DATA_T
{
	SWLIBS_2Syst_F16	ph1;
    SWLIBS_2Syst_F16	ph2;
    tFrac16				dcOffset;
}ADC_RAW_DATA_T;

/******************************************************************************
| Exported Variables
-----------------------------------------------------------------------------*/
extern tU16 adcRawResultArray[5];

/******************************************************************************
| Exported function prototypes
-----------------------------------------------------------------------------*/
extern tBool MEAS_Clear(measModule_t *ptr);
extern tBool MEAS_CalibCurrentSense(measModule_t *ptr);
extern tBool MEAS_Get3PhCurrent(measModule_t *ptr, SWLIBS_3Syst_FLT *i, tU16 svmSector);
extern tBool MEAS_GetUdcVoltage(measModule_t *ptr, GDFLIB_FILTER_MA_T *uDcbFilter);
extern tBool MEAS_GetIdcCurrent(measModule_t *ptr);
extern void MEAS_GetPhaseABCurrent(measModule_t *ptr, tU16 *pRawBuf);
extern void MEAS_SaveAdcRawResult(void);

/******************************************************************************
| Inline functions
-----------------------------------------------------------------------------*/

#endif /* _MEAS_S32K_H_ */
