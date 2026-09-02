/*
 * File: GearShift_Stateflow.h
 *
 * Code generated for Simulink model 'GearShift_Stateflow'.
 *
 * Model version                  : 1.18
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Wed Sep  2 22:16:07 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: NXP->Cortex-M4
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#ifndef GearShift_Stateflow_h_
#define GearShift_Stateflow_h_
#ifndef GearShift_Stateflow_COMMON_INCLUDES_
#define GearShift_Stateflow_COMMON_INCLUDES_
#include "rtwtypes.h"
#include "math.h"
#endif                                /* GearShift_Stateflow_COMMON_INCLUDES_ */

#include "GearShift_Stateflow_types.h"

/* Macros for accessing real-time model data structure */
#ifndef rtmGetErrorStatus
#define rtmGetErrorStatus(rtm)         ((rtm)->errorStatus)
#endif

#ifndef rtmSetErrorStatus
#define rtmSetErrorStatus(rtm, val)    ((rtm)->errorStatus = (val))
#endif

/* Block states (default storage) for system '<Root>' */
typedef struct {
  uint16_T comm_timeout_timer;         /* '<Root>/GearShift_Logic' */
  uint8_T is_active_c3_GearShift_Stateflo;/* '<Root>/GearShift_Logic' */
  uint8_T is_c3_GearShift_Stateflow;   /* '<Root>/GearShift_Logic' */
  uint8_T is_M;                        /* '<Root>/GearShift_Logic' */
  uint8_T temporalCounter_i1;          /* '<Root>/GearShift_Logic' */
} DW_GearShift_Stateflow_T;

/* External inputs (root inport signals with default storage) */
typedef struct {
  uint8_T stick_x;                     /* '<Root>/stick_x' */
  uint8_T stick_y;                     /* '<Root>/stick_y' */
  uint16_T brake_pedal;                /* '<Root>/brake_pedal' */
  uint16_T vehicle_speed;              /* '<Root>/vehicle_speed' */
  uint16_T engine_rpm;                 /* '<Root>/engine_rpm' */
  boolean_T p_button;                  /* '<Root>/p_button' */
  boolean_T unlock_button;             /* '<Root>/unlock_button' */
  boolean_T valid;                     /* '<Root>/valid' */
} ExtU_GearShift_Stateflow_T;

/* External outputs (root outports fed by signals with default storage) */
typedef struct {
  uint8_T current_gear;                /* '<Root>/current_gear' */
  uint8_T gear_display;                /* '<Root>/gear_display' */
} ExtY_GearShift_Stateflow_T;

/* Real-time Model Data Structure */
struct tag_RTM_GearShift_Stateflow_T {
  const char_T * volatile errorStatus;
};

/* Block states (default storage) */
extern DW_GearShift_Stateflow_T GearShift_Stateflow_DW;

/* External inputs (root inport signals with default storage) */
extern ExtU_GearShift_Stateflow_T GearShift_Stateflow_U;

/* External outputs (root outports fed by signals with default storage) */
extern ExtY_GearShift_Stateflow_T GearShift_Stateflow_Y;

/* Model entry point functions */
extern void GearShift_Stateflow_initialize(void);
extern void GearShift_Stateflow_step(void);
extern void GearShift_Stateflow_terminate(void);

/* Real-time Model object */
extern RT_MODEL_GearShift_Stateflow_T *const GearShift_Stateflow_M;

/*-
 * These blocks were eliminated from the model due to optimizations:
 *
 * Block '<Root>/Constant' : Unused code path elimination
 * Block '<Root>/Constant1' : Unused code path elimination
 * Block '<Root>/Constant10' : Unused code path elimination
 * Block '<Root>/Constant11' : Unused code path elimination
 * Block '<Root>/Constant12' : Unused code path elimination
 * Block '<Root>/Constant13' : Unused code path elimination
 * Block '<Root>/Constant14' : Unused code path elimination
 * Block '<Root>/Constant15' : Unused code path elimination
 * Block '<Root>/Constant16' : Unused code path elimination
 * Block '<Root>/Constant2' : Unused code path elimination
 * Block '<Root>/Constant3' : Unused code path elimination
 * Block '<Root>/Constant4' : Unused code path elimination
 * Block '<Root>/Constant5' : Unused code path elimination
 * Block '<Root>/Constant6' : Unused code path elimination
 * Block '<Root>/Constant7' : Unused code path elimination
 * Block '<Root>/Constant8' : Unused code path elimination
 * Block '<Root>/Constant9' : Unused code path elimination
 * Block '<Root>/Display' : Unused code path elimination
 * Block '<Root>/Display1' : Unused code path elimination
 * Block '<Root>/Manual Switch' : Unused code path elimination
 * Block '<Root>/Manual Switch1' : Unused code path elimination
 * Block '<Root>/Manual Switch2' : Unused code path elimination
 * Block '<Root>/Multiport Switch' : Unused code path elimination
 * Block '<Root>/Multiport Switch1' : Unused code path elimination
 * Block '<Root>/Unit Delay' : Unused code path elimination
 * Block '<Root>/Unit Delay1' : Unused code path elimination
 */

/*-
 * The generated code includes comments that allow you to trace directly
 * back to the appropriate location in the model.  The basic format
 * is <system>/block_name, where system is the system number (uniquely
 * assigned by Simulink) and block_name is the name of the block.
 *
 * Use the MATLAB hilite_system command to trace the generated code back
 * to the model.  For example,
 *
 * hilite_system('<S3>')    - opens system 3
 * hilite_system('<S3>/Kp') - opens and selects block Kp which resides in S3
 *
 * Here is the system hierarchy for this model
 *
 * '<Root>' : 'GearShift_Stateflow'
 * '<S1>'   : 'GearShift_Stateflow/GearShift_Logic'
 */
#endif                                 /* GearShift_Stateflow_h_ */

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
