/* ###################################################################
**     Filename    : main.c
**     Processor   : S32K14x
**     Abstract    :
**         Main module.
**         This module contains user's application code.
**     Settings    :
**     Contents    :
**         No public methods
**
** ###################################################################*/
/*!
** @file main.c
** @version 01.00
** @brief
**         Main module.
**         This module contains user's application code.
*/
/*!
**  @addtogroup main_module main module documentation
**  @{
*/
/* MODULE main */


/* Including necessary module. Cpu.h contains other modules needed for compiling.*/
#include "Cpu.h"
#include "pin_mux.h"
#include "clockMan1.h"
#include "pwrMan1.h"
#include "trgmux1.h"
#include "lpuart1.h"
#include "dmaController1.h"
#include "lpspiCom1.h"
#include "flexTimer_pwm3.h"
#include "flexTimer_qd2.h"
#include "pdb1.h"
#include "adConv1.h"

volatile int exit_code = 0;

/* User includes (#include below this line is not maintained by Processor Expert) */

#include "peripherals_config.h"
#include "gd3000_init.h"
#include "freemaster.h"
#include "PMSM_appconfig.h"
#include "actuate_s32k.h"
#include "meas_s32k.h"
#include "motor_structure.h"
#include "state_machine.h"
#include "pospe_sensor.h"
#include "amclib.h"
#include "aml/common_aml.h"
#include "aml/gpio_aml.h"
#include "tpp/tpp.h"
#include "PMSM_appfreemaster_TSA.h"

/*******************************************************************************
* Global variables
*******************************************************************************/


/*******************************************************************************
* Local variables
*******************************************************************************/
pmsmDrive_t         drvFOC;         // Field Oriented Control Variables
driveStates_t 	    cntrState;	    // Responsible for stateMachine state propagation
appFaultStatus_t    tempfaults;		// Temporary faults to be indicated inhere
appFaultStatus_t    permFaults;		// Permanent faults to be indicated inhere
fm_scale_t          fmScale;		// Scales for freeMASTER interface
measModule_t        meas;			// Variables measured by ADC modules
tPos_mode           pos_mode;		// Variable defining position mode
gd3000Status_t      gd3000Status;	// GD3000 status variables
tpp_drv_config_t    tppDrvConfig;	// GD3000 configuration structure
tBool               statePWM;		// Status of the PWM update
pdbStatus_t         pdbStatus;		// PDB0 and PDB1 status tracking
tBool               fieldWeakOnOff; // Enable/Disable Field Weakening
encoderPospe_t      encoderPospe;	// Encoder position and speed
switchSensor_t      switchSensor;	// Position sensor selector

static void MCAT_Init();

static tBool FocFastLoop(void);
static tBool FocSlowLoop(void);
static tBool FaultDetection();

tBool AutomaticMode(void);
static tBool CalcOpenLoop(openLoopPospe_t *openLoop, tFloat speedReqRamp);

// Open Loop and Closed loop speed ramp variants
volatile tFloat     OL_SpeedRampInc = 0.0F, CL_SpeedRampInc = 0.0F, CL_SpeedRampDec = 0.0F;
volatile tFloat     FW_PropGainControl = 0.0F, FW_IntegGainControl = 0.0F;
volatile tFloat     UDQVectorSum = 0.0F;

/*****************************************************************************
* Define Motor with or without Encoder sensor
*
* ENCODER  0 		PM motor is NOT equipped with Encoder sensor, motor position/speed is estimated only by eBEMF observer
* ENCODER  1 		PM motor is equipped with Encoder sensor, motor position/speed can either be estimated by eBEMF observer
* 					or obtained by Encoder sensor. To switch between Sensorless and Encoder mode use switchSensor variable.
*
* Note				Default PM motor of the MTRDEVKSPNK144 (LINIX 45ZWN24-40) is NOT equipped with Encoder sensor,
* 					thus ENCODER 0 is used as default for MTRDEVKSPNK144
******************************************************************************/
#define ENCODER		0

/*!
  \brief The main function for the project.
  \details The startup initialization sequence is the following:
 * - startup asm routine
 * - main()
*/

int main(void)
{
	/* Write your local variable definition here */

	// MCU peripherals initialization
 	McuClockConfig();
 	McuCacheConfig();
	McuPowerConfig();
 	McuIntConfig();
	McuTrigmuxConfig();
 	McuPinsConfig();
 	McuLpuartConfig();
 	McuAdcConfig();
 	McuPdbConfig();
	McuFtmConfig();

    // FreeMASTER initialization
	FMSTR_Init();

    // MC34GD3000 initialization
    GD3000_Init();

    // MCAT variables initialization
    MCAT_Init();

    // Clear measured variables
    MEAS_Clear(&meas);

    // Application starts from init state
    cntrState.state   	= init;
    cntrState.event 	= e_init;

    // Default setting of a FOC Control Mode
    cntrState.usrControl.FOCcontrolMode		= speedControl;

    // Application starts by FTM3 initialization trigger
    FTM_RMW_EXTTRIG_REG(FTM3, 0x00, 0x40);

    /*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/
    #ifdef PEX_RTOS_INIT
    PEX_RTOS_INIT();                   /* Initialization of the selected RTOS. Macro is defined by the RTOS component. */
    #endif
    /*** End of Processor Expert internal initialization.                    ***/

    /* Write your code here */
    /* For example: for(;;) { } */
    for(;;)
    {
    	/* FreeMASTER */
    	FMSTR_Poll();

    	// Read GD3000 Status register 0 and Status register 1, if there is GD3000 interrupt
    	if(gd3000Status.B.gd3000IntFlag)
    	{
    		gd3000Status.B.gd3000IntFlag = false;
    		TPP_GetStatusRegister(&tppDrvConfig, tppSR0_deviceEvents, &(tppDrvConfig.deviceConfig.statusRegister[0U]));
    	}

    	// Clear GD3000 Errors
    	if(gd3000Status.B.gd3000ClearErr)
    	{
    		gd3000Status.B.gd3000ClearErr = false;
    		permFaults.gd3000 = false;
    		tppDrvConfig.deviceConfig.statusRegister[0U] = 0U;
    		TPP_ClearInterrupts(&tppDrvConfig, TPP_CLINT0_MASK, TPP_CLINT1_MASK);
    	}

    	// Enter fault state, if there are PDBs sequence errors
    	if(permFaults.mcu.B.PDB0_Error || permFaults.mcu.B.PDB1_Error)
    	{
    		FTM_RMW_EXTTRIG_REG(FTM3, 0x40, 0x00);

    		// Enter fault state
    		cntrState.event = e_fault;
    		StateTable[cntrState.event][cntrState.state]();
    		StateLED[cntrState.state]();
    	}
    }

    /*** Don't write any code pass this line, or it will be deleted during code generation. ***/
  /*** RTOS startup code. Macro PEX_RTOS_START is defined by the RTOS component. DON'T MODIFY THIS CODE!!! ***/
  #ifdef PEX_RTOS_START
    PEX_RTOS_START();                  /* Startup of the selected RTOS. Macro is defined by the RTOS component. */
  #endif
  /*** End of RTOS startup code.  ***/
  /*** Processor Expert end of main routine. DON'T MODIFY THIS CODE!!! ***/
  for(;;) {
    if(exit_code != 0) {
      break;
    }
  }
  return exit_code;
  /*** Processor Expert end of main routine. DON'T WRITE CODE BELOW!!! ***/
} /*** End of main routine. DO NOT MODIFY THIS TEXT!!! ***/

/* END main */
/*!
** @}
*/
/*
** ###################################################################
**
**     This file was created by Processor Expert 10.1 [05.21]
**     for the Freescale S32K series of microcontrollers.
**
** ###################################################################
*/

/*******************************************************************************
*
* Function: 	TPP_InitializeOutputs(void)
*
* Description:  This function initialize output of the MC34GD3000 MOSFET Pre-driver
*
*******************************************************************************/
void TPP_InitializeOutputs(void)
{

}

/*******************************************************************************
* Interrupt Routines
*******************************************************************************/
/*******************************************************************************
*
* Function: 	PORTE_IRQHandler(void)
*
* Description:  PORTE Interrupt Service Routine
*
*******************************************************************************/
void PORTE_IRQHandler(void)
{
	if((PINS_DRV_GetPortIntFlag(PORTE) & (1<<10u)) && gd3000Status.B.gd3000InitDone)
	{
		gd3000Status.B.gd3000IntFlag = true;
	}

	PINS_DRV_ClearPinIntFlagCmd(PORTE, 10u);

	gd3000Status.B.gd3000InitDone = true;
}

