/*
 * File: GearShift_Stateflow.c
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

#include "GearShift_Stateflow.h"
#include "rtwtypes.h"

/* Named constants for Chart: '<Root>/GearShift_Logic' */
#define GearShift_St_IN_NO_ACTIVE_CHILD ((uint8_T)0U)
#define GearShift_Stateflow_IN_D       ((uint8_T)1U)
#define GearShift_Stateflow_IN_FAULT   ((uint8_T)2U)
#define GearShift_Stateflow_IN_Init    ((uint8_T)1U)
#define GearShift_Stateflow_IN_M       ((uint8_T)3U)
#define GearShift_Stateflow_IN_M1      ((uint8_T)2U)
#define GearShift_Stateflow_IN_M2      ((uint8_T)3U)
#define GearShift_Stateflow_IN_M3      ((uint8_T)4U)
#define GearShift_Stateflow_IN_M4      ((uint8_T)5U)
#define GearShift_Stateflow_IN_M5      ((uint8_T)6U)
#define GearShift_Stateflow_IN_M6      ((uint8_T)7U)
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
static void GearShift_Stateflow_Init(void);
static void GearShift_Stateflow_M_h(void);

/* Function for Chart: '<Root>/GearShift_Logic' */
static void GearShift_Stateflow_Init(void)
{
  /* Inport: '<Root>/vehicle_speed' */
  if (GearShift_Stateflow_U.vehicle_speed < 10) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M1;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 11U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 11U;
  } else if ((GearShift_Stateflow_U.vehicle_speed >= 10) &&
             (GearShift_Stateflow_U.vehicle_speed < 30)) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M2;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 12U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 12U;
  } else if ((GearShift_Stateflow_U.vehicle_speed >= 30) &&
             (GearShift_Stateflow_U.vehicle_speed < 50)) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M3;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 13U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 13U;
  } else if ((GearShift_Stateflow_U.vehicle_speed >= 50) &&
             (GearShift_Stateflow_U.vehicle_speed < 70)) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M4;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 14U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 14U;
  } else if ((GearShift_Stateflow_U.vehicle_speed >= 70) &&
             (GearShift_Stateflow_U.vehicle_speed < 90)) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M5;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 15U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 15U;
  } else if (GearShift_Stateflow_U.vehicle_speed >= 90) {
    GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M6;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 16U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 16U;
  }

  /* End of Inport: '<Root>/vehicle_speed' */
}

/* Function for Chart: '<Root>/GearShift_Logic' */
static void GearShift_Stateflow_M_h(void)
{
  if (GearShift_Stateflow_DW.comm_timeout_timer > 500) {
    GearShift_Stateflow_DW.is_M = GearShift_St_IN_NO_ACTIVE_CHILD;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
      GearShift_Stateflow_IN_FAULT;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
  } else if (GearShift_Stateflow_U.p_button &&
             (GearShift_Stateflow_U.vehicle_speed < 5)) {
    GearShift_Stateflow_DW.is_M = GearShift_St_IN_NO_ACTIVE_CHILD;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_P;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 0U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 0U;
  } else if (GearShift_Stateflow_U.stick_y == 1) {
    GearShift_Stateflow_DW.is_M = GearShift_St_IN_NO_ACTIVE_CHILD;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_D;

    /* Outport: '<Root>/current_gear' */
    GearShift_Stateflow_Y.current_gear = 3U;

    /* Outport: '<Root>/gear_display' */
    GearShift_Stateflow_Y.gear_display = 3U;
  } else if (GearShift_Stateflow_U.valid) {
    GearShift_Stateflow_DW.comm_timeout_timer = 0U;
    GearShift_Stateflow_DW.is_c3_GearShift_Stateflow = GearShift_Stateflow_IN_M;
    GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_Init;
  } else {
    switch (GearShift_Stateflow_DW.is_M) {
     case GearShift_Stateflow_IN_Init:
      GearShift_Stateflow_Init();
      break;

     case GearShift_Stateflow_IN_M1:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 11U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 11U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/engine_rpm'
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 2) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.engine_rpm >= 2000) &&
          (GearShift_Stateflow_U.vehicle_speed >= 10)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M2;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 12U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 12U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M1;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     case GearShift_Stateflow_IN_M2:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 12U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 12U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/engine_rpm'
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 2) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.engine_rpm >= 2000) &&
          (GearShift_Stateflow_U.vehicle_speed >= 25)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M3;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 13U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 13U;
      } else if ((GearShift_Stateflow_U.stick_y == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
                 (GearShift_Stateflow_U.vehicle_speed <= 120)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M1;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 11U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 11U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M2;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     case GearShift_Stateflow_IN_M3:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 13U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 13U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/engine_rpm'
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 2) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.engine_rpm >= 2000) &&
          (GearShift_Stateflow_U.vehicle_speed >= 40)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M4;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 14U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 14U;
      } else if ((GearShift_Stateflow_U.stick_y == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
                 (GearShift_Stateflow_U.vehicle_speed <= 160)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M2;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 12U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 12U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M3;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     case GearShift_Stateflow_IN_M4:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 14U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 14U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/engine_rpm'
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 2) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.engine_rpm >= 2000) &&
          (GearShift_Stateflow_U.vehicle_speed >= 60)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M5;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 15U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 15U;
      } else if ((GearShift_Stateflow_U.stick_y == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
                 (GearShift_Stateflow_U.vehicle_speed <= 200)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M3;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 13U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 13U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M4;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     case GearShift_Stateflow_IN_M5:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = 15U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 15U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/engine_rpm'
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 2) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.engine_rpm >= 2000) &&
          (GearShift_Stateflow_U.vehicle_speed >= 85)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M6;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 16U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 16U;
      } else if ((GearShift_Stateflow_U.stick_y == 1) &&
                 (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
                 (GearShift_Stateflow_U.vehicle_speed <= 240)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M4;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 14U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 14U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M5;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     default:
      /* Outport: '<Root>/current_gear' */
      /* case IN_M6: */
      GearShift_Stateflow_Y.current_gear = 16U;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = 16U;

      /* Inport: '<Root>/stick_y' incorporates:
       *  Inport: '<Root>/valid'
       *  Inport: '<Root>/vehicle_speed'
       */
      if ((GearShift_Stateflow_U.stick_y == 1) &&
          (GearShift_Stateflow_DW.temporalCounter_i1 >= 3) &&
          (GearShift_Stateflow_U.vehicle_speed <= 280)) {
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M5;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 15U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 15U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.temporalCounter_i1 = 0U;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_M6;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;
    }
  }
}

