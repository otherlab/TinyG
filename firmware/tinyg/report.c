/*
 * canonical_machine.c - rs274/ngc status report and other reporting functions.
 * Part of TinyG project
 *
 * Copyright (c) 2010 - 2012 Alden S Hart, Jr.
 */
/* TinyG is free software: you can redistribute it and/or modify it 
 * under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, 
 * or (at your option) any later version.
 *
 * TinyG is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 * See the GNU General Public License for details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with TinyG  If not, see <http://www.gnu.org/licenses/>.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "tinyg.h"
#include "util.h"
#include "config.h"
#include "json_parser.h"
#include "controller.h"
#include "planner.h"
#include "gcode_parser.h"
#include "canonical_machine.h"
#include "report.h"
#include "xio/xio.h"
#include "xmega/xmega_rtc.h"
#include "gpio.h"


/*****************************************************************************
 * Status Reports
 *
 *	Status report behaviors
 *
 *	Configuration:
 *
 *		Status reports are configurable only from JSON. SRs are configured
 *		by sending a status report SET object, e.g:
 *
 *		  {"sr":{"line":true,"posx":true,"posy":true....."motm":true,"stat":true}}
 *
 *	Status report formats: The following formats exist for status reports:
 *
 *	  -	JSON format: Returns a JSON object as above, but with the values filled in. 
 *		In JSON form all values are returned as numeric values or enumerations. 
 *		E.g. "posx" is returned as 124.523 and "unit" is returned as 0 for 
 *		inches (G20) and 1 for mm (G21).
 *
 *	  - CSV format: Returns a single line of comma separated token:value pairs.
 *		Values are returned as numeric values or English text.
 *		E.g. "posx" is still returned as 124.523 but "unit" is returned as 
 *		"inch" for inches (G20) and "mm" for mm (G21).
 *
 *	  - Multi-line format: Returns a multi-line report where each value occupies 
 *		one line. Each line contains explanatory English text. Enumerated values are
 *		returned as English text as per CSV form.
 *	
 *	Status report invocation: Status reports can be invoked in the following ways:
 *
 *	  - Ad-hoc request in JSON mode. Issue {"sr":""} (or equivalent). Returns a 
 *		JSON format report (wrapped in a response header, of course).
 *
 *	  - Automatic status reports in JSON mode. Returns JSON format reports 
 *		according to "si" setting.
 *
 *	  - Ad-hoc request in text mode. Triggered by sending ?<cr>. Returns status 
 *		report in multi-line format. Additionally, a line starting with ? will put 
 *		the system into text mode.
 *
 *	  - Automatic status reports in text mode return CSV format according to si setting
 *
 *	  - grbl compatibility forms are not yet supported.
 */

/* rpt_init_status_report()
 *
 *	Call this function to completely re-initialze the status report
 *	Sets SR to hard-coded default and re-initializes sr values in NVM
 */
void rpt_init_status_report(uint8_t persist_flag)
{
	cmdObj cmd;
	uint8_t i=0;
	char sr_defaults[][CMD_TOKEN_LEN+1] = { SR_DEFAULTS };	// see config.h

	cmd.index = cmd_get_index_by_token("sr00");		// find first persistence index
	for (; i < (sizeof(sr_defaults)/(CMD_TOKEN_LEN+1)); i++) {
		cmd.value = cmd_get_index_by_token(sr_defaults[i]);
		cfg.status_report_spec[i] = cmd.value;
		if (persist_flag == true) {
			cmd_write_NVM_value(&cmd);
			cmd.index++;
		}
	}
	for (; i < CMD_STATUS_REPORT_LEN; i++) {		// fill rest of spec with -1
		cmd.value = -1;
		cfg.status_report_spec[i] = cmd.value;
		if (persist_flag == true) {
			cmd_write_NVM_value(&cmd);
			cmd.index++;
		}
	}
	cm.status_report_counter = cfg.status_report_interval;
}

/*	rpt_decr_status_report()  	 - decrement status report counter
 *	rpt_request_status_report()  - force a status report to be sent on next callback
 *	rpt_status_report_callback() - main loop callback to send a report if one is ready
 *	rpt_run_multiline_status_report() - generate a status report in multiline format
 *	rpt_run_status_report()	  	 - populate cmdObj body with status values
 */
void rpt_decr_status_report() 
{
	if (cm.status_report_counter != 0) cm.status_report_counter--; // stick at zero
}

void rpt_request_status_report()
{
	cm.status_report_counter = 0; // report will be called from controller dispatcher
}