/*******************************************************************************
*
* Function: 	PDB1_IRQHandler(void)
*
* Description:  PDB1 Interrupt Service Routine
*
*******************************************************************************/
void PDB1_IRQHandler(void)
{
	// PDB1 Sequence error interrupt
	if(PDB_DRV_GetAdcPreTriggerSeqErrFlags(INST_PDB1, 0, 0xFF))
	{
		// Indicates PDB1 Sequence error
		permFaults.mcu.B.PDB1_Error = true;
		// Get PDB1 Sequence error flags
		pdbStatus.PDB1_SeqErrFlags = PDB_DRV_GetAdcPreTriggerSeqErrFlags(INST_PDB1, 0, 0xFF);

		// Disable PDB1
		PDB_DRV_Disable(INST_PDB1);

		// Clear PDB1 sequence errors
		PDB_DRV_ClearAdcPreTriggerSeqErrFlags(INST_PDB1, 0, 0xFF);

		// Enable PDB1
		PDB_DRV_Enable(INST_PDB1);
	}

	// PDB1 timer overflow interrupt
	if (PDB_DRV_GetTimerIntFlag(INST_PDB1))
	{
		// Enable FTM initialization trigger to trigger ADC modules
		// every control loop (150uS). Ignore if there are PDBs sequence errors
		if(!(permFaults.mcu.B.PDB0_Error || permFaults.mcu.B.PDB1_Error))
		{
			FTM_RMW_EXTTRIG_REG(FTM3, 0x00, 0x40);		//enable FMT3_INIT_TRIG output
			ACTUATE_PwmUpdateBuffer();		//Update PWM edge values from buffer of FOC calculated results to buffer used for FTM registers update
			ACTUATE_PwmUpdateRegisters();				//Update FTM PWM edge registers values with global variables;
			//we update FTM registers here because it's just close to the end of 150uS control loop,
			//and we want to use same set of FTM PWM edges values in each 150uS control loop;
			//at 125uS, the FTM PWM registers were already updated in FTM reload ISR;
			//here we update the FTM PWM registers again but with latest FOC calculated values.
		}

		PDB_DRV_ClearTimerIntFlag(INST_PDB1);
	}
}

/*******************************************************************************
*
* Function: 	ADC1_IRQHandler()
*
* Description:  ADC1 interrupt service routine
*
*******************************************************************************/
__attribute__((section (".code_ram"))) 		// inserting function to the RAM section
void ADC1_IRQHandler()
{
	static tBool getFcnStatus;

	// Board buttons to control the application from board
	cntrState.usrControl.btSpeedUp = ((PINS_DRV_ReadPins(PTC)  >> 12) & 1);
	cntrState.usrControl.btSpeedDown = ((PINS_DRV_ReadPins(PTC)  >> 13) & 1);

	// Board buttons control logic
	BoardButtons();

	// User accessible switch for stopping the application.
	if (cntrState.usrControl.switchAppOnOff ^ cntrState.usrControl.switchAppOnOffState)
	{
		cntrState.usrControl.switchAppOnOffState  = cntrState.usrControl.switchAppOnOff;
		cntrState.event   = (cntrState.usrControl.switchAppOnOff) ? e_app_on: e_app_off;
	}

	getFcnStatus    =    true;

	// Set pin to measure TOTAL execution time
    //PTD->PSOR |= 1<<2;

	//save ADC1 results to buffer adcRawResultArray[], and clear ADC conversion complete flag;
	MEAS_SaveAdcRawResult();

	// DCB voltage, DCB current and phase currents measurement
	getFcnStatus &= MEAS_Get3PhCurrent(&meas, &drvFOC.iAbcFbck, drvFOC.svmSector);
	getFcnStatus &= MEAS_GetIdcCurrent(&meas);
	getFcnStatus  = MEAS_GetUdcVoltage(&meas, &drvFOC.uDcbFilter);

	drvFOC.fltUdcb = meas.measured.fltUdcb.filt;
	drvFOC.fltIdcb = meas.measured.fltIdcb.filt;
	
#if ENCODER
    // Get rotor position and speed from Encoder sensor
	POSPE_GetPospeElEnc(&encoderPospe);
#endif

	// Fault detection routine, must be executed prior application state machine
	getFcnStatus &= FaultDetection();

	if (getFcnStatus)    cntrState.event = e_fault;

	// Execute State table with newly measured data
	StateTable[cntrState.event][cntrState.state]();

	// Clear pin to measure TOTAL execution time
	//PTD->PCOR |= 1<<2;

	StateLED[cntrState.state]();
	FMSTR_Recorder();
}

/***************************************************************************//*!
*
* @brief	FTM3 counter overflow ISR
*
* @param	none
*
* @return	none
*
* @details	It update the FTM3 CnV registers - even cycle and odd cycle use different values
*           And if the FTM_EXT_TRIG is enabled, this ISR will disable it for next 5x25us period;
*           The FTM_EXT_TRIG will be re-enabled in PDB1_ISR; FTM_EXT_TRIG is enabled every 150uS;
*
******************************************************************************/
__attribute__((section (".code_ram"))) 		// inserting function to the RAM section
void FTM3_Ovf_Reload_IRQHandler(void)
{
	if(FTM3->EXTTRIG & FTM_EXTTRIG_INITTRIGEN_MASK)
	{
		FTM_RMW_EXTTRIG_REG(FTM3, 0x40, 0x00);	//disable FTM3 INIT trigger output
		pwmCycleCnt = 1;
	}

	//increment cycle counter, it's used to update FTM registers with odd cycle values or even cycle values;
	//this increment must be done before calling function ACTUATE_PwmUpdateRegisters() not after calling it;
	pwmCycleCnt ++;

	//This function updates FTM PWM edge registers with global variables pwmEdgesFtm
	ACTUATE_PwmUpdateRegisters();
	FTM_DRV_ClearStatusFlags(INST_FLEXTIMER_PWM3, FTM_TIME_OVER_FLOW_FLAG);

	return;
}

