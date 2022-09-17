/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     meas_s32k.c
*
* @date     March-28-2017
*
* @brief    Header file for measurement module
*
*******************************************************************************/
/******************************************************************************
| Includes
-----------------------------------------------------------------------------*/
#include "meas_s32k.h"

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
tU16 adcRawResultArray[5] = {0x7FF, 0x7FF, 0, 0x7FF, 0x7FF};

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

/**************************************************************************//*!
@brief			Measurement module software initialization

@param[in,out]  *ptr

@return			# true - when Module initialization ended successfully
            	# false - when Module initialization is ongoing, or error occurred

@details		Clear variables needed for both calibration as well as run time
				measurement.

@note			It is not intended to be executed when application is in run mode.

@warning
******************************************************************************/
tBool MEAS_Clear(measModule_t *ptr)
{
    ptr->measured.fltPhA.filt   	= 0.0F;
    ptr->measured.fltPhA.raw    	= 0.0F;
    ptr->measured.fltPhB.filt   	= 0.0F;
    ptr->measured.fltPhB.raw    	= 0.0F;
    ptr->measured.fltPhC.filt   	= 0.0F;
    ptr->measured.fltPhC.raw    	= 0.0F;
    ptr->measured.fltUdcb.filt  	= 0.0F;
    ptr->measured.fltUdcb.raw   	= 0.0F;
    ptr->measured.fltTemp.filt  	= 0.0F;
    ptr->measured.fltTemp.raw   	= 0.0F;
    ptr->measured.fltIdcb.filt  	= 0.0F;
    ptr->measured.fltIdcb.raw   	= 0.0F;

    ptr->offset.fltPhA.fltOffset 	= I_MAX;
	ptr->offset.fltPhB.fltOffset 	= I_MAX;
	ptr->offset.fltPhC.fltOffset 	= I_MAX;
	ptr->offset.fltIdcb.fltOffset 	= I_DCB_MAX;
    
    ptr->flag.R             		= 0;

    ptr->param.u16CalibSamples  	= 0;
    ptr->flag.B.calibInitDone   	= 0;
    ptr->flag.B.calibDone       	= 0;

    return 1;
}

/**************************************************************************//*!
@brief      	3Phase current measurement software calibration routine.

@param[in,out]  *ptr    	Pointer to structure of measurement module variables and
                        	parameters
@param[in]      svmSector	Space Vector Modulation Sector

@return     	# true - when Calibration ended successfully
            	# false - when Calibration is ongoing

@details    	This function performs offset calibration for 3 phase current measurement
				during the calibration phase of the application. It is not intended to be
				executed when application is in run mode.

@warning
******************************************************************************/
tBool MEAS_CalibCurrentSense(measModule_t *ptr)
{
	if (!(ptr->flag.B.calibInitDone))
    {
        ptr->calibCntr = 1<< (ptr->param.u16CalibSamples + 4); // +4 in order to accommodate settling time of the filter

        ptr->measured.fltPhA.filt   			= 0x0;
        ptr->measured.fltPhB.filt   			= 0x0;
        ptr->measured.fltPhC.filt   			= 0x0;
        ptr->measured.fltIdcb.filt  			= 0x0;

        ptr->offset.fltPhA.filtParam.fltAcc		= I_MAX;
        ptr->offset.fltPhB.filtParam.fltAcc		= I_MAX;
        ptr->offset.fltPhC.filtParam.fltAcc		= I_MAX;
        ptr->offset.fltIdcb.filtParam.fltAcc 	= I_DCB_MAX;

        ptr->flag.B.calibDone       			= 0;
        ptr->flag.B.calibInitDone   			= 1;
    }

    if (!(ptr->flag.B.calibDone))
    {
    	//
    	MEAS_GetPhaseABCurrent(ptr, adcRawResultArray);
    	/* --------------------------------------------------------------
         * Phase A - DC offset data filtering using MA recursive filter
         * ------------------------------------------------------------ */
    	ptr->offset.fltPhA.fltOffset	= GDFLIB_FilterMA(ptr->measured.fltPhA.raw, &ptr->offset.fltPhA.filtParam);

        /* --------------------------------------------------------------
         * Phase B - DC offset data filtering using MA recursive filter
         * ------------------------------------------------------------ */
    	ptr->offset.fltPhB.fltOffset	= GDFLIB_FilterMA(ptr->measured.fltPhB.raw, &ptr->offset.fltPhB.filtParam);

        /* --------------------------------------------------------------
         * Phase C - DC offset data filtering using MA recursive filter
         * ------------------------------------------------------------ */
    	ptr->offset.fltPhC.fltOffset	= GDFLIB_FilterMA(ptr->measured.fltPhC.raw, &ptr->offset.fltPhC.filtParam);

        /* --------------------------------------------------------------
         * DC Bus Current - DC offset data filtering using MA recursive filter
         * ------------------------------------------------------------ */
    	ptr->offset.fltIdcb.fltOffset	= GDFLIB_FilterMA(ptr->measured.fltPhA.raw, &ptr->offset.fltIdcb.filtParam);	//"ptr->measured.fltPhA.raw" contains the amplifier output of DC bus current when motor is not rotating.

        if ((--ptr->calibCntr)<=0)
        {
        	ptr->flag.B.calibDone			= 1U;    // end of DC offset calibration
        	ptr->offset.fltIdcb.fltOffset 	= GDFLIB_FilterMA(ptr->measured.fltPhA.raw, &ptr->offset.fltIdcb.filtParam);
        }
    }
    return (ptr->flag.B.calibDone);
}

