/*
 * canonical_machine.h - rs274/ngc canonical machining functions
 * Part of TinyG project
 *
 * This code is a loose implementation of Kramer, Proctor and Messina's
 * canonical machining functions as described in the NIST RS274/NGC v3
 *
 * Copyright (c) 2010 - 2012 Alden S. Hart Jr.
 *
 * TinyG is free software: you can redistribute it and/or modify it 
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
 */

#ifndef canonical_machine_h
#define canonical_machine_h

/*****************************************************************************
 * GCODE STRUCTURES
 */
struct canonicalMachineSingleton {	// struct to manage cm globals and cycles
	uint32_t linecount;				// count of executed gcode blocks
	uint32_t linenum;				// actual gcode line number (Nxxxxx)
	uint8_t machine_state;			// see cmMachineState
	uint8_t hold_state;				// feedhold sub-state machine
	uint8_t homing_state;			// homing cycle sub-state machine (see note)
	uint8_t status_report_counter;
}; struct canonicalMachineSingleton cm;

// Note: homing state is used both to indicate the homing state of the machine 
// and to keep state during homing operations

/* GCODE MODEL - The following GCodeModel structs are used:
 *
 * - gm keeps the internal gcode state model in normalized, canonical form. 
 *	 All values are unit converted (to mm) and in the machine coordinate 
 *	 system (absolute coordinate system). Gm is owned by the canonical machine 
 *	 layer and should be accessed only through cm_ routines.
 *
 * - gn is used by the gcode interpreter and is re-initialized for each 
 *   gcode block.It accepts data in the new gcode block in the formats 
 *	 present in the block (pre-normalized forms). During initialization 
 *	 some state elements are necessarily restored from gm.
 *
 * - gf is used by the gcode parser interpreter to hold flags for any data 
 *	 that has changed in gn during the parse. gf.target[] values are also used 
 *	 by the canonical machine during set_target().
 *
 * - cfg (config struct in config.h) is also used heavily and contains some 
 *	 values that might be considered to be Gcode model values. The distinction 
 *	 is that all values in the config are persisted and restored, whereas the 
 *	 gm structs are transient. So cfg has the G54 - G59 offsets, but gm has the 
 *	 G92 offsets. cfg has the power-on / reset gcode default values, but gm has
 *	 the operating state for the values (which may have changed).
 */
struct GCodeModel {						// Gcode model- meaning depends on context
	uint8_t next_action;				// handles G modal group 1 moves & non-modals
	uint8_t motion_mode;				// Group1: G0, G1, G2, G3, G38.2, G80, G81,
										// G82, G83 G84, G85, G86, G87, G88, G89 
	uint8_t program_flow;				// currently vestigal - captured, but not uses

	double target[AXES]; 				// XYZABC where the move should go
	double position[AXES];				// XYZABC model position (Note: not used in gn or gf) 
	double origin_offset[AXES];			// XYZABC G92 offsets (Note: not used in gn or gf)

	double feed_rate; 					// F - normalized to millimeters/minute
	double inverse_feed_rate; 			// ignored if inverse_feed_rate not active
	uint8_t inverse_feed_rate_mode;		// TRUE = inv (G93) FALSE = normal (G94)

	uint8_t select_plane;				// G17,G18,G19 - values to set plane to
	uint8_t plane_axis_0;		 		// actual axes of the selected plane
	uint8_t plane_axis_1;		 		// ...(used in gm only)
	uint8_t plane_axis_2; 

	uint8_t coord_system;				// G54-G59 - select coordinate system 1-9
	uint8_t set_coord_offset;			// G10 - coordinate system to apply offset (transient value)
	uint8_t units_mode;					// G20,G21 - 0=inches (G20), 1 = mm (G21)
	uint8_t absolute_override;			// TRUE = move in abs coordinate - this block only (G53)
	uint8_t path_control;				// EXACT_STOP, EXACT_PATH, CONTINUOUS
	uint8_t distance_mode;				// 0=absolute(G90), 1=incremental(G91)
	uint8_t origin_offset_mode;			// G92 - 1=in origin offset mode (G92)

	uint8_t tool;						// T value
	uint8_t change_tool;				// M6
	uint8_t spindle_mode;				// 0=OFF (M5), 1=CW (M3), 2=CCW (M4)
	double spindle_speed;				// in RPM