/***************************************************************************//*!
*
* @brief   MCAT Initialization
*
* @param
*
* @return  none
*
******************************************************************************/
void MCAT_Init()
{
	/*------------------------------------
	 * Freemaster variables
	 * ----------------------------------*/
	fmScale.speed_n_m						= FM_SPEED_RPM_MEC_SCALE;
	fmScale.position						= FM_POSITION_DEG_SCALE;

    drvFOC.alignCntr						= ALIGN_DURATION;
	drvFOC.alignVoltage						= ALIGN_VOLTAGE;

    /*------------------------------------
     * Currents
     * ----------------------------------*/
    drvFOC.iAbcFbck.fltArg1					= 0.0F;
    drvFOC.iAbcFbck.fltArg2                	= 0.0F;
    drvFOC.iAbcFbck.fltArg3                	= 0.0F;

    drvFOC.iAlBeFbck.fltArg1               	= 0.0F;
    drvFOC.iAlBeFbck.fltArg2               	= 0.0F;

    drvFOC.iDQReqInLoop.fltArg1             = 0.0F;
    drvFOC.iDQReqInLoop.fltArg2             = 0.0F;

    drvFOC.iDQReqOutLoop.fltArg1            = 0.0F;
    drvFOC.iDQReqOutLoop.fltArg2            = 0.0F;

    drvFOC.iDQFbck.fltArg1                 	= 0.0F;
    drvFOC.iDQFbck.fltArg2                	= 0.0F;

    /*------------------------------------
     * Voltages
     * ----------------------------------*/
    drvFOC.uAlBeReq.fltArg1                 = 0.0F;
    drvFOC.uAlBeReq.fltArg2                	= 0.0F;

    drvFOC.uAlBeReqDCB.fltArg1             	= 0.0F;
    drvFOC.uAlBeReqDCB.fltArg2             	= 0.0F;

    drvFOC.uDQReq.fltArg1                 	= 0.0F;
    drvFOC.uDQReq.fltArg2                  	= 0.0F;

    /*------------------------------------
     * Speed/Position
     * ----------------------------------*/
    drvFOC.thTransform.fltArg1             	= 0.0F;
    drvFOC.thTransform.fltArg2             	= 0.0F;

    /*------------------------------------
     * SVC-PWM variables
     * ----------------------------------*/
    drvFOC.svmSector                       	= 1;

    drvFOC.pwmflt.fltArg1                   = 0.0F;
    drvFOC.pwmflt.fltArg2                   = 0.0F;
    drvFOC.pwmflt.fltArg3                   = 0.0F;

	/*------------------------------------
     * FOC variables
     * ----------------------------------*/

    // D-axis PI controller
    drvFOC.CurrentLoop.pPIrAWD.fltCC1sc             = D_CC1SC;
    drvFOC.CurrentLoop.pPIrAWD.fltCC2sc             = D_CC2SC;
    drvFOC.CurrentLoop.pPIrAWD.fltLowerLimit        = MLIB_Neg(CLOOP_LIMIT);
    drvFOC.CurrentLoop.pPIrAWD.fltUpperLimit        = CLOOP_LIMIT;

    // Q-axis PI controller
    drvFOC.CurrentLoop.pPIrAWQ.fltCC1sc             = Q_CC1SC;
    drvFOC.CurrentLoop.pPIrAWQ.fltCC2sc             = Q_CC2SC;
    drvFOC.CurrentLoop.pPIrAWQ.fltLowerLimit        = MLIB_Neg(CLOOP_LIMIT);
    drvFOC.CurrentLoop.pPIrAWQ.fltUpperLimit        = CLOOP_LIMIT;

    drvFOC.CurrentLoop.pIDQReq  					= &drvFOC.iDQReqInLoop;
    drvFOC.CurrentLoop.pIDQFbck 					= &drvFOC.iDQFbck;

    // Clear AMCLIB_CurrentLoop state variables
	AMCLIB_CurrentLoopInit_FLT(&drvFOC.CurrentLoop);

    // DCBus 1st order filter; Fcut = 100Hz, Ts = 100e-6
    drvFOC.uDcbFilter.fltLambda 					= MLIB_Div(1.0F, 8.0F);
    GDFLIB_FilterMAInit_FLT(&drvFOC.uDcbFilter);
    drvFOC.uDcbFilter.fltAcc 						= 12.0F;

    drvFOC.elimDcbRip.fltModIndex          			= 0.866025403784439F;
    drvFOC.elimDcbRip.fltArgDcBusMsr       			= 0.0F;

    OL_SpeedRampInc = OL_START_RAMP_INC;
    CL_SpeedRampInc = SPEED_RAMP_UP;
    CL_SpeedRampDec = SPEED_RAMP_DOWN;

	drvFOC.FwSpeedLoop.pRamp.fltRampUp				= OL_SpeedRampInc;
	drvFOC.FwSpeedLoop.pRamp.fltRampDown			= OL_SpeedRampInc;

	drvFOC.FwSpeedLoop.pPIpAWQ.fltPropGain			= SPEED_PI_PROP_GAIN;
	drvFOC.FwSpeedLoop.pPIpAWQ.fltIntegGain			= SPEED_PI_INTEG_GAIN;
	drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit		= SPEED_LOOP_HIGH_LIMIT;
	drvFOC.FwSpeedLoop.pPIpAWQ.fltLowerLimit		= SPEED_LOOP_LOW_LIMIT;
	drvFOC.FwSpeedLoop.pFilterW.fltLambda			= POSPE_SPEED_FILTER_MA_LAMBDA;

	/* Field weakening FilterMA */
	drvFOC.FwSpeedLoop.pFilterFW.fltLambda			= 1.0F;
	/* Field weakening PI controller */
	drvFOC.FwSpeedLoop.pPIpAWFW.fltPropGain			= SPEED_PI_PROP_GAIN;
	drvFOC.FwSpeedLoop.pPIpAWFW.fltIntegGain		= SPEED_PI_INTEG_GAIN;
	FW_PropGainControl								= SPEED_PI_PROP_GAIN;
	FW_IntegGainControl								= SPEED_PI_INTEG_GAIN;
	drvFOC.FwSpeedLoop.pPIpAWFW.fltUpperLimit		= 0.0F;
	drvFOC.FwSpeedLoop.pPIpAWFW.fltLowerLimit		= MLIB_Neg(FLOAT_PI_DIVBY_2);
	/* Input/output pointers */
	drvFOC.FwSpeedLoop.pIQFbck						= &(drvFOC.iDQFbck.fltArg2);
	drvFOC.FwSpeedLoop.pUQLim						= &(drvFOC.CurrentLoop.pPIrAWQ.fltUpperLimit);
	drvFOC.FwSpeedLoop.pUQReq						= &(drvFOC.uDQReq.fltArg2);
	/* Field weakening PI controller */
	drvFOC.FwSpeedLoop.fltUmaxDivImax				= MLIB_Div(U_DCB_MAX, I_MAX);

	// Clear AMCLIB_SpeedLoop state variables
  	AMCLIB_FWSpeedLoopInit_FLT(&drvFOC.FwSpeedLoop);

    // Position observer
    drvFOC.pospeSensorless.wRotEl   	   						= 0.0F;
    drvFOC.pospeSensorless.thRotEl		   						= 0.0F;
    drvFOC.pospeSensorless.DQtoGaDeError   						= 0.0F;

    // back-EMF observer parameters - D-axis
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltCC1sc 			= BEMF_DQ_CC1_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltCC2sc 			= BEMF_DQ_CC2_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltUpperLimit 		= FLOAT_MAX;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltLowerLimit 		= FLOAT_MIN;
    // back-EMF observer parameters - Q-axis
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltCC1sc 			= BEMF_DQ_CC1_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltCC2sc 			= BEMF_DQ_CC2_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltUpperLimit 		= FLOAT_MAX;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltLowerLimit 		= FLOAT_MIN;
    // back-EMF observer parameters - Scale constants
    drvFOC.pospeSensorless.bEMFObs.fltIGain 					= I_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltUGain 					= U_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltEGain 					= E_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltWIGain			 		= WI_Gain;

    /* Clear back-EMF observer state variables */
    AMCLIB_BemfObsrvDQInit_FLT(&drvFOC.pospeSensorless.bEMFObs);

    // ATO observer - Controller parameters
    drvFOC.pospeSensorless.TrackObsrv.pParamPI.fltCC1sc 		= TO_CC1SC;
    drvFOC.pospeSensorless.TrackObsrv.pParamPI.fltCC2sc 		= TO_CC2SC;
    drvFOC.pospeSensorless.TrackObsrv.pParamPI.fltUpperLimit 	= FLOAT_MAX;
    drvFOC.pospeSensorless.TrackObsrv.pParamPI.fltLowerLimit 	= FLOAT_MIN;
    // ATO observer - Integrator parameters
    drvFOC.pospeSensorless.TrackObsrv.pParamInteg.fltC1 		= TO_THETA_GAIN;

    /* Clear ATO observer state variables */
    AMCLIB_TrackObsrvInit_FLT(&drvFOC.pospeSensorless.TrackObsrv);

    // Encoder ATO observer - Controller parameters
    encoderPospe.TrackObsrv.pParamPI.fltCC1sc					= POSPE_ENC_TO_CC1;
    encoderPospe.TrackObsrv.pParamPI.fltCC2sc					= POSPE_ENC_TO_CC2;
    encoderPospe.TrackObsrv.pParamPI.fltUpperLimit				= FLOAT_MAX;
    encoderPospe.TrackObsrv.pParamPI.fltLowerLimit				= FLOAT_MIN;
    // Encoder observer - Integrator parameters
    encoderPospe.TrackObsrv.pParamInteg.fltC1					= POSPE_ENC_TO_INTEG_GAIN;

    /* Clear ATO observer state variables */
    AMCLIB_TrackObsrvInit_FLT(&encoderPospe.TrackObsrv);

    drvFOC.pospeSensorless.wRotEl			= 0.0F;
    drvFOC.pospeSensorless.thRotEl			= 0.0F;
    
    drvFOC.pospeSensorless.wRotElMatch_1	= MLIB_Div(MLIB_Mul(MERG_SPEED_1_TRH, MLIB_Mul(FLOAT_2_PI, MOTOR_PP)), 60.0F);
    drvFOC.pospeSensorless.wRotElMatch_2	= MLIB_Div(MLIB_Mul(MERG_SPEED_2_TRH, MLIB_Mul(FLOAT_2_PI, MOTOR_PP)), 60.0F);

    drvFOC.pospeSensorless.iQUpperLimit		= SPEED_LOOP_HIGH_LIMIT;
    drvFOC.pospeSensorless.iQLowerLimit		= SPEED_LOOP_LOW_LIMIT;

    drvFOC.pospeOpenLoop.integ.f32InK1 		= 0;
    drvFOC.pospeOpenLoop.integ.f32State 	= 0;
    drvFOC.pospeOpenLoop.integ.f32C1 		= SCALAR_INTEG_GAIN;
    drvFOC.pospeOpenLoop.integ.u16NShift	= SCALAR_INTEG_SHIFT;

    drvFOC.pospeOpenLoop.thRotEl			= 0.0F;
    drvFOC.pospeOpenLoop.wRotEl				= 0.0F;

    drvFOC.pospeOpenLoop.iQUpperLimit		= OL_START_I;
    drvFOC.pospeOpenLoop.iQLowerLimit		= MLIB_Neg(drvFOC.pospeOpenLoop.iQUpperLimit);

    // Default state set according to the selected sensor
    if(switchSensor==encoder)
    {
        cntrState.usrControl.controlMode		= automatic;
        pos_mode								= encoder1;
    }
    else
    {
        cntrState.usrControl.controlMode		= automatic;
        pos_mode								= force;
    }

}