/**************************************************************************//*!
@brief      	3-phase current measurement reading.

@param[in,out]  *ptr    Pointer to structure of module variables and
                        parameters

@return    		# true - when measurement ended successfully
            	# false - when measurement is ongoing, or error occurred.

@details    	This function performs measurement of three phase currents from
            	two shunt resistors. Because a non-zero length PWM pulse width
            	is required for successful current sample, this approach can not
            	be utilized up to full PWM dutycycle.

@note

@warning
******************************************************************************/
tBool MEAS_Get3PhCurrent(measModule_t *ptr, SWLIBS_3Syst_FLT *i, tU16 svmSector)
{
	MEAS_GetPhaseABCurrent(ptr, adcRawResultArray);	//Convert ADC raw integer results into float values "ptr->measured.fltPhA.raw" and "ptr->measured.fltPhB.raw"

	switch (svmSector){
		case 1:
			/* direct sensing of U, -W, calculation of V */
			i->fltArg1 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg3 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg2 = MLIB_Neg_FLT(MLIB_Add(i->fltArg1,i->fltArg3));
			break;
		case 2:
			/* direct sensing of V, -W, calculation of U */
			i->fltArg2 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg3 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg1 = MLIB_Neg_FLT(MLIB_Add(i->fltArg2,i->fltArg3));
			break;
		case 3:
			/* direct sensing of V, -U, calculation of W */
			i->fltArg2 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg1 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg3 = MLIB_Neg_FLT(MLIB_Add(i->fltArg2,i->fltArg1));
			break;
		case 4:
			/* direct sensing of W, -U, calculation of V */
			i->fltArg3 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg1 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg2 = MLIB_Neg_FLT(MLIB_Add(i->fltArg3,i->fltArg1));
			break;
		case 5:
			/* direct sensing of W, -V, calculation of U */
			i->fltArg3 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg2 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg1 = MLIB_Neg_FLT(MLIB_Add(i->fltArg3,i->fltArg2));
			break;
		case 6:
			/* direct sensing of U, -V, calculation of W */
			i->fltArg1 = MLIB_Neg_FLT(MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhA.raw));
			i->fltArg2 = MLIB_Sub(ptr->offset.fltIdcb.fltOffset,ptr->measured.fltPhB.raw);
			i->fltArg3 = MLIB_Neg_FLT(MLIB_Add(i->fltArg1,i->fltArg2));
			break;
		default:
			i->fltArg1 = 0.0F;
			i->fltArg2 = 0.0F;
			i->fltArg3 = 0.0F;

			break;
	}

	return(1);
}