	double dwell_time;					// P - dwell time in seconds
	double arc_radius;					// R - radius value in arc radius mode
	double arc_offset[3];  				// IJK - used by arc commands

// unimplemented gcode values
//	uint8_t feed_override_mode;			// TRUE = feed override is active, FALSE = inactive
//	double feed_override_rate;			// 1.0000 = set feed rate. Go up or down from there
//	uint8_t	override_enable;			// TRUE = overrides enabled (M48), F=(M49)
//	double cutter_radius;				// D - cutter radius compensation (0 is off)
//	double cutter_length;				// H - cutter length compensation (0 is off)
//	uint8_t mist_coolant_on;			// TRUE = mist on (M7), FALSE = off (M9)
//	uint8_t flood_coolant_on;			// TRUE = flood on (M8), FALSE = off (M9)
};
struct GCodeModel gm;					// active gcode model
struct GCodeModel gn;					// gcode input values
struct GCodeModel gf;					// gcode input flags

/*****************************************************************************
 * GCODE MODEL DEFINES
 *
 * MACHINE STATE
 *
 * The following variables track canonical machine state and state transitions.
 *
 *		- cm.machine_state
 *		- mr.feedhold_state
 *		- cm.cycle_start_asserted
 *
 *	Standard transitions:
 *
 *		machine_state[RESET] ---(cycle_start)---> machine_state[RUN]
 *		machine_state[RUN]	 ---(program_stop)--> machine_state[STOP]
 *		machine_state[RUN]	 ---(program_end)---> machine_state[RESET]
 *		machine_state[RUN]	 ---(abort (^x))----> machine_state[RESET]
 *		machine_state[RUN]	 ---(feedhold)------> machine_state[HOLD]
 *		machine_state[STOP]	 ---(cycle_start)---> machine_state[RUN]
 *		machine_state[HOLD]	 ---(cycle_start)---> machine_state[END_HOLD]
 *		machine_state[END_HOLD] ---(auto)-------> machine_state[RUN or STOP]
 *
 * Other transitions that can happen but are exceptions or ignored 
 *
 *		machine_state[RUN]	 ---(cycle_start)---> machine_state[RUN]
 *		machine_state[HOLD]	 ---(feedhold)------> machine_state[HOLD]
 *
 *	Sub-state machines manage transitions in cycles and feedholds, as well 
 *	as spindle state and program location (i.e. where will the the program
 *	resume after cycle_start is pushed) 
 *
 *	TODO: gm.program_flow needs to be integrated into this
 */
/* COORDINATE SYSTEMS AND OFFSETS
 *
 * Places you may need to touch if you change any of this.
 *	canonical_machine.h	/ cmCoordSystem enum
 *	canonical_machine.c / cm_get_coord_offsets()
 *	config.c / display strings
 *	config.c / cfgArray entries
 */

enum cmMachineState {				// *** Note: check status strings for cm_print_machine_state()
	MACHINE_RESET = 0,				// machine has been reset or aborted
	MACHINE_RUN,					// machine is running
	MACHINE_STOP,					// program stop or no more blocks
	MACHINE_HOLD,					// feedhold in progress
	MACHINE_END_HOLD,				// transitional state to leave feedhold
	MACHINE_HOMING					// homing cycle
};

enum cmFeedholdState {				// applies to cm.feedhold_state
	FEEDHOLD_OFF = 0,				// no feedhold in effect
	FEEDHOLD_SYNC, 					// sync to latest aline segment
	FEEDHOLD_PLAN, 					// replan blocks for feedhold
	FEEDHOLD_DECEL,					// decelerate to hold point
	FEEDHOLD_HOLD					// holding
};

enum cmHomingState {				// applies to cm.homing_state
	// persistent states (must be numbered 0 and 1 as indicated)
	HOMING_NOT_HOMED = 0,			// machine is not homed
	HOMING_HOMED = 1,				// machine is homed
	HOMING_IN_CYCLE					// set when homing is running
};

/* The difference between NextAction and MotionMode is that NextAction is 
 * used by the current block, and may carry non-modal commands, whereas 
 * MotionMode persists across blocks (as G modal group 1)
 */

enum cmNextAction {					// motion mode and non-modals
	NEXT_ACTION_NONE = 0,			// no moves
	NEXT_ACTION_MOTION,				// action set by MotionMode
	NEXT_ACTION_DWELL,				// G4
	NEXT_ACTION_RETURN_TO_HOME,		// G28
	NEXT_ACTION_HOMING_CYCLE,		// G30 cycle
};

enum cmNonModal {
	NON_MODAL_NONE = 0,				// no moves
	NON_MODAL_DWELL,				// G4
	NON_MODAL_SET_COORD_OFFSET,		// G10
	NON_MODAL_RETURN_TO_HOME,		// G28
	NON_MODAL_HOMING_CYCLE,			// G30 cycle
};

