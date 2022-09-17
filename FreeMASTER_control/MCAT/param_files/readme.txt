M1_param.txt is a default MCAT parameters file tuned for LINIX 45ZWN24-40 motor of the MTRDEVKSPNK144 kit.
M1_param_Buhler.txt and M1_param_TGDrives.txt are alernative MCAT parameters files tuned for Buhler motor (1.25.037.403.00) 
and TG Drives motor (TGN2-0054-30-36), respectively.
MCAT automatically loads M1_param.txt file. To load Buhler or TG Drives motor MCAT parameters file, change file name from M1_param_Buhler.txt or M1_param_TGDrives.txt 
to M1_param.txt.
Generate configuration file in MCAT (PMSM_appconfig.h), precompile the the project and flash the target.

Motor wires order for the Linix motor (45ZWN24-40):
DEVKIT-MOTORGD Phase A terminal: White   
DEVKIT-MOTORGD Phase B terminal: Blue
DEVKIT-MOTORGD Phase C terminal: Green

Motor wires order for the Buhler motor (1.25.037.403.00):
DEVKIT-MOTORGD Phase A terminal: Black   
DEVKIT-MOTORGD Phase B terminal: White
DEVKIT-MOTORGD Phase C terminal: Red

Motor wires order for the TG Drives motor (TGN2-0054-30-36):
DEVKIT-MOTORGD Phase A terminal: 1  
DEVKIT-MOTORGD Phase B terminal: 2
DEVKIT-MOTORGD Phase C terminal: 3