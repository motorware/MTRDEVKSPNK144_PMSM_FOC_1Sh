/*******************************************************************************
*
* Copyright 2006-2015 Freescale Semiconductor, Inc.
* Copyright 2016-2017 NXP
*
****************************************************************************//*!
*
* @file     peripherals_config.h
*
* @date     March-28-2017
*
* @brief    MCU Peripherals Configuration
*
*******************************************************************************/
#ifndef PERIPHERALS_PERIPHERALS_INIT_H_
#define PERIPHERALS_PERIPHERALS_INIT_H_

/*******************************************************************************
* Includes
*******************************************************************************/
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


/*******************************************************************************
* Constants and macros
*******************************************************************************/
extern ftm_state_t statePwm;

/*******************************************************************************
* Global function prototypes
*******************************************************************************/
void McuClockConfig(void);
void McuPowerConfig(void);
void McuIntConfig(void);
void McuSimConfig(void);
void McuTrigmuxConfig(void);
void McuPinsConfig(void);
void McuLpuartConfig(void);
void McuAdcConfig(void);
void McuPdbConfig(void);
void McuFtmConfig(void);
void McuCacheConfig(void);
//void McuMpuInit(void);
//void McuLpitConfig(void);

#endif /* PERIPHERALS_PERIPHERALS_INIT_H_ */