/***************************************************************************//*!
*
* @brief   FAULT state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateFault()
{
	uint16_t adc_r;
    /*-----------------------------------------------------
    Application State Machine - state identification
    ----------------------------------------------------- */
    // Entering state fault
	cntrState.state   = fault;
	cntrState.event   = e_fault;

	// Turn off PWM
	statePWM = ACTUATE_DisableOutput();

	// Indicate State Machine fault state invoked by irrelevant event call
	if((permFaults.mcu.R == 0)&&(permFaults.motor.R == 0)&&(permFaults.stateMachine.R == 0)&&(permFaults.gd3000 == 0))
		permFaults.stateMachine.B.FOCError = 1;

    // Disable user application switch
    cntrState.usrControl.switchAppOnOff      = false;
    cntrState.usrControl.switchAppOnOffState = false;

    if (cntrState.usrControl.switchFaultClear)
    {
		// Clear permanent and temporary SW faults
		permFaults.mcu.R          		= 0;    		// Clear mcu faults
		permFaults.motor.R		  		= 0;	  		// Clear motor faults
		permFaults.stateMachine.R 		= 0;	  		// Clear state machine faults
		gd3000Status.B.gd3000ClearErr	= true;			// Clear GD3000 faults
		pdbStatus.PDB0_SeqErrFlags 		= 0;			// Clear PDB0 sequence error flags
		pdbStatus.PDB1_SeqErrFlags 		= 0;			// Clear PDB1 sequence error flags

		// When all Faults cleared prepare for transition to next state.
		cntrState.usrControl.readFault             = true;
		cntrState.usrControl.switchFaultClear      = false;
		cntrState.event                            = e_fault_clear;

		// Read ADCs Results registers to unlock PDB pre-triggers lock states
		ADC_DRV_GetChanResult(INST_ADCONV1, 0, &adc_r);
		ADC_DRV_GetChanResult(INST_ADCONV1, 1, &adc_r);
		ADC_DRV_GetChanResult(INST_ADCONV1, 2, &adc_r);
		ADC_DRV_GetChanResult(INST_ADCONV1, 3, &adc_r);
		ADC_DRV_GetChanResult(INST_ADCONV1, 4, &adc_r);

		// Enable FTM INIT trigger after clearing faults and errors
		FTM_RMW_EXTTRIG_REG(FTM3, 0x00, 0x40);
    }
}
/***************************************************************************//*!
*
* @brief   INIT state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateInit()
{
    /*-----------------------------------------------------
    Application State Machine - state identification
    ----------------------------------------------------- */
    tBool InitFcnStatus;
    cntrState.state                             	= init;
    cntrState.event								 	= e_init;

    // Turn off PWM output
    statePWM = ACTUATE_DisableOutput();

    /*------------------------------------
     * General use variables
     * ----------------------------------*/
    InitFcnStatus                                	= false;

    /*------------------------------------
     * Application state machine variables
     * ----------------------------------*/
    // Reset state of all user control variables
    cntrState.usrControl.switchAppOnOff           	= false;
    cntrState.usrControl.switchAppOnOffState      	= false;
    cntrState.usrControl.switchFaultClear          	= false;
    cntrState.usrControl.switchAppReset            	= false;

    cntrState.usrControl.ledCounter					= 0;
    cntrState.usrControl.ledFlashing				= 1250;


    drvFOC.pospeControl.speedLoopCntr               = 0;

    drvFOC.alignCntr								= ALIGN_DURATION;

    InitFcnStatus = MEAS_Clear(&meas);

    meas.param.u16CalibSamples                     	= 10.0F;    // number of samples = 2^u16CalibSamples
    meas.offset.fltPhA.filtParam.fltLambda		   	= MLIB_Div(1.0F,(tFloat)(meas.param.u16CalibSamples));
	meas.offset.fltPhB.filtParam.fltLambda		   	= MLIB_Div(1.0F,(tFloat)(meas.param.u16CalibSamples));
	meas.offset.fltPhC.filtParam.fltLambda		   	= MLIB_Div(1.0F,(tFloat)(meas.param.u16CalibSamples));
	meas.offset.fltIdcb.filtParam.fltLambda		   	= MLIB_Div(1.0F,(tFloat)(meas.param.u16CalibSamples));

    /*------------------------------------
     * Currents
     * ----------------------------------*/
    drvFOC.iAbcFbck.fltArg1               	= 0.0F;
    drvFOC.iAbcFbck.fltArg2               	= 0.0F;
    drvFOC.iAbcFbck.fltArg3                	= 0.0F;

    drvFOC.iAlBeFbck.fltArg1              	= 0.0F;
    drvFOC.iAlBeFbck.fltArg2               	= 0.0F;
    
    drvFOC.iDQReqInLoop.fltArg1             = 0.0F;
    drvFOC.iDQReqInLoop.fltArg2             = 0.0F;

    drvFOC.iDQReqOutLoop.fltArg1            = 0.0F;
    drvFOC.iDQReqOutLoop.fltArg2            = 0.0F;
    

    drvFOC.iDQFbck.fltArg1                  = 0.0F;
    drvFOC.iDQFbck.fltArg2                  = 0.0F;

    /*------------------------------------
     * Voltages
     * ----------------------------------*/
    drvFOC.uAlBeReq.fltArg1                	= 0.0F;
    drvFOC.uAlBeReq.fltArg2                	= 0.0F;

    drvFOC.uAlBeReqDCB.fltArg1             	= 0.0F;
    drvFOC.uAlBeReqDCB.fltArg2             	= 0.0F;

    drvFOC.uDQReq.fltArg1                  	= 0.0F;
    drvFOC.uDQReq.fltArg2                  	= 0.0F;

    /*------------------------------------
     * Speed/Position
     * ----------------------------------*/
    drvFOC.thTransform.fltArg1             	= 0.0F;
    drvFOC.thTransform.fltArg2             	= 0.0F;

    /*------------------------------------
     * SVC-PWM variables
     * ----------------------------------*/
    drvFOC.svmSector                       	= 1;

    drvFOC.pwmflt.fltArg1                 	= 0.0F;
    drvFOC.pwmflt.fltArg2                 	= 0.0F;
    drvFOC.pwmflt.fltArg3                 	= 0.0F;

    /*------------------------------------
     * FOC variables re-init
     * ----------------------------------*/

    // D-axis PI controller
    drvFOC.CurrentLoop.pPIrAWD.fltCC1sc             = D_CC1SC;
    drvFOC.CurrentLoop.pPIrAWD.fltCC2sc             = D_CC2SC;
    drvFOC.CurrentLoop.pPIrAWD.fltLowerLimit        = MLIB_Neg(CLOOP_LIMIT);
    drvFOC.CurrentLoop.pPIrAWD.fltUpperLimit        = CLOOP_LIMIT;

    // Q-axis PI controller
    drvFOC.CurrentLoop.pPIrAWQ.fltCC1sc             = Q_CC1SC;
    drvFOC.CurrentLoop.pPIrAWQ.fltCC2sc             = Q_CC2SC;
    drvFOC.CurrentLoop.pPIrAWQ.fltLowerLimit        = MLIB_Neg(CLOOP_LIMIT);
    drvFOC.CurrentLoop.pPIrAWQ.fltUpperLimit        = CLOOP_LIMIT;

    drvFOC.CurrentLoop.pIDQReq  					= &drvFOC.iDQReqInLoop;
    drvFOC.CurrentLoop.pIDQFbck 					= &drvFOC.iDQFbck;

    // Clear AMCLIB_CurrentLoop state variables
	AMCLIB_CurrentLoopInit_FLT(&drvFOC.CurrentLoop);

    // DCBus 1st order filter; Fcut = 100Hz, Ts = 100e-6
    drvFOC.uDcbFilter.fltLambda                     = MLIB_Div(1.0F, 8.0F);
    GDFLIB_FilterMAInit_FLT(&drvFOC.uDcbFilter);
    drvFOC.uDcbFilter.fltAcc                        = 12.0F;

    drvFOC.elimDcbRip.fltModIndex          			= 0.866025403784439F;
    drvFOC.elimDcbRip.fltArgDcBusMsr       			= 0.0F;

    drvFOC.FwSpeedLoop.pRamp.fltRampUp	   			= OL_SpeedRampInc;
    drvFOC.FwSpeedLoop.pRamp.fltRampDown	   		= OL_SpeedRampInc;
    drvFOC.FwSpeedLoop.pFilterW.fltLambda	   		= POSPE_SPEED_FILTER_MA_LAMBDA;
    fieldWeakOnOff = true;

    AMCLIB_FWSpeedLoopInit_FLT(&drvFOC.FwSpeedLoop);

    drvFOC.pospeControl.wRotEl			   			= 0.0F;

    // Position observer
    drvFOC.pospeSensorless.wRotEl   	   			= 0.0F;
    drvFOC.pospeSensorless.thRotEl		  			= 0.0F;
    drvFOC.pospeSensorless.DQtoGaDeError   			= 0.0F;

    // back-EMF observer parameters - D-axis
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltCC1sc 		= BEMF_DQ_CC1_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltCC2sc 		= BEMF_DQ_CC2_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltUpperLimit 	= FLOAT_MAX;
    drvFOC.pospeSensorless.bEMFObs.pParamD.fltLowerLimit 	= FLOAT_MIN;
    // back-EMF observer parameters - Q-axis
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltCC1sc 		= BEMF_DQ_CC1_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltCC2sc 		= BEMF_DQ_CC2_GAIN;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltUpperLimit 	= FLOAT_MAX;
    drvFOC.pospeSensorless.bEMFObs.pParamQ.fltLowerLimit 	= FLOAT_MIN;
    // back-EMF observer parameters - Scale constants
    drvFOC.pospeSensorless.bEMFObs.fltIGain 				= I_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltUGain 				= U_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltEGain 				= E_Gain;
    drvFOC.pospeSensorless.bEMFObs.fltWIGain 				= WI_Gain;

    /* Clear back-EMF observer state variables */
    AMCLIB_BemfObsrvDQInit_FLT(&drvFOC.pospeSensorless.bEMFObs);

    /* Clear ATO observer state variables */
    AMCLIB_TrackObsrvInit_FLT(&drvFOC.pospeSensorless.TrackObsrv);

    /* Clear ATO observer state variables */
    AMCLIB_TrackObsrvInit_FLT(&encoderPospe.TrackObsrv);

    drvFOC.pospeSensorless.wRotEl			= 0.0F;
    drvFOC.pospeSensorless.thRotEl			= 0.0F;

    drvFOC.pospeOpenLoop.integ.f32InK1 		= 0.0F;
    drvFOC.pospeOpenLoop.integ.f32State 	= 0.0F;

    drvFOC.pospeOpenLoop.thRotEl			= 0.0F;
    drvFOC.pospeOpenLoop.wRotEl				= 0.0F;

    drvFOC.pospeOpenLoop.iQLowerLimit		= MLIB_Neg(drvFOC.pospeOpenLoop.iQUpperLimit);

    // Default mode of operation
    cntrState.usrControl.controlMode		= automatic;
    pos_mode								= force;

    switchSensor							= sensorless;

    if (!InitFcnStatus)
    {
    	// Fault in initialization state
        tempfaults.stateMachine.B.InitError = 1;		    // Mark the initialization fault
        cntrState.event                     = e_fault;	    // prepare for transition to fault state.
    }
    else
    {
    	// Initialization phase successfully done
    	cntrState.event                     = e_init_done;  // prepare for transition to next state.
    }
}

