#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "driver/mcpwm.h"

#define SERVO_MIN_PULSEWIDTH_US 500
#define SERVO_MAX_PULSEWIDTH_US 2500
#define SERVO_MAX_DEGREE 180

typedef struct {
    mcpwm_unit_t mcpwm_num;
    mcpwm_timer_t timer_num;
    mcpwm_io_signals_t io_signal;
} ServoConfig;

void servo_init(ServoConfig *config);
void servo_set_angle(ServoConfig *config, double angle);
void servo_deinit(ServoConfig *config);

#endif /* SERVO_CONTROL_H */
