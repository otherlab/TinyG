/*
 * canonical_spindle.c - canonical machine spindle driver
 * Part of TinyG project
 *
 * Copyright (c) 2010 - 2012 Alden S. Hart Jr.
 *
 * TinyG is free software: you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, 
 * or (at your option) any later version.
 *
 * TinyG is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License 
 * for details. You should have received a copy of the GNU General Public 
 * License along with TinyG  If not, see <http://www.gnu.org/licenses/>.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <avr/io.h>
#include "tinyg.h"
#include "gpio.h"
#include "gcode_parser.h"
#include "canonical_machine.h"
#include "spindle.h"
#include "planner.h"
#include "system.h"
#include "pwm.h"

/* 
 * sp_init()
 */
#define SPINDLE_PWM_FREQ 100.0  // Hz
void cm_spindle_init()
{
    pwm_set_freq(PWM_1, SPINDLE_PWM_FREQ);
    pwm_set_duty(PWM_1, 0.1);   // keep alive for ESC
	return;
}

/*
 * cm_spindle_control() -  queue the spindle command to the planner buffer
 */
uint8_t cm_spindle_control(uint8_t spindle_mode)
{
	if (spindle_mode == SPINDLE_CW) {
		mp_queue_mcode(MCODE_SPINDLE_CW);
	} else if (spindle_mode == SPINDLE_CCW) {
		mp_queue_mcode(MCODE_SPINDLE_CCW);
	} else {
		mp_queue_mcode(MCODE_SPINDLE_OFF);	// failsafe operation
	}
	return(TG_OK);
}


double cm_get_spindle_pwm( uint8_t spindle_mode )
{
    if (spindle_mode==SPINDLE_CW || spindle_mode==SPINDLE_CCW )
    {
        double microseconds = gm.spindle_speed;     // hi Alden! yes, this should be in RPM. need a spindle settings page in the eeprom...
        if( microseconds < 1200 )   // minimum ESC turn on power
            microseconds = 1200;
        return ((microseconds-1000)/1000.0  * 0.1) + .1;   // range capped at 0.1-0.2
    }
    else
        return 0.1; // minimum keep alive
}

/*
 * cm_exec_spindle_control() - execute the spindle command (called from planner)
 */
void cm_exec_spindle_control(uint8_t spindle_mode)
{
	cm_set_spindle_mode(spindle_mode);
 	if (spindle_mode == SPINDLE_CW) {
		gpio_set_bit_on(SPINDLE_BIT);
		gpio_set_bit_off(SPINDLE_DIR);
	} else if (spindle_mode == SPINDLE_CCW) {
		gpio_set_bit_on(SPINDLE_BIT);
		gpio_set_bit_on(SPINDLE_DIR);
	} else {
		gpio_set_bit_off(SPINDLE_BIT);	// failsafe: any error causes stop
	}
    
    // PWM spindle control
    pwm_set_duty(PWM_1, cm_get_spindle_pwm(spindle_mode) );
}

/*
 * cm_set_spindle_speed() - queue the S parameter to the planner buffer
 */

uint8_t cm_set_spindle_speed(double speed)
{
//	if (speed > cfg.max_spindle speed) {
//		return (TG_MAX_SPINDLE_SPEED_EXCEEDED);
//	}
	cm_set_spindle_speed_parameter(speed);
    
    // update spindle speed if we're running
    pwm_set_duty(PWM_1, cm_get_spindle_pwm(gm.spindle_mode) );
    
	return (TG_OK);
}

/*
 * cm_exec_spindle_speed() - execute the S command (called from the planner buffer)
 */
void cm_exec_spindle_speed(double speed)
{

}