/***************************************************************************//*!
*
* @brief   READY state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateReady()
{
    /*-----------------------------------------------------
    Application State Machine - state identification
    ----------------------------------------------------- */

	// Turn off PWM output
	statePWM = ACTUATE_DisableOutput();

	if(cntrState.loadDefSetting)
	{
		cntrState.loadDefSetting = false;
		MCAT_Init();
	}

	cntrState.state   = ready;
    cntrState.event   = e_ready;

}

/***************************************************************************//*!
*
* @brief   CALIBRATION state - ADC calibration state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateCalib()
{
    /*-----------------------------------------------------
      Application State Machine - state identification
    ----------------------------------------------------- */
    tBool           CalibStatus;

    cntrState.state    = calib;
    cntrState.event    = e_calib;
    CalibStatus        = false;

    // Turn on actuator output
    statePWM = ACTUATE_EnableOutput();

    CalibStatus = MEAS_CalibCurrentSense(&meas);

    // Apply 0.5 duty cycle
    drvFOC.pwmflt.fltArg1 = 0.5F;
    drvFOC.pwmflt.fltArg2 = 0.5F;
    drvFOC.pwmflt.fltArg3 = 0.5F;

    statePWM = ACTUATE_SetDutycycle(&drvFOC.pwmflt, 2);

    // Exit the calibration state when DC calibration is done for all sectors
    if (CalibStatus)
    {
    	// Calibration sequence has successfully finished
		cntrState.event               = e_calib_done;
    }
}

/***************************************************************************//*!
*
* @brief   ALIGNMENT state - motor control d-axes alignment
*
* @param
*
* @return  none
*
******************************************************************************/
void StateAlign( )
{
    /*-----------------------------------------------------
    Application State Machine - state identification
    ----------------------------------------------------- */
    tBool           AlignStatus;

    cntrState.state   = align;
    cntrState.event   = e_align;

    // Align sequence is at the beginning
    AlignStatus     = true;

    drvFOC.uDQReq.fltArg1      = drvFOC.alignVoltage;
    drvFOC.uDQReq.fltArg2      = 0.0F;

	GFLIB_SinCos_FLT(0.0F, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);

    GMCLIB_ParkInv_FLT(&(drvFOC.uAlBeReq),&(drvFOC.thTransform),&(drvFOC.uDQReq));

    if (--(drvFOC.alignCntr)<=0)
    {
    	drvFOC.CurrentLoop.pIDQReq->fltArg1 	= 0.0F;
        drvFOC.CurrentLoop.pIDQReq->fltArg2 	= 0.0F;

        AMCLIB_CurrentLoopInit_FLT(&drvFOC.CurrentLoop);

        drvFOC.uDQReq.fltArg1 					= 0.0F;
        drvFOC.uDQReq.fltArg2 					= 0.0F;

        drvFOC.CurrentLoop.pPIrAWD.fltInErrK1  	= 0.0F;
        drvFOC.CurrentLoop.pPIrAWD.fltAcc      	= 0.0F;

        drvFOC.CurrentLoop.pPIrAWQ.fltInErrK1  	= 0.0F;
        drvFOC.CurrentLoop.pPIrAWQ.fltAcc      	= 0.0F;

        drvFOC.pwmflt.fltArg1 					= 0.5F;
        drvFOC.pwmflt.fltArg2 					= 0.5F;
        drvFOC.pwmflt.fltArg3 					= 0.5F;

        // Clear Encoder position and speed variables
		POSPE_ClearPospeElEnc(&encoderPospe);
		// FTM2 starts to count in Quadrature decoder mode
		FTM_DRV_QuadDecodeStart(INST_FLEXTIMER_QD2, &flexTimer_qd2_QuadDecoderConfig);
		// Set initial value of the FTM2 counter to -2048 to wrap encoder position into the range <-pi,pi>
		FTM_RMW_CNTIN(FTM2, 0x0000, 0xF800);

        if (!AlignStatus)
        {
        	tempfaults.stateMachine.B.AlignError = 1;
        }
        else
        {
        	cntrState.event           = e_align_done;
        }
    }

    drvFOC.elimDcbRip.fltArgDcBusMsr  = meas.measured.fltUdcb.raw;
    GMCLIB_ElimDcBusRip_FLT(&drvFOC.uAlBeReqDCB,&drvFOC.uAlBeReq,&drvFOC.elimDcbRip);

    drvFOC.svmSector   = GMCLIB_SvmStd_FLT(&(drvFOC.pwmflt),&drvFOC.uAlBeReqDCB);

    statePWM = ACTUATE_SetDutycycle(&drvFOC.pwmflt, drvFOC.svmSector);
}