enum cmMotionMode {					// G Modal Group 1
	MOTION_MODE_STRAIGHT_TRAVERSE=0,// G0 - seek
	MOTION_MODE_STRAIGHT_FEED,		// G1 - feed
	MOTION_MODE_CW_ARC,				// G2 - arc feed
	MOTION_MODE_CCW_ARC,			// G3 - arc feed
	MOTION_MODE_CANCEL_MOTION_MODE,	// G80
	MOTION_MODE_STRAIGHT_PROBE,		// G38.2
	MOTION_MODE_CANNED_CYCLE_81,	// G81 - drilling
	MOTION_MODE_CANNED_CYCLE_82,	// G82 - drilling with dwell
	MOTION_MODE_CANNED_CYCLE_83,	// G83 - peck drilling
	MOTION_MODE_CANNED_CYCLE_84,	// G84 - right hand tapping
	MOTION_MODE_CANNED_CYCLE_85,	// G85 - boring, no dwell, feed out
	MOTION_MODE_CANNED_CYCLE_86,	// G86 - boring, spindle stop, rapid out
	MOTION_MODE_CANNED_CYCLE_87,	// G87 - back boring
	MOTION_MODE_CANNED_CYCLE_88,	// G88 - boring, spindle stop, manual out
	MOTION_MODE_CANNED_CYCLE_89		// G89 - boring, dwell, feed out
};

enum cmCanonicalPlane {				// canonical plane - translates to:
									// 		axis_0	axis_1	axis_2
	CANON_PLANE_XY = 0,				// G17    X		  Y		  Z
	CANON_PLANE_XZ,					// G18    X		  Z		  Y
	CANON_PLANE_YZ					// G19	  Y		  Z		  X							
};

enum cmUnitsMode {
	INCHES = 0,						// G20
	MILLIMETERS,					// G21
	DEGREES							// ABC axes
};

enum cmCoordSystem {
	ABSOLUTE_COORDS = 0,			// machine coordinate system
	G54,							// G54 coordinate system
	G55,							// G55 coordinate system
	G56,							// G56 coordinate system
	G57,							// G57 coordinate system
	G58,							// G58 coordinate system
	G59								// G59 coordinate system
};

enum cmPathControlMode {			// G Modal Group 13
	PATH_EXACT_STOP = 0,			// G61
	PATH_EXACT_PATH,				// G61.1
	PATH_CONTINUOUS					// G64 and typically the default mode
};

enum cmDistanceMode {
	ABSOLUTE_MODE = 0,				// G90
	INCREMENTAL_MODE				// G91
};

enum cmOriginOffset {
	ORIGIN_OFFSET_SET=0,			// G92 - set origin offsets
	ORIGIN_OFFSET_CANCEL,			// G92.1 - zero out origin offsets
	ORIGIN_OFFSET_SUSPEND,			// G92.2 - do not apply offsets, but preserve the values
	ORIGIN_OFFSET_RESUME			// G92.3 - resume application of the suspended offsets
};

enum cmProgramFlow {
	PROGRAM_FLOW_RUNNING = 0,		// must be zero
	PROGRAM_FLOW_PAUSED,
	PROGRAM_FLOW_COMPLETED
};

enum cmSpindleState {				// spindle settings
	SPINDLE_OFF = 0,
	SPINDLE_CW,
	SPINDLE_CCW
};

enum cmDirection {					// used for spindle and arc dir
	DIRECTION_CW = 0,
	DIRECTION_CCW
};

enum cmAxisMode {					// axis modes (ordered: see _cm_get_feed_time())
	AXIS_DISABLED = 0,				// kill axis
	AXIS_STANDARD,					// axis in coordinated motion w/standard behaviors
	AXIS_INHIBITED,					// axis is computed but not activated
	AXIS_RADIUS,					// rotary axis calibrated to circumference
	AXIS_SLAVE_X,					// rotary axis slaved to X axis
	AXIS_SLAVE_Y,					// rotary axis slaved to Y axis
	AXIS_SLAVE_Z,					// rotary axis slaved to Z axis
	AXIS_SLAVE_XY,					// rotary axis slaved to XY plane
	AXIS_SLAVE_XZ,					// rotary axis slaved to XZ plane
	AXIS_SLAVE_YZ,					// rotary axis slaved to YZ plane
	AXIS_SLAVE_XYZ					// rotary axis slaved to XYZ movement
};	// ordering must be preserved. See _cm_get_feed_time() and seek time()

/* Define modal group internal numbers for checking multiple command violations 
 * and tracking the type of command that is called in the block. A modal group 
 * is a group of g-code commands that are mutually exclusive, or cannot exist 
 * on the same line, because they each toggle a state or execute a unique motion. 
 * These are defined in the NIST RS274-NGC v3 g-code standard, available online, 
 * and are similar/identical to other g-code interpreters by manufacturers 
 * (Haas,Fanuc,Mazak,etc).
 */