/**************************************************************************//*!
@brief      	DCB Voltage measurement routine.

@param[in,out]  *ptr    Pointer to structure of module variables and
                        parameters

@return     	# true - when measurement ended successfully
            	# false - when measurement is ongoing, or error occurred.

@details    	This function performs measurement of DCBus Voltage.
            
@note

@warning
******************************************************************************/
tBool MEAS_GetUdcVoltage(measModule_t *ptr, GDFLIB_FILTER_MA_T *uDcbFilter)
{
	uint16_t DCBus_Voltage;

    //DC Bus Voltage raw ADC data is already stored into adcRawResultArray[2] in function MEAS_SaveAdcRawResult();
	DCBus_Voltage = adcRawResultArray[2];
	ptr->measured.fltUdcb.raw	= MLIB_Mul((tFloat)(DCBus_Voltage & 0x00000FFF), MLIB_Div(U_DCB_MAX,4095.0F));
	ptr->measured.fltUdcb.filt	= GDFLIB_FilterMA(ptr->measured.fltUdcb.raw, uDcbFilter);

    return(1);
}

/**************************************************************************//*!
@brief      	DCB Current measurement routine.

@param[in,out]  *ptr    Pointer to structure of module variables and
                        parameters

@return     	# true - when measurement ended successfully
            	# false - when measurement is ongoing, or error occurred.

@details    	This function performs measurement of DCBus current.

@note

@warning
******************************************************************************/
tBool MEAS_GetIdcCurrent(measModule_t *ptr)
{
	ptr->measured.fltIdcb.raw	= ptr->measured.fltPhA.raw;
	ptr->measured.fltIdcb.filt	= MLIB_Sub(ptr->measured.fltIdcb.raw, ptr->offset.fltIdcb.fltOffset);

    return(1);
}

/**************************************************************************//*!
@brief      	Read ADC results registers and clear ADC flags;

@param[in,out]  adcRawResultArray[], output, save ADC result registers values into the array adcRawResultArray[].

@return     	none

@note

@warning
******************************************************************************/
void MEAS_SaveAdcRawResult(void)
{
	 adcRawResultArray[0] = ((ADC1->R[0]) & ADC_R_D_MASK) >> ADC_R_D_SHIFT;
	 adcRawResultArray[1] = ((ADC1->R[1]) & ADC_R_D_MASK) >> ADC_R_D_SHIFT;
	 adcRawResultArray[2] = ((ADC1->R[2]) & ADC_R_D_MASK) >> ADC_R_D_SHIFT;
	 adcRawResultArray[3] = ((ADC1->R[3]) & ADC_R_D_MASK) >> ADC_R_D_SHIFT;
	 adcRawResultArray[4] = ((ADC1->R[4]) & ADC_R_D_MASK) >> ADC_R_D_SHIFT;
}

/***************************************************************************//*!
@brief Read raw values from adc and remove offset obtained in calibration function

@param[in,out]  *ptr    	output, Pointer to structure of module variables and
                        	parameters
                *pRawBuf    input, this buffer contain 5 ADC results,
                			pRawBuf[0,1,3,4] are DC bus currents; pRawBuf[2] is DC bus voltage;

@return         void

@details		convert raw ADC results from counts to float values, and save in "meas" structure;
******************************************************************************/
void MEAS_GetPhaseABCurrent(measModule_t *ptr, tU16 *pRawBuf)
{
	tU32 PhaseCurrent1, PhaseCurrent2;

	PhaseCurrent1 = ((pRawBuf[0] + pRawBuf[4]) >> 1);		//average of result 0 and result 4 is one phase current
	PhaseCurrent2 = ((pRawBuf[1] + pRawBuf[3]) >> 1);		//average of result 1 and result 3 is another phase current

	ptr->measured.fltPhA.raw = MLIB_Mul((tFloat)PhaseCurrent1, MLIB_Div(I_DCB_MAX,2048.0F));
	ptr->measured.fltPhB.raw = MLIB_Mul((tFloat)PhaseCurrent2, MLIB_Div(I_DCB_MAX,2048.0F));

}
/* End of file */