/***************************************************************************//*!
*
* @brief   RUN state
*
* @param   none
*
* @return  none
*
******************************************************************************/
__attribute__((section (".code_ram"))) 		// inserting function to the RAM section
void StateRun( )
{
	static tBool stateRunStatus;

    stateRunStatus = false;

    /*-----------------------------------------------------
    Application State Machine - state identification
    ----------------------------------------------------- */
    cntrState.state   = run;
    cntrState.event   = e_run;

    /*-----------------------------------------------------
        Calculate positions
     ----------------------------------------------------- */
	CalcOpenLoop(&drvFOC.pospeOpenLoop,drvFOC.FwSpeedLoop.pRamp.fltState);

	// Start calculation of the Bemf Observer in tracking mode
	if(pos_mode!=force)
	// SENSORLESS CALCULATION - BACK-EMF OBSERVER
	// INPUT	- Stator currents and voltages
	// 		  	- Electrical angular velocity
	// 		  	- Estimated rotor flux angle
	// OUTPUT 	- Phase error between synchronous and quasi-synchronous reference frame
    drvFOC.pospeSensorless.DQtoGaDeError = AMCLIB_BemfObsrvDQ_FLT(&drvFOC.iAlBeFbck, &drvFOC.uAlBeReq,
															       drvFOC.pospeSensorless.wRotEl,
															       drvFOC.pospeSensorless.thRotEl,
															      &drvFOC.pospeSensorless.bEMFObs);

	// SENSORLESS CALCULATION - ANGLE TRACKING OBSERVER
	// INPUT	- Phase error between synchronous and quasi-synchronous reference frame
	// OUTPUT 	- Estimated rotor position and velocity
	AMCLIB_TrackObsrv_FLT(drvFOC.pospeSensorless.DQtoGaDeError, &drvFOC.pospeSensorless.thRotEl, &drvFOC.pospeSensorless.wRotEl, &drvFOC.pospeSensorless.TrackObsrv);

	drvFOC.pospeOpenLoop.thDifOpenLEstim = MLIB_Sub(drvFOC.pospeSensorless.thRotEl, drvFOC.pospeOpenLoop.thRotEl);

	/*-----------------------------------------------------
	Get positions according to selected mode
	----------------------------------------------------- */
	// Selecting 0 will disable the "AUTOMATIC MODE"
	// where the transition from open loop to sensorless in performed automatically
	// Selecting 1 will enable the "USER mode"
	// where user decide whether to switch to force mode, tracking mode, sensorless mode
#if ENCODER
	if(switchSensor == encoder && cntrState.usrControl.FOCcontrolMode != scalarControl)
	{
		pos_mode = encoder1;
	}

	else if(cntrState.usrControl.controlMode == automatic)
	{
		AutomaticMode();
	}
#else
	if(cntrState.usrControl.controlMode == automatic)
	{
		AutomaticMode();
	}
#endif


	// user decide whether to switch to force mode, tracking mode, sensorless mode
	switch (pos_mode)
	{
		case force:
			drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit 	= drvFOC.pospeOpenLoop.iQUpperLimit;
			drvFOC.FwSpeedLoop.pPIpAWQ.fltLowerLimit 	= MLIB_Neg(drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit);

			drvFOC.pospeControl.thRotEl 			    = drvFOC.pospeOpenLoop.thRotEl;
			drvFOC.pospeControl.wRotEl				    = 0;

			drvFOC.FwSpeedLoop.pRamp.fltRampDown		= OL_SpeedRampInc;
			drvFOC.FwSpeedLoop.pRamp.fltRampUp          = OL_SpeedRampInc;

		    drvFOC.pospeSensorless.wRotEl			    = 0;
		    drvFOC.pospeSensorless.thRotEl			    = 0;

		    /* Clear back-EMF observer state variables */
		    AMCLIB_BemfObsrvDQInit_FLT(&drvFOC.pospeSensorless.bEMFObs);

		    /* Load back-EMF observer state variables from tracking mode */
		    drvFOC.pospeSensorless.TrackObsrv.pParamPI.fltAcc 		= drvFOC.pospeOpenLoop.wRotEl;
		    drvFOC.pospeSensorless.TrackObsrv.pParamInteg.fltState 	= drvFOC.pospeOpenLoop.thRotEl;

		break;
		case tracking:
			drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit 	= drvFOC.pospeOpenLoop.iQUpperLimit;
			drvFOC.FwSpeedLoop.pPIpAWQ.fltLowerLimit 	= MLIB_Neg(drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit);

			drvFOC.pospeControl.thRotEl 			    = drvFOC.pospeOpenLoop.thRotEl;
			drvFOC.pospeControl.wRotEl				    = 0;

			drvFOC.FwSpeedLoop.pRamp.fltRampDown		= OL_SpeedRampInc;
			drvFOC.FwSpeedLoop.pRamp.fltRampUp          = OL_SpeedRampInc;

		break;
		case sensorless1:
			drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit 	= drvFOC.pospeSensorless.iQUpperLimit;
			drvFOC.FwSpeedLoop.pPIpAWQ.fltLowerLimit 	= drvFOC.pospeSensorless.iQLowerLimit;

			drvFOC.pospeControl.thRotEl 			    = drvFOC.pospeSensorless.thRotEl;
			drvFOC.pospeControl.wRotEl				    = drvFOC.pospeSensorless.wRotEl;

			drvFOC.FwSpeedLoop.pRamp.fltRampDown		= CL_SpeedRampDec;
			drvFOC.FwSpeedLoop.pRamp.fltRampUp          = CL_SpeedRampInc;

		    drvFOC.pospeOpenLoop.integ.f32State 	    = MLIB_ConvertPU_F32FLT(MLIB_Div(drvFOC.pospeSensorless.thRotEl, FLOAT_PI));
		break;
#if ENCODER
		case encoder1:
			drvFOC.FwSpeedLoop.pPIpAWQ.fltUpperLimit 	= drvFOC.pospeSensorless.iQUpperLimit;
			drvFOC.FwSpeedLoop.pPIpAWQ.fltLowerLimit 	= drvFOC.pospeSensorless.iQLowerLimit;

			// Quadrature decoder mode
			drvFOC.pospeControl.thRotEl                 = encoderPospe.thRotEl.filt;
			drvFOC.pospeControl.wRotEl	                = encoderPospe.wRotEl.raw;

			drvFOC.FwSpeedLoop.pRamp.fltRampDown		= CL_SpeedRampDec;
			drvFOC.FwSpeedLoop.pRamp.fltRampUp          = CL_SpeedRampInc;

		    drvFOC.pospeOpenLoop.integ.f32State         = MLIB_ConvertPU_F32FLT(MLIB_Div(encoderPospe.thRotEl.filt, FLOAT_PI));
		break;
#endif
		default:
			pos_mode = sensorless1;
	}

	/*-----------------------------------------------------
	    Calculate Field Oriented Control FOC
	----------------------------------------------------- */
    if (++drvFOC.pospeControl.speedLoopCntr>=SPEED_LOOP_CNTR)
    {
        drvFOC.pospeControl.speedLoopCntr    	= 0;
        stateRunStatus  = FocSlowLoop();

        if (!stateRunStatus)
        {
        	tempfaults.stateMachine.B.RunError 	= 1;
        }
    }

    stateRunStatus = FocFastLoop();

    if (!stateRunStatus)
    {
    	tempfaults.stateMachine.B.RunError 		= 1;
    }

    /* Voltage vector sum calculation to check if DC bus voltage is used appropriately */
    UDQVectorSum = GFLIB_Sqrt(MLIB_Add(MLIB_Mul(drvFOC.uDQReq.fltArg1,drvFOC.uDQReq.fltArg1),MLIB_Mul(drvFOC.uDQReq.fltArg2,drvFOC.uDQReq.fltArg2)));

    statePWM = ACTUATE_SetDutycycle(&drvFOC.pwmflt, drvFOC.svmSector);

}

/***************************************************************************//*!
*
* @brief   Field Oriented Control - slow loop calculations
*
* @param
*
* @return  none
*
******************************************************************************/
static tBool FocSlowLoop()
{
	if(cntrState.usrControl.FOCcontrolMode != speedControl)
	{
		// required speed for open loop start-up in sensorless mode = MERG_SPEED_1_TRH*1,5
        // wRotElReq = MERG_SPEED_1_TRH * 9.55 * 1.5 / pp = MERG_SPEED_1_TRH * 4.775 = ((MERG_SPEED_1_TRH*Frac16(0.596875)) << 3;
		if (cntrState.usrControl.FOCcontrolMode == voltageControl && drvFOC.uDQReq.fltArg2==0)
			drvFOC.pospeControl.wRotElReq = 0;
		else if (cntrState.usrControl.FOCcontrolMode == currentControl && drvFOC.CurrentLoop.pIDQReq->fltArg2==0)
			drvFOC.pospeControl.wRotElReq = 0;
		else if (cntrState.usrControl.FOCcontrolMode != scalarControl)
			drvFOC.pospeControl.wRotElReq = MLIB_Mul(0.75,MERG_SPEED_1_TRH);
	}

    // Required speed limit due to reduced DC bus voltage
	if(drvFOC.pospeControl.wRotElReq > SPEED_LIM_RAD)	drvFOC.pospeControl.wRotElReq = SPEED_LIM_RAD;
	if(drvFOC.pospeControl.wRotElReq < -SPEED_LIM_RAD)	drvFOC.pospeControl.wRotElReq = -SPEED_LIM_RAD;

	if(fieldWeakOnOff)
	{
		drvFOC.FwSpeedLoop.pPIpAWFW.fltPropGain			= FW_PropGainControl;
		drvFOC.FwSpeedLoop.pPIpAWFW.fltIntegGain		= FW_IntegGainControl;

	}else
	{
		drvFOC.FwSpeedLoop.pPIpAWFW.fltPropGain			= 0.0F;
		drvFOC.FwSpeedLoop.pPIpAWFW.fltIntegGain		= 0.0F;
		drvFOC.FwSpeedLoop.pPIpAWFW.fltIntegPartK_1 	= 0.0F;
	}

   	AMCLIB_FWSpeedLoop_FLT(drvFOC.pospeControl.wRotElReq, drvFOC.pospeControl.wRotEl, &drvFOC.iDQReqOutLoop, &drvFOC.FwSpeedLoop);

    // Speed FO control mode
    if(cntrState.usrControl.FOCcontrolMode == speedControl)
    {
    	// In Speed control mode, FOC Outer Loop (Speed Loop & Field Weakening) output is interconnected with FOC Inner Loop (Current Loop) input
    	drvFOC.iDQReqInLoop.fltArg1 = drvFOC.iDQReqOutLoop.fltArg1;
    	drvFOC.iDQReqInLoop.fltArg2 = drvFOC.iDQReqOutLoop.fltArg2;
    }
    return true;
}