enum cmModalGroup {					// thanks, Sonny Jeon.
	MODAL_GROUP_NONE = 0,
	MODAL_GROUP_0, 					// [G4,G10,G28,G30,G53,G92,G92.1] Non-modal
	MODAL_GROUP_1,					// [G0,G1,G2,G3,G80] Motion
	MODAL_GROUP_2,					// [G17,G18,G19] Plane selection
	MODAL_GROUP_3,					// [G90,G91] Distance mode
	MODAL_GROUP_4,					// [M0,M1,M2,M30] Stopping
	MODAL_GROUP_5,					// [G93,G94] Feed rate mode
	MODAL_GROUP_6,					// [G20,G21] Units
	MODAL_GROUP_7,					// [M3,M4,M5] Spindle turning
	MODAL_GROUP_12					// [G54,G55,G56,G57,G58,G59] Coordinate system selection
};
/*****************************************************************************
 * FUNCTION PROTOTYPES
 */
/*--- helper functions for canonical machining functions ---*/
uint8_t cm_get_next_action(void);
uint8_t cm_get_motion_mode(void);
uint8_t cm_get_machine_state(void);
uint8_t cm_get_select_plane(void);
uint8_t cm_get_path_control(void);
uint8_t cm_get_coord_system(void);
uint8_t cm_get_units_mode(void);
uint8_t cm_get_distance_mode(void);
uint8_t cm_isbusy(void);

double cm_get_model_work_position(uint8_t axis);
double *cm_get_model_work_position_vector(double position[]);
double *cm_get_model_canonical_position_vector(double position[]);
double cm_get_runtime_machine_position(uint8_t axis);
double cm_get_runtime_work_position(uint8_t axis);
double cm_get_coord_offset(uint8_t axis);

double *cm_set_vector(double x, double y, double z, double a, double b, double c);
void cm_set_target(double target[], double flag[]);
void cm_set_arc_offset(double i, double j, double k);
void cm_set_arc_radius(double r);
void cm_set_absolute_override(uint8_t absolute_override);

/*--- canonical machining functions ---*/
void cm_init(void);									// init canonical machine

uint8_t cm_select_plane(uint8_t plane);
uint8_t cm_set_machine_coords(double offset[]);
uint8_t cm_set_origin_offsets(uint8_t origin_offset_mode, double offset[], double flag[]); // G92
uint8_t	cm_set_coord_system(uint8_t coord_system);	// G10 (G54...G59)
uint8_t	cm_set_coord_offsets(uint8_t coord_system, double offset[], double flag[]);
uint8_t cm_set_units_mode(uint8_t mode);			// G20, G21
uint8_t cm_set_distance_mode(uint8_t mode);			// G90, G91
uint8_t cm_straight_traverse(double target[]);

uint8_t cm_set_feed_rate(double feed_rate);			// F parameter
uint8_t cm_set_inverse_feed_rate_mode(uint8_t mode);// True= inv mode
uint8_t cm_set_path_control(uint8_t mode);			// G61, G61.1, G64
uint8_t cm_dwell(double seconds);					// G4, P parameter
uint8_t cm_straight_feed(double target[]); 

uint8_t cm_set_spindle_speed(double speed);			// S parameter
uint8_t cm_start_spindle_clockwise(void);			// M3
uint8_t cm_start_spindle_counterclockwise(void);	// M4
uint8_t cm_stop_spindle_turning(void);				// M5
uint8_t cm_spindle_control(uint8_t spindle_mode);	// integrated spindle control command

uint8_t cm_change_tool(uint8_t tool);				// M6, T
uint8_t cm_select_tool(uint8_t tool);				// T parameter

// canonical machine commands not called from gcode dispatcher
void cm_comment(char *comment);						// comment handler
void cm_message(char *message);						// msg to console

void cm_cycle_start(void);							// (no Gcode)
void cm_program_stop(void);							// M0
void cm_optional_program_stop(void);				// M1
void cm_program_end(void);							// M2
void cm_feedhold(void);								// (no Gcode)
void cm_abort(void);								// (no Gcode)
void cm_exec_stop(void);
void cm_exec_end(void);

uint8_t cm_arc_feed(double target[],				// G2, G3
					double i, double j, double k,
					double radius, uint8_t motion_mode);

/*--- canned cycles ---*/

uint8_t cm_return_to_home(void);					// G28
uint8_t cm_return_to_home_callback(void);			// G28 main loop callback

uint8_t cm_homing_cycle(void);						// G30
uint8_t cm_homing_callback(void);					// G30 main loop callback

#endif