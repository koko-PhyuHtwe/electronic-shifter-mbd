/*
 * File: GearShift_Stateflow.c
 *
 * Code generated for Simulink model 'GearShift_Stateflow'.
 *
 * Model version                  : 1.16
 * Simulink Coder version         : 25.2 (R2025b) 28-Jul-2025
 * C/C++ source code generated on : Sat Aug 29 15:58:34 2026
 *
 * Target selection: ert.tlc
 * Embedded hardware selection: NXP->Cortex-M4
 * Code generation objectives: Unspecified
 * Validation result: Not run
 */

#include "GearShift_Stateflow.h"
#include "rtwtypes.h"

/* Named constants for Chart: '<Root>/GearShift_Logic' */
#define GearShift_Stateflow_IN_D       ((uint8_T)1U)
#define GearShift_Stateflow_IN_FAULT   ((uint8_T)2U)
#define GearShift_Stateflow_IN_M       ((uint8_T)3U)
#define GearShift_Stateflow_IN_N       ((uint8_T)4U)
#define GearShift_Stateflow_IN_P       ((uint8_T)5U)
#define GearShift_Stateflow_IN_R       ((uint8_T)6U)

/* Block states (default storage) */
DW_GearShift_Stateflow_T GearShift_Stateflow_DW;

/* External inputs (root inport signals with default storage) */
ExtU_GearShift_Stateflow_T GearShift_Stateflow_U;

/* External outputs (root outports fed by signals with default storage) */
ExtY_GearShift_Stateflow_T GearShift_Stateflow_Y;

/* Real-time model */
static RT_MODEL_GearShift_Stateflow_T GearShift_Stateflow_M_;
RT_MODEL_GearShift_Stateflow_T *const GearShift_Stateflow_M =
  &GearShift_Stateflow_M_;

/* Forward declaration for local functions */
static void GearShift_Stateflow_M_h(void);

/* Function for Chart: '<Root>/GearShift_Logic' */
static void GearShift_Stateflow_M_h(void)
{
  if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
      GearShift_Stateflow_IN_FAULT;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
  } else if (GearShift_Stateflow_ConstB.MultiportSwitch1 == 1) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_D;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 3U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 3U;
  } else {
    GearShift_Stateflow_DW.comm_timeout_timer = 0U;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_M;
  }
}

/* Model step function */
void GearShift_Stateflow_step(void)
{
  /* Chart: '<Root>/GearShift_Logic' */
  if (GearShift_Stateflow_DW.temporalCounter_i1 < 7) {
    GearShift_Stateflow_DW.temporalCounter_i1++;
  }

  if (GearShift_Stateflow_DW.is_active_c3_GearShift_Stateflo == 0) {
    GearShift_Stateflow_DW.is_active_c3_GearShift_Stateflo = 1U;
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_P;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 0U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 0U;
  } else {
    switch (GearShift_Stateflow_DW.is_c3_GearShift_Stateflow) {
     case GearShift_Stateflow_IN_D:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 3U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 3U;
      if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_FAULT;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 2U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 2U;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch1 == 2) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_M;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;
      }
      break;

     case GearShift_Stateflow_IN_FAULT:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      GearShift_Stateflow_DW.comm_timeout_timer = 0U;
      GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
        GearShift_Stateflow_IN_FAULT;
      break;

     case GearShift_Stateflow_IN_M:
      GearShift_Stateflow_M_h();
      break;

     case GearShift_Stateflow_IN_N:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 2U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 2U;
      if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_FAULT;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 2) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 5)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 3U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 3U;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 5)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 1U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 1U;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;
      }
      break;

     case GearShift_Stateflow_IN_P:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 0U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 0U;
      if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_FAULT;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 2) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 3U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 3U;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 1U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 1U;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;
      }
      break;

     default:
      /* Outport: '<Root>/current_gear' */
      /* case IN_R: */
      GearShift_Stateflow_Y.current_gear = 1U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 1U;
      if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_FAULT;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      } else if ((GearShift_Stateflow_ConstB.MultiportSwitch == 2) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 2U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 2U;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;
      }
      break;
    }
  }

  /* End of Chart: '<Root>/GearShift_Logic' */
}

/* Model initialize function */
void GearShift_Stateflow_initialize(void)
{
  /* SystemInitialize for Chart: '<Root>/GearShift_Logic' */
  GearShift_Stateflow_DW.comm_timeout_timer = 1U;
}

/* Model terminate function */
void GearShift_Stateflow_terminate(void)
{
  /* (no terminate code required) */
}

/*
 * File trailer for generated code.
 *
 * [EOF]
 */
