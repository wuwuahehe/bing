#ifndef _BOARD_PINS_H
#define _BOARD_PINS_H

#include "driver_gpio.h"   /* GPIO_PIN_x / GPIOx_REG */

/*
 * HR042-R2 (R2 / 2nd generation board) pin map
 * Source: schematic HR042-ZLH-MB-V1.0-20261019A (2026-10-19)
 *
 * IMPORTANT:
 *   The pin <-> net mapping below is INFERRED from the net names in the
 *   schematic text layer. It must be verified with a multimeter on the first
 *   sample before trusting it. Keep every board dependent constant here, so a
 *   verification only touches one file.
 *
 * Differences vs 1st generation board (2024-09-12):
 *   - U1 (HT75XX LDO) removed -> VDDIO is fed by the SoC internal buck only
 *     (LX + L1 10uH + D4). config.h BUCK_MODE_EN must stay 1.
 *   - NTC divider changed: 51K pull-up -> R7 10K pull-up + R8 100R series.
 *   - Battery rail added (VBAT), 40mAh cell.
 */

/* ---- TEC enable (net C_EN, inferred PB4) ---- */
#define PIN_TEC_EN_PORT         GPIOB_REG
#define PIN_TEC_EN_PIN          GPIO_PIN_4
#define TEC_EN_ACTIVE_HIGH      1       /* 1: high = TEC on. Verify polarity (U3). */

/* ---- Fan (net FAN, inferred PB0) ---- */
#define PIN_FAN_PORT            GPIOB_REG
#define PIN_FAN_PIN             GPIO_PIN_0
#define FAN_ACTIVE_HIGH         1       /* 1: high = fan on. Verify. */

/* ---- Touch key (UPT8233 TCH -> net KEY, inferred PB3) ---- */
#define PIN_KEY_PORT            GPIOB_REG
#define PIN_KEY_PIN             GPIO_PIN_3
#define KEY_OUTPUT_LEVEL_MODE   1       /* 1: TCH is level while touched; 0: toggle (U2) */

/* ---- 188 display: 6 charlieplex lines on PA0..PA5 (net IO1..IO6) ---- */
#define PIN_DISP_PORT           GPIOA_REG
#define PIN_DISP_MASK           (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | \
                                 GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5)

/* ---- power / battery ---- */
#define PIN_VBUS_DETECT_PORT    GPIOB_REG
#define PIN_VBUS_DETECT_PIN     GPIO_PIN_9   /* VUSB/PB9, shared with USB detect */
#define BAT_LOW_OFF_MV          3300         /* 1S Li-ion: cut off above 3.0V FD */

#endif /* _BOARD_PINS_H */
