#include "servo.h"

void servo_init(ServoConfig *config) {
    mcpwm_gpio_init(config->mcpwm_num, config->io_signal, GPIO_NUM_16);
    mcpwm_config_t pwm_config;
    pwm_config.frequency = 50;  
    pwm_config.cmpr_a = 0;    
    pwm_config.cmpr_b = 0;    
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;

    mcpwm_init(config->mcpwm_num, config->timer_num, &pwm_config);
}

void servo_set_angle(ServoConfig *config, double angle) {
    uint32_t duty_us = SERVO_MIN_PULSEWIDTH_US + (uint32_t)(angle / SERVO_MAX_DEGREE * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US));
    mcpwm_set_duty_in_us(config->mcpwm_num, config->timer_num, MCPWM_OPR_A, duty_us);
}

void servo_deinit(ServoConfig *config) {
    mcpwm_stop(config->mcpwm_num, config->timer_num);
}