/***************************************************************************//*!
*
* @brief   Field Oriented Control - fast loop calculations
*
* @param
*
* @return  none
*
******************************************************************************/
static tBool FocFastLoop()
{
	GMCLIB_Clark_FLT(&drvFOC.iAlBeFbck,&drvFOC.iAbcFbck);

	// Scalar control mode
	if(cntrState.usrControl.FOCcontrolMode == scalarControl)
	{
		// generated electrical position for scalar control purpose

		// Required voltage = VHzRatio * Required Frequency
		drvFOC.scalarControl.UmReq	  	= MLIB_Mul(drvFOC.scalarControl.VHzRatioReq, drvFOC.pospeControl.wRotElReq);

		// thRotEl is calculated in CalcOpenLoop executed in focSlowLoop
		GFLIB_SinCos_FLT(drvFOC.pospeControl.thRotEl, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);

		drvFOC.uDQReq.fltArg1           = 0.0F;
		drvFOC.uDQReq.fltArg2           = drvFOC.scalarControl.UmReq;

		// enable Bemf observer
		cntrState.usrControl.controlMode = manual;
		pos_mode = tracking;
	}

	// DQ Voltage FO control mode
	if(cntrState.usrControl.FOCcontrolMode == voltageControl)
	{
		if(drvFOC.uDQReq.fltArg2!=0)
		{
			GFLIB_SinCos_FLT(drvFOC.pospeControl.thRotEl, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);
		}
		else
		{
			GFLIB_SinCos_FLT(0.0F, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);
		}

		GMCLIB_Park_FLT(&drvFOC.iDQFbck,&drvFOC.thTransform,&drvFOC.iAlBeFbck);
	}

	// DQ Current and Speed FO control mode
	if(cntrState.usrControl.FOCcontrolMode == currentControl || cntrState.usrControl.FOCcontrolMode == speedControl)
	{
		if(cntrState.usrControl.FOCcontrolMode == speedControl)
		{
			GFLIB_SinCos_FLT(drvFOC.pospeControl.thRotEl, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);
		}
		else
		{
			if(drvFOC.iDQReqOutLoop.fltArg2!=0)
			{
				GFLIB_SinCos_FLT(drvFOC.pospeControl.thRotEl, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);
			}
			else
			{
				GFLIB_SinCos_FLT(0.0F, &drvFOC.thTransform, GFLIB_SINCOS_DEFAULT_FLT);
			}
		}

		GMCLIB_Park_FLT(&drvFOC.iDQFbck,&drvFOC.thTransform,&drvFOC.iAlBeFbck);

		// 85% of available DCbus recalculated to phase voltage = 0.90*uDCB/sqrt(3)
		AMCLIB_CurrentLoop_FLT(drvFOC.fltUdcb, &drvFOC.uDQReq, &drvFOC.CurrentLoop);

	}

    GMCLIB_ParkInv_FLT(&drvFOC.uAlBeReq,&drvFOC.thTransform,&drvFOC.uDQReq);

    drvFOC.elimDcbRip.fltArgDcBusMsr  = meas.measured.fltUdcb.raw;
    GMCLIB_ElimDcBusRip_FLT(&drvFOC.uAlBeReqDCB,&drvFOC.uAlBeReq,&drvFOC.elimDcbRip);

    drvFOC.AlBeReqDCBLim.fltLimit = 0.9;
    GFLIB_VectorLimit_FLT (&drvFOC.uAlBeReqDCBLim,&drvFOC.uAlBeReqDCB,&drvFOC.AlBeReqDCBLim);

    drvFOC.svmSector = GMCLIB_SvmStd_FLT(&(drvFOC.pwmflt),&drvFOC.uAlBeReqDCBLim);

    return (true);
}


/***************************************************************************//*!
*
* @brief   Fault Detection function
*
* @param   none
*
* @return  none
*
******************************************************************************/
static tBool FaultDetection()
{
    tBool faultDetectiontEvent;

    faultDetectiontEvent = false;

    //-----------------------------
    // Actual Faults
    //-----------------------------
    // TRIP:   Phase A over-current detected
	tempfaults.motor.B.OverPhaseACurrent = (drvFOC.iAbcFbck.fltArg1 > MLIB_Mul(I_PH_OVER, 0.9F)) ? true : false;

	// TRIP:   Phase B over-current detected
	tempfaults.motor.B.OverPhaseBCurrent = (drvFOC.iAbcFbck.fltArg2 > MLIB_Mul(I_PH_OVER, 0.9F)) ? true : false;

	// TRIP:   Phase C over-current detected
	tempfaults.motor.B.OverPhaseCCurrent = (drvFOC.iAbcFbck.fltArg3 > MLIB_Mul(I_PH_OVER, 0.9F)) ? true : false;

	// TRIP:   DC-bus over-voltage
	tempfaults.motor.B.OverDCBusVoltage  = (meas.measured.fltUdcb.raw > U_DCB_TRIP) ? true : false;

	// TRIP:   DC-bus under-voltage
	tempfaults.motor.B.UnderDCBusVoltage = (meas.measured.fltUdcb.raw < MLIB_Div(U_DCB_UNDER,0.91F)) ? true : false;

	// TRIP:   DC-bus over-current
	tempfaults.motor.B.OverDCBusCurrent  = (meas.measured.fltIdcb.filt > MLIB_Mul(I_PH_OVER, 0.9F)) ? true : false;

	// Activate braking resistor, if there is DC-bus over-voltage
	/*
	if(tempfaults.motor.B.OverDCBusVoltage)
	{
		// Activate braking resistor
		GPIO_HAL_SetPins(PTD, 1<<14);
	}
	else
	{
		// Deactivate braking resistor
		GPIO_HAL_ClearPins(PTD, 1<<14);
	}
    */

	//-----------------------------
	// Pending Faults
	//-----------------------------

	if (cntrState.state != fault)
	{
		// Fault:   Phase A over-current detected
		permFaults.motor.B.OverPhaseACurrent    = (drvFOC.iAbcFbck.fltArg1 > I_PH_OVER) ? true : permFaults.motor.B.OverPhaseACurrent;

		// Fault:   Phase B over-current detected
		permFaults.motor.B.OverPhaseBCurrent    = (drvFOC.iAbcFbck.fltArg2 > I_PH_OVER) ? true : permFaults.motor.B.OverPhaseBCurrent;

		// Fault:   Phase C over-current detected
		permFaults.motor.B.OverPhaseCCurrent    = (drvFOC.iAbcFbck.fltArg3 > I_PH_OVER) ? true : permFaults.motor.B.OverPhaseCCurrent;

		// Fault:   DC-bus over-voltage
		permFaults.motor.B.OverDCBusVoltage     = (meas.measured.fltUdcb.raw > U_DCB_OVER) ? true : permFaults.motor.B.OverDCBusVoltage;

		// Fault:   DC-bus under-voltage
		permFaults.motor.B.UnderDCBusVoltage    = (meas.measured.fltUdcb.raw < U_DCB_UNDER) ? true : permFaults.motor.B.UnderDCBusVoltage;

		// Fault:   DC-bus over-current
		permFaults.motor.B.OverDCBusCurrent   	= (meas.measured.fltIdcb.filt > I_PH_OVER) ? true : permFaults.motor.B.OverDCBusCurrent;
	}

	// Check, whether back-EMF observer estimates rotor position properly
	if((pos_mode == sensorless1) && (cntrState.usrControl.FOCcontrolMode == speedControl) && (cntrState.state != fault)
		&& (MLIB_Abs(drvFOC.pospeOpenLoop.thDifOpenLEstim)>MAX_TH_DIF_OPEN_ESTIM))
	{
		drvFOC.pospeSensorless.sensorlessCnt++;
		if(drvFOC.pospeSensorless.sensorlessCnt > 10000)
		{
			drvFOC.pospeSensorless.sensorlessCnt 	= 0;
			permFaults.stateMachine.B.FOCError 		= 1;
		}
	}
	else
	{
		drvFOC.pospeSensorless.sensorlessCnt 		= 0;
	}

	// Check the status of the GD3000 MOSFET pre-driver
    if (tppDrvConfig.deviceConfig.statusRegister[0U])
    {
    	permFaults.gd3000 = true;
    	faultDetectiontEvent = true;
    }

    if ((permFaults.motor.R != 0x0))
    	faultDetectiontEvent = true;
    if ((permFaults.mcu.R != 0x0))
        faultDetectiontEvent = true;
    if ((permFaults.stateMachine.R != 0x0))
        faultDetectiontEvent = true;

    return faultDetectiontEvent;
}