uint8_t rpt_status_report_callback() // called by controller dispatcher
{
	if ((cm.machine_state != MACHINE_RESET) && 
		(cfg.status_report_interval > 0) && (cm.status_report_counter == 0)) {
		rpt_populate_status_report();
		cmd_print_list(TG_OK, TEXT_INLINE_PAIRS);	// will report in JSON or inline text modes
		cm.status_report_counter = (cfg.status_report_interval / RTC_PERIOD);	// RTC fires every 10 ms
		return (TG_OK);
	}
	return (TG_NOOP);
}

void rpt_run_multiline_status_report()		// multiple line status report
{
	rpt_populate_status_report();
	cmd_print_list(TG_OK, TEXT_MULTILINE_FORMATTED);
}

uint8_t rpt_populate_status_report()
{
	cmdObj *cmd = cmd_body;

	cmd_clear(cmd);							// wipe it first
	cmd->type = TYPE_PARENT; 				// setup the parent object
	sprintf_P(cmd->token, PSTR("sr"));
//	strcpy(cmd->token, "sr");				// alternate form of above: more RAM, less FLASH & cycles
	cmd = cmd->nx;

	for (uint8_t i=0; i<CMD_STATUS_REPORT_LEN; i++) {
		if ((cmd->index = cfg.status_report_spec[i]) == -1) { continue;}
		if (cmd->index == 0) { break;}
		cmd_get_cmdObj(cmd);
		cmd = cmd->nx;
	}
	return (TG_OK);
}

/*****************************************************************************
 * Queue Reports
 *
 * rpt_request_queue_report()	- request a queue report with current values
 * rpt_queue_report_callback()	- run the queue report w/stored values
 * rpt_run_queue_report() 		- run a queue report right now
 *
 *	Queue reports return 
 *		[lix] - line index 
 *		[pba] - planner buffers available 
 */

struct qrIndexes {			// static data for queue reports
	uint8_t request;		// set to true to request a report
	INDEX_T qr;				// index for QR parent
	INDEX_T lix;			// index for line index
	INDEX_T pba;			// index for planner_buffer_available value
	uint32_t lineindex;
	uint8_t buffers_available;
};
struct qrIndexes qr = { 0,0,0,0,0,0 };		// init to zeros

void rpt_request_queue_report() 
{ 
	qr.lineindex = mp_get_runtime_lineindex();
	qr.buffers_available = mp_get_planner_buffers_available();
	qr.request = true;

#if 1
    if( qr.buffers_available == PLANNER_BUFFER_POOL_SIZE )
        gpio_set_bit_on(0x1 | 0x2);                    // turn on all LEDs, starved
    else
        gpio_set_bit_off(0x1 | 0x2);                    // turn off all LEDs

    if( qr.buffers_available == 0 )
        gpio_set_bit_on(0x4 | 0x8);                    // turn on all LEDs, full
    else
        gpio_set_bit_off(0x4 | 0x8);                    // turn off all LEDs
#endif
}

uint8_t rpt_queue_report_callback()
{
	if (qr.request != true) { return (TG_NOOP);}

	cmdObj *cmd = cmd_body;
	cmd_clear(cmd);			 				// parent qr object			
	sprintf_P(cmd->token, PSTR("qr"));
	cmd->type = TYPE_PARENT;

	cmd = cmd->nx;							// line index
	sprintf_P(cmd->token, PSTR("lix"));
	cmd->value = qr.lineindex;
	cmd->type = TYPE_INTEGER;
	cmd->depth++;

	cmd = cmd->nx;							// planner buffers available
	sprintf_P(cmd->token, PSTR("pba"));
	cmd->value = qr.buffers_available;
	cmd->type = TYPE_INTEGER;
	cmd->depth++;

	cmd_print_list(TG_OK, TEXT_INLINE_PAIRS);// report in JSON or inline text mode
	qr.request = false;
	return (TG_OK);
}

uint8_t rpt_run_queue_report()
{
	cmdObj *cmd = cmd_body;

	if (qr.qr == 0) {					// cache the report indices
		qr.qr = cmd_get_index("qr");	// this only happens once
		qr.lix = cmd_get_index("lix");
		qr.pba = cmd_get_index("pba");
	}

	cmd_clear(cmd);			 			// setup the parent object			
	cmd->type = TYPE_PARENT;
	cmd->index = qr.qr;
	sprintf_P(cmd->token, PSTR("qr"));

	cmd = cmd->nx;
	cmd->index = qr.lix;				// line index element
	cmd_get_cmdObj(cmd);

	cmd = cmd->nx;
	cmd->index = qr.pba;				// planner buffers available element
	cmd_get_cmdObj(cmd);

	return (TG_OK);
}

/****************************************************************************
 ***** Report Unit Tests ****************************************************
 ****************************************************************************/

#ifdef __UNIT_TEST_REPORT

void sr_unit_tests(void)
{
	sr_init();
	tg.communications_mode = TG_JSON_MODE;
	sr_run_status_report();
}

#endif