/* Model step function */
void GearShift_Stateflow_step(void)
{
  int32_T tmp;

  /* Chart: '<Root>/GearShift_Logic' incorporates:
   *  Inport: '<Root>/brake_pedal'
   *  Inport: '<Root>/p_button'
   *  Inport: '<Root>/stick_x'
   *  Inport: '<Root>/stick_y'
   *  Inport: '<Root>/unlock_button'
   *  Inport: '<Root>/valid'
   *  Inport: '<Root>/vehicle_speed'
   */
  if (GearShift_Stateflow_DW.temporalCounter_i1 < 3) {
    GearShift_Stateflow_DW.temporalCounter_i1++;
  }

  if (GearShift_Stateflow_DW.is_active_c3_GearShift_Stateflo == 0) {
    GearShift_Stateflow_DW.is_active_c3_GearShift_Stateflo = 1U;
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
      } else if (GearShift_Stateflow_U.p_button &&
                 (GearShift_Stateflow_U.vehicle_speed < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 0U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 0U;
      } else if (GearShift_Stateflow_U.stick_x == 1) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 2U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 2U;
      } else if (GearShift_Stateflow_U.stick_y == 2) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_M;
        GearShift_Stateflow_DW.is_M = GearShift_Stateflow_IN_Init;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
      }
      break;

     case GearShift_Stateflow_IN_FAULT:
      /* Outport: '<Root>/current_gear' */
      GearShift_Stateflow_Y.current_gear = MAX_uint8_T;

      /* Outport: '<Root>/gear_display' */
      GearShift_Stateflow_Y.gear_display = MAX_uint8_T;
      if (GearShift_Stateflow_U.p_button && (GearShift_Stateflow_U.vehicle_speed
           < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 0U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 0U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_FAULT;
      } else {
        tmp = GearShift_Stateflow_DW.comm_timeout_timer + 1;
        if (GearShift_Stateflow_DW.comm_timeout_timer + 1 > 65535) {
          tmp = 65535;
        }

        GearShift_Stateflow_DW.comm_timeout_timer = (uint16_T)tmp;
      }
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
      } else if (GearShift_Stateflow_U.p_button &&
                 (GearShift_Stateflow_U.vehicle_speed < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 0U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 0U;
      } else if (GearShift_Stateflow_U.stick_x == 2) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 3U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 3U;
      } else if ((GearShift_Stateflow_U.stick_x == 1) &&
                 (GearShift_Stateflow_U.vehicle_speed < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 1U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 1U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
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
      } else if ((GearShift_Stateflow_U.stick_x == 2) &&
                 (GearShift_Stateflow_U.brake_pedal > 30) &&
                 (GearShift_Stateflow_U.vehicle_speed < 5) &&
                 GearShift_Stateflow_U.unlock_button) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_D;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 3U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 3U;
      } else if ((GearShift_Stateflow_U.stick_x == 1) &&
                 (GearShift_Stateflow_U.brake_pedal > 30) &&
                 (GearShift_Stateflow_U.vehicle_speed < 5) &&
                 GearShift_Stateflow_U.unlock_button) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 1U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 1U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
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
      } else if (GearShift_Stateflow_U.p_button &&
                 (GearShift_Stateflow_U.vehicle_speed < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_P;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 0U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 0U;
      } else if ((GearShift_Stateflow_U.stick_x == 2) &&
                 (GearShift_Stateflow_U.vehicle_speed < 5)) {
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_N;

        /* Outport: '<Root>/current_gear' */
        GearShift_Stateflow_Y.current_gear = 2U;

        /* Outport: '<Root>/gear_display' */
        GearShift_Stateflow_Y.gear_display = 2U;
      } else if (GearShift_Stateflow_U.valid) {
        GearShift_Stateflow_DW.comm_timeout_timer = 0U;
        GearShift_Stateflow_DW.is_c3_GearShift_Stateflow =
          GearShift_Stateflow_IN_R;
      } else {
        GearShift_Stateflow_DW.comm_timeout_timer++;
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