/***************************************************************************//*!
*
* @brief   Open loop generator of open loop position and speed
*
* @param   pointer to structure of openLoopPospe_t type
* @param   speed ramp
*
* @return  none
*
******************************************************************************/
static tBool CalcOpenLoop(openLoopPospe_t *openLoop, tFloat speedReqRamp)
{
	openLoop->wRotEl  = speedReqRamp;
	openLoop->thRotEl = MLIB_Mul(MLIB_ConvertPU_FLTF32(GFLIB_IntegratorTR_F32(MLIB_ConvertPU_F32FLT(MLIB_Div(speedReqRamp, WEL_MAX)),
								 &(openLoop->integ))), FLOAT_PI);

	return(true);
}

/***************************************************************************//*!
*
* @brief   Calculate Position and Speed In Sensorless Mode
*
* @param   none
*
* @return  bool
*
******************************************************************************/
tBool AutomaticMode()
{
	static tBool trackingToSensorless 	= false;

	if ((MLIB_Abs(drvFOC.pospeOpenLoop.wRotEl) > drvFOC.pospeSensorless.wRotElMatch_2))
	{
		// Just once the sensorless is entered, the speed PI controller needs to be reset to avoid step current changes
		if(trackingToSensorless)
		{
			AMCLIB_CurrentLoopSetState(drvFOC.uDQReq.fltArg1, drvFOC.uDQReq.fltArg2, &drvFOC.CurrentLoop);
			AMCLIB_FWSpeedLoopSetState(drvFOC.pospeOpenLoop.wRotEl, 0.0F,drvFOC.iDQReqInLoop.fltArg2, 0.0F, drvFOC.pospeOpenLoop.wRotEl, &drvFOC.FwSpeedLoop);
			trackingToSensorless		= false;
		}

		pos_mode = sensorless1;
	}
	else if ((MLIB_Abs(drvFOC.pospeOpenLoop.wRotEl) >= drvFOC.pospeSensorless.wRotElMatch_1 &&
			 MLIB_Abs(drvFOC.pospeOpenLoop.wRotEl) < drvFOC.pospeSensorless.wRotElMatch_2))
	{
		pos_mode = tracking;
		trackingToSensorless			= true;
	}
	else
	{
		pos_mode 						= force;
	}

	return(true);
}

/***************************************************************************//*!
*
* @brief   Board buttons to control the motor speed and ON/OFF/FAULT state
*
* @param   none
*
* @return  none
*
******************************************************************************/
void BoardButtons()
{
	/* Turn the application on and increase the rotor velocity */
    if (cntrState.usrControl.btSpeedUp && (!cntrState.usrControl.btSpeedDown))
    {
    	if(cntrState.usrControl.cntSpeedUp>SPEED_UP_CNT)
    	{
    		cntrState.usrControl.cntSpeedUp = 0;
    		cntrState.usrControl.switchAppOnOff = true;
    		drvFOC.pospeControl.wRotElReq = MLIB_Add(drvFOC.pospeControl.wRotElReq, SPEED_RAD_INC);
    	}
    	else
    	{
    		cntrState.usrControl.cntSpeedUp++;
    		cntrState.usrControl.cntSpeedDown=0;
    		cntrState.usrControl.cntAppOff=0;
    	}
    }

    /* Turn the application ON and decrease the rotor velocity */
    if ((!cntrState.usrControl.btSpeedUp) && cntrState.usrControl.btSpeedDown)
    {
    	if(cntrState.usrControl.cntSpeedDown>SPEED_DOWN_CNT)
    	{
    		cntrState.usrControl.cntSpeedDown=0;
    		cntrState.usrControl.switchAppOnOff = true;
    		drvFOC.pospeControl.wRotElReq = MLIB_Sub(drvFOC.pospeControl.wRotElReq, SPEED_RAD_DEC);
    	}
    	else
       	{
    		cntrState.usrControl.cntSpeedDown++;
    		cntrState.usrControl.cntSpeedUp=0;
    		cntrState.usrControl.cntAppOff=0;
      	}
    }

    /* Turn the application off and clear application faults */
    if(cntrState.usrControl.btSpeedUp && cntrState.usrControl.btSpeedDown)
    {
    	/* Turn the application off */
    	if(cntrState.usrControl.cntAppOff>APP_OFF_CNT && cntrState.state != fault)
    	{
    		cntrState.usrControl.cntAppOff=0;
    		cntrState.usrControl.switchAppOnOff = false;
    		drvFOC.pospeControl.wRotElReq=0;
    	}

    	/* Clear application faults */
    	else if(cntrState.usrControl.cntAppOff>APP_OFF_CNT && cntrState.state == fault)
    	{
    		cntrState.usrControl.cntAppOff=0;
    		cntrState.usrControl.switchFaultClear = true;
    		drvFOC.pospeControl.wRotElReq=0;
    	}
    	else
    	{
    		cntrState.usrControl.cntAppOff++;
    		cntrState.usrControl.cntSpeedUp=0;
    		cntrState.usrControl.cntSpeedDown=0;
    	}
    }
}

/***************************************************************************//*!
*
* @brief   RGB LED OFF state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateRGBLedOFF()
{
	PINS_DRV_SetPins(PTD, 1<<0);		// RGB Blue  Led OFF
	PINS_DRV_SetPins(PTD, 1<<15);		// RGB Red 	 Led OFF
	PINS_DRV_SetPins(PTD, 1<<16);		// RGB Green Led OFF
}

/***************************************************************************//*!
*
* @brief   Blue RGB LED ON state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateRGBLedBlueON()
{
	PINS_DRV_ClearPins(PTD, 1<<0);		// RGB Blue  Led ON
	PINS_DRV_SetPins(PTD, 1<<15);		// RGB Red 	 Led OFF
	PINS_DRV_SetPins(PTD, 1<<16);		// RGB Green Led OFF
}

/***************************************************************************//*!
*
* @brief   Red RGB LED ON state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateRGBLedRedON()
{
	PINS_DRV_SetPins(PTD, 1<<0);		// RGB Blue  Led OFF
	PINS_DRV_ClearPins(PTD, 1<<15);		// RGB Red 	 Led ON
	PINS_DRV_SetPins(PTD, 1<<16);		// RGB Green Led OFF
}

/***************************************************************************//*!
*
* @brief   Green RGB LED ON state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateRGBLedGreenON()
{
	PINS_DRV_ClearPins(PTD, 1<<16);		// RGB Green Led ON
	PINS_DRV_SetPins(PTD, 1<<0);		// RGB Blue  Led OFF
	PINS_DRV_SetPins(PTD, 1<<15);		// RGB Red 	 Led OFF
}

/***************************************************************************//*!
*
* @brief   Green RGB LED FLASHING state
*
* @param
*
* @return  none
*
******************************************************************************/
void StateRGBLedGreenFlashing()
{
	cntrState.usrControl.ledCounter += 1;

	/* RGB Green Led FLASHING */
	if((cntrState.usrControl.ledCounter)>((cntrState.usrControl.ledFlashing)<<1))
	{
		PINS_DRV_TogglePins(PTD, 1<<16);
		cntrState.usrControl.ledCounter = 0;
	}
	PINS_DRV_SetPins(PTD, 1<<0);		// RGB Blue  Led OFF
	PINS_DRV_SetPins(PTD, 1<<15);		// RGB Red 	 Led OFF
}

/* End of file */
