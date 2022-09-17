/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     State_machine.c
*
* @date     March-28-2017
*
* @brief    Header file for StateMachineFrame "c" project
*
*******************************************************************************/
/******************************************************************************
* Includes
******************************************************************************/
#include "state_machine.h"

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


PFCN_VOID_STATES StateTable[12][6]={
    /* Actual State ->         'Init'           'Fault'         'Ready'         'Calib'         'Align'         'Run'*/
    /* e_fault          */ { StateFault,     StateFault,     StateFault,     StateFault,     StateFault,     StateFault},
    /* e_fault_clear    */ { StateFault,     StateInit,      StateFault,     StateFault,     StateFault,     StateFault},
    /* e_init          	*/ { StateInit,      StateFault,     StateFault,     StateFault,     StateFault,     StateFault},
    /* e_init_done      */ { StateReady,     StateFault,     StateFault,     StateFault,     StateFault,     StateFault},
    /* e_ready          */ { StateFault,     StateFault,     StateReady,     StateFault,     StateFault,     StateFault},
    /* e_app_on         */ { StateFault,     StateFault,     StateCalib,     StateFault,     StateFault,     StateFault},
    /* e_calib          */ { StateFault,     StateFault,     StateFault,     StateCalib,     StateFault,     StateFault},
    /* e_calib_done     */ { StateFault,     StateFault,     StateFault,     StateAlign,     StateFault,     StateFault},
    /* e_align          */ { StateFault,     StateFault,     StateFault,     StateFault,     StateAlign,     StateFault},
    /* e_align_done     */ { StateFault,     StateFault,     StateFault,     StateFault,     StateRun,       StateFault},
    /* e_run            */ { StateFault,     StateFault,     StateFault,     StateFault,     StateFault,     StateRun},
    /* e_app_off        */ { StateFault,     StateFault,     StateReady,     StateInit,      StateInit,      StateInit}
};



/* Actual State ->         				'Init'			'Fault'			   'Ready'         			'Calib'					'Align'	         		'Run'*/
PFCN_VOID_LED StateLED[6] = {StateRGBLedOFF, StateRGBLedRedON, StateRGBLedGreenON, StateRGBLedGreenFlashing, StateRGBLedGreenFlashing, StateRGBLedBlueON};
