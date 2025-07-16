#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
#include <stdbool.h>

#include "qol.h"
#include "opcodes_cb.h"
#include "opcodes_main.h"
#include "apu.h"
#include <SDL2\SDL.h>

// Screen Dimensions.
int scale = 6;
#define SCREEN_WIDTH 960 // (160 * scale)
#define SCREEN_HEIGHT 864 // (144 * scale)


// Clockspeed.
#define CLOCKSPEED 4194304
#define CYCLES_PER_FRAME 69905  

#pragma region Global Vars

int timer_count = 0;
u16 present_clock_speed = 1024;
int divider_count = 0;

// Operands (either one depending on size).
u8 Oper8;
u16 Oper16;

// Cycle Counter
long int cur_cycle_count;

// Stores amount of cycles in last instruction.
int last_cycles_of_inst;

bool interrupt_master_enable = 0;  // Interrupt Master Enable Flag.

// Graphics Variables
int scanline_count;
u8 Tiles[384][8][8];
struct RGB {
	u8 r;
	u8 g;
	u8 b;
} frame_buffer[SCREEN_HEIGHT][SCREEN_WIDTH], color_pallete[4];

// Joypad Variable
u8 controller_state = 0xFF;

// ram Variables
/*
$FFFF           Interrupt Enable Flag
$FF80-$FFFE     Zero Page - 127 bytes
$FF00-$FF7F     Hardware I/O Registers
$FEA0-$FEFF     Unusable ram
$FE00-$FE9F     OAM - Object Attribute ram
$E000-$FDFF     Echo RAM - Reserved, Do Not Use
$D000-$DFFF     Internal RAM - Bank 1-7 (switchable - CGB only)
$C000-$CFFF     Internal RAM - Bank 0 (fixed)
$A000-$BFFF     Cartridge RAM (If Available)
$9C00-$9FFF     BG Map value 2
$9800-$9BFF     BG Map value 1
$8000-$97FF     TILE RAM (Graphics)
$4000-$7FFF     Cartridge ROM - Switchable Banks 1-xx
$0150-$3FFF     Cartridge ROM - Bank 0 (fixed)
$0100-$014F     Cartridge Header Area
$0000-$00FF     Restart and Interrupt Vectors
*/
#define ALT_CART 0
u8 ram[65536] = { 0 };
u8* rom;
u8 bank_offset = 0;
int num_of_banks = 2;//Default based off of Tetris
bool mbc1 = false;
bool mbc2 = false;
bool battery = false; //battery reffereing to the battery powered ram (save file)

// Registers
struct cpu_regs {
	struct {
		union {
			struct {
				u8 f;  // Flag Register. [z,n,h,c,0,0,0,0]
				u8 a;
			};
			u16 af;
		};
	};
	struct {
		union {
			struct {
				u8 c;
				u8 b;
			};
			u16 bc;
		};
	};
	struct {
		union {
			struct {
				u8 e;
				u8 d;
			};
			u16 de;
		};
	};
	struct {
		union {
			struct {
				u8 l;
				u8 h;
			};
			u16 hl;
		};
	};
	u16 sp;  // Stack pointer.
	u16 pc;  // Program counter.
} cpu_regs;

// Instruction Lookup Struct
struct instruction {
	char name[15];
	int num_o_cycles;
	void (*fcnPtr)();
};

//flags
u8 z = 0x80; //Zero flag
u8 n = 0x40; //Negative flag (BCD)
u8 h = 0x20; //Half Carry flag -> ie if the sum of the lower nibbles is greater than 0xF or 00001111
u8 c = 0x10; //Carry flag (sometimes called cy)

//interrupt types
u8 int_vblank = 0;
u8 int_lcd = 1;
u8 int_timer = 2;
u8 int_serial = 3;
u8 int_joypad = 4;

// SDL State
SDL_Event event;
SDL_Renderer* renderer;
SDL_Window* window;
SDL_Texture* texture;
SDL_Surface* logo;
#pragma endregion

#pragma region Function Decs
// Rom Loading
void load_rom(char* filename);
void direct_load_rom(u8* buffer);
void detect_banking_mode();

void load_save();
void save_game();

// User I/O
u8 controller_reg_state();        // Sets up FF00 depending on key presses.
void key_press(int key);    // Does a key press
void key_release(int key);  // Does a key release
void handle_input();        // Detects key presses.

// ram Operations
u8 bus_read(u16 address);              // Read ram at address.
void bus_write(u8 value, u16 address);  // Write ram at address.
void dma_transfer(u8 value);                   // Does a direct ram transfer.

// CPU Operations
void cpu_cycle();  // Reads current opcode then executes instruction. Also prints output.
void check_interrupts();  // Checks if there is any interputs to do and then does them.
void execute_interrupt(u8 interupt);    // Carries out the specified interupt and resets ime.
void enable_interrupt(u8 interupt);   // Allows for check_interrupts to be set.
void print_cpu_regs();                // Prints cpu_regs info.
void update_timers();

// Graphics functions.
void init_HAL();       // Starts SDL Window and render surface.
void setup_color_pallete();  // Sets up the colours. (Todo: load from rom)
void load_tiles();           // Loads Tiles into Array Tiles[][x][y].
void render_tile_map_line(); // Arranges tiles according to tilemap and displays
// onto
// screen.

void render_sprites();    // Renders the sprites.
void display_buffer();    // Loads buffer into texture and renders it.
void render_graphics();   // Combines above.
void shutdown_emu();          // Shuts down SDL and exits.
void set_lcd_status();    // Sets the lcd status register [0xFF41] according to
// the
// current scanline.
void increment_scan_line();

// Arithmetic Instructions (on register a).
void add_byte(u8 value2);                // Adds value2 to register a and sets relevent flags.
u16 add_2_byte(u16 a,	u16 b);  // Adds a to b and sets relevent flags.
void sub_byte(u8 value);                 // Subtracts value from register a and sets relevant flags.
void adc(u8 a);
void cp(u8 value);  // Compare value with register a setting flags. (Basically subtraction without storing value)

u8 inc(u8 value);  // Increment value and set flags.
u8 dec(u8 value);  // Decrement value and set flags.

// Rotations.
u8 RotByteLeft(u8 number);   // Rotate left and set carry flag.
u8 RotByteRight(u8 number);  // Rotate right and set carry flag.

u8 Rotate_Left_Carry(u8 number);   // Rotate left into carry.
u8 Rotate_Right_Carry(u8 number);  // Rotate right into carry.

// Shifts.
u8 Shift_Left(u8 number);   // Shift left into carry.
u8 Shift_Right(u8 number);  // Shift right into carry.

u8 Shift_Right_A(u8 number);  // Arithmetic Shift.

// Swap.
u8 Swap(u8 number);  // Swaps highest 4 bits with lowest 4 bits.


void And(u8 a);  
void Or(u8 a);  
void Xor(u8 a); 

// Stack Instructions: Push places on stack and decrements sp, Pop removes from stack and increments sp
void Push(u16 a);  
u16 Pop();         

// Bit tests.
u8 Bit_Test_w_flags(u8 bit, u8 number);
u8 Bit_Test_no_flags(u8 bit, u8 number);  // Doesn't set flags.

// Bit sets.
u8 Res(u8 bit, u8 number);  // Resets specified bit.
u8 Set(u8 bit, u8 number);  // Sets specified bit.

// Flag functions.
void set_flag(u8 flag_type);
void clear_flag(u8 flag_type);
bool is_flag_set(u8 flag_type);

#pragma endregion

#pragma region Instructions_Lst
const struct instruction instructions[] = {
	{"NOP", 1, NOP},                  //    0x0
	{"LD BC d16", 3, LD_BC_d16},      //    0x1
	{"LD BCp A", 1, LD_BCp_A},        //    0x2
	{"INC BC", 1, INC_BC},            //    0x3
	{"INC B", 1, INC_B},              //    0x4
	{"DEC B", 1, DEC_B},              //    0x5
	{"LD B d8", 2, LD_B_d8},          //    0x6
	{"RLCA", 1, RLCA},                //    0x7
	{"LD a16p SP", 3, LD_a16p_SP},    //    0x8
	{"ADD HL BC", 1, ADD_HL_BC},      //    0x9
	{"LD A BCp", 1, LD_A_BCp},        //    0xa
	{"DEC BC", 1, DEC_BC},            //    0xb
	{"INC C", 1, INC_C},              //    0xc
	{"DEC C", 1, DEC_C},              //    0xd
	{"LD C d8", 2, LD_C_d8},          //    0xe
	{"RRCA", 1, RRCA},                //    0xf
	{"STOP", 1, STOP_0},              //    0x10
	{"LD DE d16", 3, LD_DE_d16},      //    0x11
	{"LD DEp A", 1, LD_DEp_A},        //    0x12
	{"INC DE", 1, INC_DE},            //    0x13
	{"INC D", 1, INC_D},              //    0x14
	{"DEC D", 1, DEC_D},              //    0x15
	{"LD D d8", 2, LD_D_d8},          //    0x16
	{"RLA", 1, RLA},                  //    0x17
	{"JR r8", 2, JR_r8},              //    0x18
	{"ADD HL DE", 1, ADD_HL_DE},      //    0x19
	{"LD A DEp", 1, LD_A_DEp},        //    0x1a
	{"DEC DE", 1, DEC_DE},            //    0x1b
	{"INC E", 1, INC_E},              //    0x1c
	{"DEC E", 1, DEC_E},              //    0x1d
	{"LD E d8", 2, LD_E_d8},          //    0x1e
	{"RRA", 1, RRA},                  //    0x1f
	{"JR NZ r8", 2, JR_NZ_r8},        //    0x20
	{"LD HL d16", 3, LD_HL_d16},      //    0x21
	{"LD HLIp A", 1, LD_HLIp_A},      //    0x22
	{"INC HL", 1, INC_HL},            //    0x23
	{"INC H", 1, INC_H},              //    0x24
	{"DEC H", 1, DEC_H},              //    0x25
	{"LD H d8", 2, LD_H_d8},          //    0x26
	{"DAA", 1, DAA},                  //    0x27
	{"JR Z r8", 2, JR_Z_r8},          //    0x28
	{"ADD HL HL", 1, ADD_HL_HL},      //    0x29
	{"LD A HLIp", 1, LD_A_HLIp},      //    0x2a
	{"DEC HL", 1, DEC_HL},            //    0x2b
	{"INC L", 1, INC_L},              //    0x2c
	{"DEC L", 1, DEC_L},              //    0x2d
	{"LD L d8", 2, LD_L_d8},          //    0x2e
	{"CPL", 1, CPL},                  //    0x2f
	{"JR NC r8", 2, JR_NC_r8},        //    0x30
	{"LD SP d16", 3, LD_SP_d16},      //    0x31
	{"LD HLdp A", 1, LD_HLdp_A},      //    0x32
	{"INC SP", 1, INC_SP},            //    0x33
	{"INC HLp", 1, INC_HLp},          //    0x34
	{"DEC HLp", 1, DEC_HLp},          //    0x35
	{"LD HLp d8", 2, LD_HLp_d8},      //    0x36
	{"SCF", 1, SCF},                  //    0x37
	{"JR C r8", 2, JR_C_r8},          //    0x38
	{"ADD HL SP", 1, ADD_HL_SP},      //    0x39
	{"LD A HLdp", 1, LD_A_HLdp},      //    0x3a
	{"DEC SP", 1, DEC_SP},            //    0x3b
	{"INC A", 1, INC_A},              //    0x3c
	{"DEC A", 1, DEC_A},              //    0x3d
	{"LD A d8", 2, LD_A_d8},          //    0x3e
	{"CCF", 1, CCF},                  //    0x3f
	{"LD B B", 1, LD_B_B},            //    0x40
	{"LD B C", 1, LD_B_C},            //    0x41
	{"LD B D", 1, LD_B_D},            //    0x42
	{"LD B E", 1, LD_B_E},            //    0x43
	{"LD B H", 1, LD_B_H},            //    0x44
	{"LD B L", 1, LD_B_L},            //    0x45
	{"LD B HLp", 1, LD_B_HLp},        //    0x46
	{"LD B A", 1, LD_B_A},            //    0x47
	{"LD C B", 1, LD_C_B},            //    0x48
	{"LD C C", 1, LD_C_C},            //    0x49
	{"LD C D", 1, LD_C_D},            //    0x4a
	{"LD C E", 1, LD_C_E},            //    0x4b
	{"LD C H", 1, LD_C_H},            //    0x4c
	{"LD C L", 1, LD_C_L},            //    0x4d
	{"LD C HLp", 1, LD_C_HLp},        //    0x4e
	{"LD C A", 1, LD_C_A},            //    0x4f
	{"LD D B", 1, LD_D_B},            //    0x50
	{"LD D C", 1, LD_D_C},            //    0x51
	{"LD D D", 1, LD_D_D},            //    0x52
	{"LD D E", 1, LD_D_E},            //    0x53
	{"LD D H", 1, LD_D_H},            //    0x54
	{"LD D L", 1, LD_D_L},            //    0x55
	{"LD D HLp", 1, LD_D_HLp},        //    0x56
	{"LD D A", 1, LD_D_A},            //    0x57
	{"LD E B", 1, LD_E_B},            //    0x58
	{"LD E C", 1, LD_E_C},            //    0x59
	{"LD E D", 1, LD_E_D},            //    0x5a
	{"LD E E", 1, LD_E_E},            //    0x5b
	{"LD E H", 1, LD_E_H},            //    0x5c
	{"LD E L", 1, LD_E_L},            //    0x5d
	{"LD E HLp", 1, LD_E_HLp},        //    0x5e
	{"LD E A", 1, LD_E_A},            //    0x5f
	{"LD H B", 1, LD_H_B},            //    0x60
	{"LD H C", 1, LD_H_C},            //    0x61
	{"LD H D", 1, LD_H_D},            //    0x62
	{"LD H E", 1, LD_H_E},            //    0x63
	{"LD H H", 1, LD_H_H},            //    0x64
	{"LD H L", 1, LD_H_L},            //    0x65
	{"LD H HLp", 1, LD_H_HLp},        //    0x66
	{"LD H A", 1, LD_H_A},            //    0x67
	{"LD L B", 1, LD_L_B},            //    0x68
	{"LD L C", 1, LD_L_C},            //    0x69
	{"LD L D", 1, LD_L_D},            //    0x6a
	{"LD L E", 1, LD_L_E},            //    0x6b
	{"LD L H", 1, LD_L_H},            //    0x6c
	{"LD L L", 1, LD_L_L},            //    0x6d
	{"LD L HLp", 1, LD_L_HLp},        //    0x6e
	{"LD L A", 1, LD_L_A},            //    0x6f
	{"LD HLp B", 1, LD_HLp_B},        //    0x70
	{"LD HLp C", 1, LD_HLp_C},        //    0x71
	{"LD HLp D", 1, LD_HLp_D},        //    0x72
	{"LD HLp E", 1, LD_HLp_E},        //    0x73
	{"LD HLp H", 1, LD_HLp_H},        //    0x74
	{"LD HLp L", 1, LD_HLp_L},        //    0x75
	{"HALT", 1, HALT},                //    0x76
	{"LD HLp A", 1, LD_HLp_A},        //    0x77
	{"LD A B", 1, LD_A_B},            //    0x78
	{"LD A C", 1, LD_A_C},            //    0x79
	{"LD A D", 1, LD_A_D},            //    0x7a
	{"LD A E", 1, LD_A_E},            //    0x7b
	{"LD A H", 1, LD_A_H},            //    0x7c
	{"LD A L", 1, LD_A_L},            //    0x7d
	{"LD A HLp", 1, LD_A_HLp},        //    0x7e
	{"LD A A", 1, LD_A_A},            //    0x7f
	{"ADD A B", 1, ADD_A_B},          //    0x80
	{"ADD A C", 1, ADD_A_C},          //    0x81
	{"ADD A D", 1, ADD_A_D},          //    0x82
	{"ADD A E", 1, ADD_A_E},          //    0x83
	{"ADD A H", 1, ADD_A_H},          //    0x84
	{"ADD A L", 1, ADD_A_L},          //    0x85
	{"ADD A HLp", 1, ADD_A_HLp},      //    0x86
	{"ADD A A", 1, ADD_A_A},          //    0x87
	{"ADC A B", 1, ADC_A_B},          //    0x88
	{"ADC A C", 1, ADC_A_C},          //    0x89
	{"ADC A D", 1, ADC_A_D},          //    0x8a
	{"ADC A E", 1, ADC_A_E},          //    0x8b
	{"ADC A H", 1, ADC_A_H},          //    0x8c
	{"ADC A L", 1, ADC_A_L},          //    0x8d
	{"ADC A HLp", 1, ADC_A_HLp},      //    0x8e
	{"ADC A A", 1, ADC_A_A},          //    0x8f
	{"SUB B", 1, SUB_B},              //    0x90
	{"SUB C", 1, SUB_C},              //    0x91
	{"SUB D", 1, SUB_D},              //    0x92
	{"SUB E", 1, SUB_E},              //    0x93
	{"SUB H", 1, SUB_H},              //    0x94
	{"SUB L", 1, SUB_L},              //    0x95
	{"SUB HLp", 1, SUB_HLp},          //    0x96
	{"SUB A", 1, SUB_A},              //    0x97
	{"SBC A B", 1, SBC_A_B},          //    0x98
	{"SBC A C", 1, SBC_A_C},          //    0x99
	{"SBC A D", 1, SBC_A_D},          //    0x9a
	{"SBC A E", 1, SBC_A_E},          //    0x9b
	{"SBC A H", 1, SBC_A_H},          //    0x9c
	{"SBC A L", 1, SBC_A_L},          //    0x9d
	{"SBC A HLp", 1, SBC_A_HLp},      //    0x9e
	{"SBC A A", 1, SBC_A_A},          //    0x9f
	{"AND B", 1, AND_B},              //    0xa0
	{"AND C", 1, AND_C},              //    0xa1
	{"AND D", 1, AND_D},              //    0xa2
	{"AND E", 1, AND_E},              //    0xa3
	{"AND H", 1, AND_H},              //    0xa4
	{"AND L", 1, AND_L},              //    0xa5
	{"AND HLp", 1, AND_HLp},          //    0xa6
	{"AND A", 1, AND_A},              //    0xa7
	{"XOR B", 1, XOR_B},              //    0xa8
	{"XOR C", 1, XOR_C},              //    0xa9
	{"XOR D", 1, XOR_D},              //    0xaa
	{"XOR E", 1, XOR_E},              //    0xab
	{"XOR H", 1, XOR_H},              //    0xac
	{"XOR L", 1, XOR_L},              //    0xad
	{"XOR HLp", 1, XOR_HLp},          //    0xae
	{"XOR A", 1, XOR_A},              //    0xaf
	{"OR B", 1, OR_B},                //    0xb0
	{"OR C", 1, OR_C},                //    0xb1
	{"OR D", 1, OR_D},                //    0xb2
	{"OR E", 1, OR_E},                //    0xb3
	{"OR H", 1, OR_H},                //    0xb4
	{"OR L", 1, OR_L},                //    0xb5
	{"OR HLp", 1, OR_HLp},            //    0xb6
	{"OR A", 1, OR_A},                //    0xb7
	{"CP B", 1, CP_B},                //    0xb8
	{"CP C", 1, CP_C},                //    0xb9
	{"CP D", 1, CP_D},                //    0xba
	{"CP E", 1, CP_E},                //    0xbb
	{"CP H", 1, CP_H},                //    0xbc
	{"CP L", 1, CP_L},                //    0xbd
	{"CP HLp", 1, CP_HLp},            //    0xbe
	{"CP A", 1, CP_A},                //    0xbf
	{"RET", 1, RET_NZ},               //    0xc0
	{"POP", 1, POP_BC},               //    0xc1
	{"JP NZ a16", 3, JP_NZ_a16},      //    0xc2
	{"JP", 3, JP_a16},                //    0xc3
	{"CALL NZ a16", 3, CALL_NZ_a16},  //    0xc4
	{"PUSH BC", 1, PUSH_BC},          //    0xc5
	{"ADD A d8", 2, ADD_A_d8},        //    0xc6
	{"RST", 1, RST_00H},              //    0xc7
	{"RET Z", 1, RET_Z},              //    0xc8
	{"RET", 1, RET},                  //    0xc9
	{"JP Z a16", 3, JP_Z_a16},        //    0xca
	{"PREFIX", 2, PREFIX_CB},         //    0xcb
	{"CALL Z a16", 3, CALL_Z_a16},    //    0xcc
	{"CALL a16", 3, CALL_a16},        //    0xcd
	{"ADC A d8", 2, ADC_A_d8},        //    0xce
	{"RST", 1, RST_08H},              //    0xcf
	{"RET", 1, RET_NC},               //    0xd0
	{"POP", 1, POP_DE},               //    0xd1
	{"JP NC a16", 3, JP_NC_a16},      //    0xd2
	{"UNKNOWN", 0, NULL},             //    0xd3
	{"CALL NC a16", 3, CALL_NC_a16},  //    0xd4
	{"PUSH DE", 1, PUSH_DE},          //    0xd5
	{"SUB d8", 2, SUB_d8},            //    0xd6
	{"RST", 1, RST_10H},              //    0xd7
	{"RET C", 1, RET_C},              //    0xd8
	{"RETI", 1, RETI},                //    0xd9
	{"JP C a16", 3, JP_C_a16},        //    0xda
	{"UNKNOWN", 0, NULL},             //    0xdb
	{"CALL C a16", 3, CALL_C_a16},    //    0xdc
	{"UNKNOWN", 0, NULL},             //    0xdd
	{"SBC A d8", 2, SBC_A_d8},        //    0xde
	{"RST", 1, RST_18H},              //    0xdf
	{"LDH a8p A", 2, LDH_a8p_A},      //    0xe0
	{"POP HL", 1, POP_HL},            //    0xe1
	{"LD cp A", 1, LD_Cp_A},          //    0xe2
	{"UNKNOWN", 0, NULL},             //    0xe3
	{"UNKNOWN", 0, NULL},             //    0xe4
	{"PUSH HL", 1, PUSH_HL},          //    0xe5
	{"AND D8", 2, AND_d8},            //    0xe6
	{"RST", 1, RST_20H},              //    0xe7
	{"ADD SP r8", 2, ADD_SP_r8},      //    0xe8
	{"JP HLp", 1, JP_HLp},            //    0xe9
	{"LD a16p A", 3, LD_a16p_A},      //    0xea
	{"UNKNOWN", 0, NULL},             //    0xeb
	{"UNKNOWN", 0, NULL},             //    0xec
	{"UNKNOWN", 0, NULL},             //    0xed
	{"XOR D8", 2, XOR_d8},            //    0xee
	{"RST", 1, RST_28H},              //    0xef
	{"LDH A a8p", 2, LDH_A_a8p},      //    0xf0
	{"POP AF", 1, POP_AF},            //    0xf1
	{"LD A cp", 1, LD_A_Cp},          //    0xf2
	{"DI", 1, DI},                    //    0xf3
	{"UNKNOWN", 0, NULL},             //    0xf4
	{"PUSH AF", 1, PUSH_AF},          //    0xf5
	{"OR d8", 2, OR_d8},              //    0xf6
	{"RST", 1, RST_30H},              //    0xf7
	{"LD HL SP+r8", 2, LD_HL_SPr8},   //    0xf8
	{"LD SP HL", 1, LD_SP_HL},        //    0xf9
	{"LD A a16p", 3, LD_A_a16p},      //    0xfa
	{"EI", 1, EI},                    //    0xfb
	{"UNKNOWN", 0, NULL},             //    0xfc
	{"UNKNOWN", 0, NULL},             //    0xfd
	{"CP d8", 2, CP_d8},              //    0xfe
	{"RST", 1, RST_38H},              //    0xff
};

// Array of structures for cb instruction.
const struct instruction CB_instructions[] = {
	{"RLC B", 2, RLC_B},          //    0x0
	{"RLC C", 2, RLC_C},          //    0x1
	{"RLC D", 2, RLC_D},          //    0x2
	{"RLC E", 2, RLC_E},          //    0x3
	{"RLC H", 2, RLC_H},          //    0x4
	{"RLC L", 2, RLC_L},          //    0x5
	{"RLC HLp", 2, RLC_HLp},      //    0x6
	{"RLC A", 2, RLC_A},          //    0x7
	{"RRC B", 2, RRC_B},          //    0x8
	{"RRC C", 2, RRC_C},          //    0x9
	{"RRC D", 2, RRC_D},          //    0xa
	{"RRC E", 2, RRC_E},          //    0xb
	{"RRC H", 2, RRC_H},          //    0xc
	{"RRC L", 2, RRC_L},          //    0xd
	{"RRC HLp", 2, RRC_HLp},      //    0xe
	{"RRC A", 2, RRC_A},          //    0xf
	{"RL B", 2, RL_B},            //    0x10
	{"RL C", 2, RL_C},            //    0x11
	{"RL D", 2, RL_D},            //    0x12
	{"RL E", 2, RL_E},            //    0x13
	{"RL H", 2, RL_H},            //    0x14
	{"RL L", 2, RL_L},            //    0x15
	{"RL HLp", 2, RL_HLp},        //    0x16
	{"RL A", 2, RL_A},            //    0x17
	{"RR B", 2, RR_B},            //    0x18
	{"RR C", 2, RR_C},            //    0x19
	{"RR D", 2, RR_D},            //    0x1a
	{"RR E", 2, RR_E},            //    0x1b
	{"RR H", 2, RR_H},            //    0x1c
	{"RR L", 2, RR_L},            //    0x1d
	{"RR HLp", 2, RR_HLp},        //    0x1e
	{"RR A", 2, RR_A},            //    0x1f
	{"SLA B", 2, SLA_B},          //    0x20
	{"SLA C", 2, SLA_C},          //    0x21
	{"SLA D", 2, SLA_D},          //    0x22
	{"SLA E", 2, SLA_E},          //    0x23
	{"SLA H", 2, SLA_H},          //    0x24
	{"SLA L", 2, SLA_L},          //    0x25
	{"SLA HLp", 2, SLA_HLp},      //    0x26
	{"SLA A", 2, SLA_A},          //    0x27
	{"SRA B", 2, SRA_B},          //    0x28
	{"SRA C", 2, SRA_C},          //    0x29
	{"SRA D", 2, SRA_D},          //    0x2a
	{"SRA E", 2, SRA_E},          //    0x2b
	{"SRA H", 2, SRA_H},          //    0x2c
	{"SRA L", 2, SRA_L},          //    0x2d
	{"SRA HLp", 2, SRA_HLp},      //    0x2e
	{"SRA A", 2, SRA_A},          //    0x2f
	{"SWAP B", 2, SWAP_B},        //    0x30
	{"SWAP C", 2, SWAP_C},        //    0x31
	{"SWAP D", 2, SWAP_D},        //    0x32
	{"SWAP E", 2, SWAP_E},        //    0x33
	{"SWAP H", 2, SWAP_H},        //    0x34
	{"SWAP L", 2, SWAP_L},        //    0x35
	{"SWAP HLp", 2, SWAP_HLp},    //    0x36
	{"SWAP A", 2, SWAP_A},        //    0x37
	{"SRL B", 2, SRL_B},          //    0x38
	{"SRL C", 2, SRL_C},          //    0x39
	{"SRL D", 2, SRL_D},          //    0x3a
	{"SRL E", 2, SRL_E},          //    0x3b
	{"SRL H", 2, SRL_H},          //    0x3c
	{"SRL L", 2, SRL_L},          //    0x3d
	{"SRL HLp", 2, SRL_HLp},      //    0x3e
	{"SRL A", 2, SRL_A},          //    0x3f
	{"BIT 0 B", 2, BIT_0_B},      //    0x40
	{"BIT 0 C", 2, BIT_0_C},      //    0x41
	{"BIT 0 D", 2, BIT_0_D},      //    0x42
	{"BIT 0 E", 2, BIT_0_E},      //    0x43
	{"BIT 0 H", 2, BIT_0_H},      //    0x44
	{"BIT 0 L", 2, BIT_0_L},      //    0x45
	{"BIT 0 HLp", 2, BIT_0_HLp},  //    0x46
	{"BIT 0 A", 2, BIT_0_A},      //    0x47
	{"BIT 1 B", 2, BIT_1_B},      //    0x48
	{"BIT 1 C", 2, BIT_1_C},      //    0x49
	{"BIT 1 D", 2, BIT_1_D},      //    0x4a
	{"BIT 1 E", 2, BIT_1_E},      //    0x4b
	{"BIT 1 H", 2, BIT_1_H},      //    0x4c
	{"BIT 1 L", 2, BIT_1_L},      //    0x4d
	{"BIT 1 HLp", 2, BIT_1_HLp},  //    0x4e
	{"BIT 1 A", 2, BIT_1_A},      //    0x4f
	{"BIT 2 B", 2, BIT_2_B},      //    0x50
	{"BIT 2 C", 2, BIT_2_C},      //    0x51
	{"BIT 2 D", 2, BIT_2_D},      //    0x52
	{"BIT 2 E", 2, BIT_2_E},      //    0x53
	{"BIT 2 H", 2, BIT_2_H},      //    0x54
	{"BIT 2 L", 2, BIT_2_L},      //    0x55
	{"BIT 2 HLp", 2, BIT_2_HLp},  //    0x56
	{"BIT 2 A", 2, BIT_2_A},      //    0x57
	{"BIT 3 B", 2, BIT_3_B},      //    0x58
	{"BIT 3 C", 2, BIT_3_C},      //    0x59
	{"BIT 3 D", 2, BIT_3_D},      //    0x5a
	{"BIT 3 E", 2, BIT_3_E},      //    0x5b
	{"BIT 3 H", 2, BIT_3_H},      //    0x5c
	{"BIT 3 L", 2, BIT_3_L},      //    0x5d
	{"BIT 3 HLp", 2, BIT_3_HLp},  //    0x5e
	{"BIT 3 A", 2, BIT_3_A},      //    0x5f
	{"BIT 4 B", 2, BIT_4_B},      //    0x60
	{"BIT 4 C", 2, BIT_4_C},      //    0x61
	{"BIT 4 D", 2, BIT_4_D},      //    0x62
	{"BIT 4 E", 2, BIT_4_E},      //    0x63
	{"BIT 4 H", 2, BIT_4_H},      //    0x64
	{"BIT 4 L", 2, BIT_4_L},      //    0x65
	{"BIT 4 HLp", 2, BIT_4_HLp},  //    0x66
	{"BIT 4 A", 2, BIT_4_A},      //    0x67
	{"BIT 5 B", 2, BIT_5_B},      //    0x68
	{"BIT 5 C", 2, BIT_5_C},      //    0x69
	{"BIT 5 D", 2, BIT_5_D},      //    0x6a
	{"BIT 5 E", 2, BIT_5_E},      //    0x6b
	{"BIT 5 H", 2, BIT_5_H},      //    0x6c
	{"BIT 5 L", 2, BIT_5_L},      //    0x6d
	{"BIT 5 HLp", 2, BIT_5_HLp},  //    0x6e
	{"BIT 5 A", 2, BIT_5_A},      //    0x6f
	{"BIT 6 B", 2, BIT_6_B},      //    0x70
	{"BIT 6 C", 2, BIT_6_C},      //    0x71
	{"BIT 6 D", 2, BIT_6_D},      //    0x72
	{"BIT 6 E", 2, BIT_6_E},      //    0x73
	{"BIT 6 H", 2, BIT_6_H},      //    0x74
	{"BIT 6 L", 2, BIT_6_L},      //    0x75
	{"BIT 6 HLp", 2, BIT_6_HLp},  //    0x76
	{"BIT 6 A", 2, BIT_6_A},      //    0x77
	{"BIT 7 B", 2, BIT_7_B},      //    0x78
	{"BIT 7 C", 2, BIT_7_C},      //    0x79
	{"BIT 7 D", 2, BIT_7_D},      //    0x7a
	{"BIT 7 E", 2, BIT_7_E},      //    0x7b
	{"BIT 7 H", 2, BIT_7_H},      //    0x7c
	{"BIT 7 L", 2, BIT_7_L},      //    0x7d
	{"BIT 7 HLp", 2, BIT_7_HLp},  //    0x7e
	{"BIT 7 A", 2, BIT_7_A},      //    0x7f
	{"RES 0 B", 2, RES_0_B},      //    0x80
	{"RES 0 C", 2, RES_0_C},      //    0x81
	{"RES 0 D", 2, RES_0_D},      //    0x82
	{"RES 0 E", 2, RES_0_E},      //    0x83
	{"RES 0 H", 2, RES_0_H},      //    0x84
	{"RES 0 L", 2, RES_0_L},      //    0x85
	{"RES 0 HLp", 2, RES_0_HLp},  //    0x86
	{"RES 0 A", 2, RES_0_A},      //    0x87
	{"RES 1 B", 2, RES_1_B},      //    0x88
	{"RES 1 C", 2, RES_1_C},      //    0x89
	{"RES 1 D", 2, RES_1_D},      //    0x8a
	{"RES 1 E", 2, RES_1_E},      //    0x8b
	{"RES 1 H", 2, RES_1_H},      //    0x8c
	{"RES 1 L", 2, RES_1_L},      //    0x8d
	{"RES 1 HLp", 2, RES_1_HLp},  //    0x8e
	{"RES 1 A", 2, RES_1_A},      //    0x8f
	{"RES 2 B", 2, RES_2_B},      //    0x90
	{"RES 2 C", 2, RES_2_C},      //    0x91
	{"RES 2 D", 2, RES_2_D},      //    0x92
	{"RES 2 E", 2, RES_2_E},      //    0x93
	{"RES 2 H", 2, RES_2_H},      //    0x94
	{"RES 2 L", 2, RES_2_L},      //    0x95
	{"RES 2 HLp", 2, RES_2_HLp},  //    0x96
	{"RES 2 A", 2, RES_2_A},      //    0x97
	{"RES 3 B", 2, RES_3_B},      //    0x98
	{"RES 3 C", 2, RES_3_C},      //    0x99
	{"RES 3 D", 2, RES_3_D},      //    0x9a
	{"RES 3 E", 2, RES_3_E},      //    0x9b
	{"RES 3 H", 2, RES_3_H},      //    0x9c
	{"RES 3 L", 2, RES_3_L},      //    0x9d
	{"RES 3 HLp", 2, RES_3_HLp},  //    0x9e
	{"RES 3 A", 2, RES_3_A},      //    0x9f
	{"RES 4 B", 2, RES_4_B},      //    0xa0
	{"RES 4 C", 2, RES_4_C},      //    0xa1
	{"RES 4 D", 2, RES_4_D},      //    0xa2
	{"RES 4 E", 2, RES_4_E},      //    0xa3
	{"RES 4 H", 2, RES_4_H},      //    0xa4
	{"RES 4 L", 2, RES_4_L},      //    0xa5
	{"RES 4 HLp", 2, RES_4_HLp},  //    0xa6
	{"RES 4 A", 2, RES_4_A},      //    0xa7
	{"RES 5 B", 2, RES_5_B},      //    0xa8
	{"RES 5 C", 2, RES_5_C},      //    0xa9
	{"RES 5 D", 2, RES_5_D},      //    0xaa
	{"RES 5 E", 2, RES_5_E},      //    0xab
	{"RES 5 H", 2, RES_5_H},      //    0xac
	{"RES 5 L", 2, RES_5_L},      //    0xad
	{"RES 5 HLp", 2, RES_5_HLp},  //    0xae
	{"RES 5 A", 2, RES_5_A},      //    0xaf
	{"RES 6 B", 2, RES_6_B},      //    0xb0
	{"RES 6 C", 2, RES_6_C},      //    0xb1
	{"RES 6 D", 2, RES_6_D},      //    0xb2
	{"RES 6 E", 2, RES_6_E},      //    0xb3
	{"RES 6 H", 2, RES_6_H},      //    0xb4
	{"RES 6 L", 2, RES_6_L},      //    0xb5
	{"RES 6 HLp", 2, RES_6_HLp},  //    0xb6
	{"RES 6 A", 2, RES_6_A},      //    0xb7
	{"RES 7 B", 2, RES_7_B},      //    0xb8
	{"RES 7 C", 2, RES_7_C},      //    0xb9
	{"RES 7 D", 2, RES_7_D},      //    0xba
	{"RES 7 E", 2, RES_7_E},      //    0xbb
	{"RES 7 H", 2, RES_7_H},      //    0xbc
	{"RES 7 L", 2, RES_7_L},      //    0xbd
	{"RES 7 HLp", 2, RES_7_HLp},  //    0xbe
	{"RES 7 A", 2, RES_7_A},      //    0xbf
	{"SET 0 B", 2, SET_0_B},      //    0xc0
	{"SET 0 C", 2, SET_0_C},      //    0xc1
	{"SET 0 D", 2, SET_0_D},      //    0xc2
	{"SET 0 E", 2, SET_0_E},      //    0xc3
	{"SET 0 H", 2, SET_0_H},      //    0xc4
	{"SET 0 L", 2, SET_0_L},      //    0xc5
	{"SET 0 HLp", 2, SET_0_HLp},  //    0xc6
	{"SET 0 A", 2, SET_0_A},      //    0xc7
	{"SET 1 B", 2, SET_1_B},      //    0xc8
	{"SET 1 C", 2, SET_1_C},      //    0xc9
	{"SET 1 D", 2, SET_1_D},      //    0xca
	{"SET 1 E", 2, SET_1_E},      //    0xcb
	{"SET 1 H", 2, SET_1_H},      //    0xcc
	{"SET 1 L", 2, SET_1_L},      //    0xcd
	{"SET 1 HLp", 2, SET_1_HLp},  //    0xce
	{"SET 1 A", 2, SET_1_A},      //    0xcf
	{"SET 2 B", 2, SET_2_B},      //    0xd0
	{"SET 2 C", 2, SET_2_C},      //    0xd1
	{"SET 2 D", 2, SET_2_D},      //    0xd2
	{"SET 2 E", 2, SET_2_E},      //    0xd3
	{"SET 2 H", 2, SET_2_H},      //    0xd4
	{"SET 2 L", 2, SET_2_L},      //    0xd5
	{"SET 2 HLp", 2, SET_2_HLp},  //    0xd6
	{"SET 2 A", 2, SET_2_A},      //    0xd7
	{"SET 3 B", 2, SET_3_B},      //    0xd8
	{"SET 3 C", 2, SET_3_C},      //    0xd9
	{"SET 3 D", 2, SET_3_D},      //    0xda
	{"SET 3 E", 2, SET_3_E},      //    0xdb
	{"SET 3 H", 2, SET_3_H},      //    0xdc
	{"SET 3 L", 2, SET_3_L},      //    0xdd
	{"SET 3 HLp", 2, SET_3_HLp},  //    0xde
	{"SET 3 A", 2, SET_3_A},      //    0xdf
	{"SET 4 B", 2, SET_4_B},      //    0xe0
	{"SET 4 C", 2, SET_4_C},      //    0xe1
	{"SET 4 D", 2, SET_4_D},      //    0xe2
	{"SET 4 E", 2, SET_4_E},      //    0xe3
	{"SET 4 H", 2, SET_4_H},      //    0xe4
	{"SET 4 L", 2, SET_4_L},      //    0xe5
	{"SET 4 HLp", 2, SET_4_HLp},  //    0xe6
	{"SET 4 A", 2, SET_4_A},      //    0xe7
	{"SET 5 B", 2, SET_5_B},      //    0xe8
	{"SET 5 C", 2, SET_5_C},      //    0xe9
	{"SET 5 D", 2, SET_5_D},      //    0xea
	{"SET 5 E", 2, SET_5_E},      //    0xeb
	{"SET 5 H", 2, SET_5_H},      //    0xec
	{"SET 5 L", 2, SET_5_L},      //    0xed
	{"SET 5 HLp", 2, SET_5_HLp},  //    0xee
	{"SET 5 A", 2, SET_5_A},      //    0xef
	{"SET 6 B", 2, SET_6_B},      //    0xf0
	{"SET 6 C", 2, SET_6_C},      //    0xf1
	{"SET 6 D", 2, SET_6_D},      //    0xf2
	{"SET 6 E", 2, SET_6_E},      //    0xf3
	{"SET 6 H", 2, SET_6_H},      //    0xf4
	{"SET 6 L", 2, SET_6_L},      //    0xf5
	{"SET 6 HLp", 2, SET_6_HLp},  //    0xf6
	{"SET 6 A", 2, SET_6_A},      //    0xf7
	{"SET 7 B", 2, SET_7_B},      //    0xf8
	{"SET 7 C", 2, SET_7_C},      //    0xf9
	{"SET 7 D", 2, SET_7_D},      //    0xfa
	{"SET 7 E", 2, SET_7_E},      //    0xfb
	{"SET 7 H", 2, SET_7_H},      //    0xfc
	{"SET 7 L", 2, SET_7_L},      //    0xfd
	{"SET 7 HLp", 2, SET_7_HLp},  //    0xfe
	{"SET 7 A", 2, SET_7_A},      //    0xff
};

// const u8 Cycles[256] = {
// 	4, 6, 4, 4, 2, 2, 4, 4, 10, 4, 4, 4, 2, 2, 4, 4,  // 0x0_
// 	2, 6, 4, 4, 2, 2, 4, 4, 4,  4, 4, 4, 2, 2, 4, 4,  // 0x1_
// 	0, 6, 4, 4, 2, 2, 4, 2, 0,  4, 4, 4, 2, 2, 4, 2,  // 0x2_
// 	4, 6, 4, 4, 6, 6, 6, 2, 0,  4, 4, 4, 2, 2, 4, 2,  // 0x3_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x4_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x5_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x6_
// 	4, 4, 4, 4, 4, 4, 2, 4, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x7_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x8_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0x9_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0xa_
// 	2, 2, 2, 2, 2, 2, 4, 2, 2,  2, 2, 2, 2, 2, 4, 2,  // 0xb_
// 	0, 6, 0, 6, 0, 8, 4, 8, 0,  2, 0, 0, 0, 6, 4, 8,  // 0xc_
// 	0, 6, 0, 0, 0, 8, 4, 8, 0,  8, 0, 0, 0, 0, 4, 8,  // 0xd_
// 	6, 6, 4, 0, 0, 8, 4, 8, 8,  2, 8, 0, 0, 0, 4, 8,  // 0xe_
// 	6, 6, 4, 2, 0, 8, 4, 8, 6,  4, 8, 2, 0, 0, 4, 8   // 0xf_
// };

#pragma endregion

int main(int argc, char** argv) {

#if ALT_CART == 0
	if (argc < 2) {
        printf("Usage: emu <rom_file>\n");
        return -1;
    }
	else {
		load_rom(argv[1]);
	}
#endif

#if ALT_CART == 1
	size_t buffer_size = 0;
    size_t buffer_cap = 8 * 1024 * 1024; // 8MB should be fine
    u8 *buffer = malloc(buffer_cap);
    if(!buffer){
        perror("malloc");
        return 1;
    }

    size_t nread;
    while((nread = fread(buffer + buffer_size, 1, buffer_cap - buffer_size, stdin)) > 0){
        buffer_size += nread;
        if(buffer_size == buffer_cap){
            buffer_cap *= 2;
            buffer = realloc(buffer, buffer_cap);
            if(!buffer){
                perror("realloc");
                return 1;
            }
        }
    }

    if(ferror(stdin)){
        perror("fread");
        free(buffer);
        return 1;
    }

    direct_load_rom(buffer);
#endif

	detect_banking_mode();

	setup_color_pallete();
	init_HAL();

	// APU TEST ZONE
	#pragma region APU
    SDL_AudioDeviceID dev;
    SDL_AudioSpec want, have;

    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_F32SYS, want.channels = 2;
    want.samples = AUDIO_SAMPLES;
    want.callback = audio_callback;
    want.userdata = NULL;

    printf("Audio driver: %s\n", SDL_GetAudioDeviceName(0, 0));

    if ((dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0)) == 0) {
        printf("SDL could not open audio device: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    audio_init();
    SDL_PauseAudioDevice(dev, 0);
	#pragma endregion
	// Main loop.
	cpu_regs.pc = 0;
	int count = 0;
	Uint32 start = SDL_GetTicks();
	while (1) {
		cur_cycle_count = 0;
		while (cur_cycle_count < CYCLES_PER_FRAME) {

			cpu_cycle();
			count++;
			//printf("\nSTEP1\n");
			update_timers();
			//printf("\nSTEP2\n");
			increment_scan_line();
			//printf("\nSTEP3\n");
			check_interrupts();
			//printf("\nSTEP4\n");
		}

		// Read inputs from SDL
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				print_cpu_regs();
				Uint32 end = SDL_GetTicks();
				printf("%d instructions in %d ms\n", count, end - start);
				shutdown_emu();
				break;
			}
			//printf("CONTROLS TIME");
			handle_input();
		}
	}
	shutdown_emu();

#if ALT_CART == 1
    free(buffer);
    return 0;
#endif
}

#pragma region cart

void load_rom(char *filepath){
    FILE *fptr;
    fptr = fopen(filepath, "rb");

    if(!fptr){printf("*Error in file opening: %s *\n", filepath); return;}

    printf("-Opened: %s-\n", filepath);

    fseek(fptr, 0, SEEK_END);
    u32 rom_size = ftell(fptr);
    
    rewind(fptr);
    //rom is a u8*
    rom = malloc(rom_size);

    fread(rom, rom_size, 1, fptr);
    fclose(fptr);

    printf("-ROM SIZE: %u-\n", rom_size);



}

void direct_load_rom(u8* buffer){
	rom = buffer;
	//printf("-ROM SIZE: %u-\n", b_size);
}

void detect_banking_mode() {
	    u8 c_type = rom[0x147];
    printf("-CART TYPE-0x%02X :", c_type);
    switch (c_type)
    {
    case 0x00:
        printf("-ROM-\n");
        break;
    case 0x01:
        printf("-MBC1-\n");
        mbc1 = true;
        break;
    case 0x02:
        printf("-MBC1 + RAM-\n");
        mbc1 = true;
        break;
    case 0x03:
        printf("-MBC1 + RAM + BATTERY-\n");
        mbc1 = true;
        battery = true;
        break;

	
    default:
        break;
    }
	num_of_banks = (2*(1 << rom[0x148]));
    printf("-ROM BANKS ON CARTRIDGE: %d- \n", num_of_banks);

}
static char serial_data[2];
u8 bus_read(u16 address) {

	if(address == 0xFF01){
		//printf("[SB]: %c", serial_data[0]);
		//printf("")

        return serial_data[0];
		

    }
    if(address == 0xFF02){
		//printf("[SC]: %c", serial_data[0]);
        return serial_data[1];
		
        
    }

	if (address < 0x4000) {
		return rom[address];
	}

	if (address < 0x8000) {
		return rom[address + bank_offset * 0x4000];
	}

	if(address >= 0xFF10 && address <= 0xFF3F){
        //To do: Sound!
        return audio_read(address);
    }

	if (address == 0xFF00) {
		return controller_reg_state();
	}
	return ram[address];
}

void bus_write(u8 value, u16 address) {

	if(address == 0xFF01){
        //printf("SB%02X\n", value);
        //LINE
        serial_data[0] = value;
        // printf("SB%02X\n", value);

		// printf("RAM VAL: %02X", ram[0xFF01]);

		// LINE

		// print_cpu_regs();

        return;

    }
    if(address == 0xFF02){
        //printf("SC%02X\n", value);
        //LINE
        serial_data[1] = value;

        // printf("SC%02X\n", value);

		// printf("RAM VAL: %02X", ram[0xFF02]);

		// LINE

		// print_cpu_regs();


        return;
    }

	if (address >= 0xFF10 && address <= 0xFF3F){
		audio_write(address, value);
	}

	else if (address >= 0x2000 && address <= 0x3FFF) {
		bank_offset = value - 1;
	}

	// Writing to read only ram
	else if (address < 0x8000) {
		return;
	}

	// Execute DMA
	else if (address == 0xFF46) {
		dma_transfer(value);
	}

	// Reset scanline count
	else if (address == 0xFF44) {
		ram[0xFF44] = 0;
	}

	// Reset the divider register
	else if (address == 0xFF04) {
		ram[0xFF04] = 0;
		divider_count = 0;
	}

	else {
		ram[address] = value;
	}
}

void dma_transfer(u8 value) {
	u16 template_start = 0x0000;
    
    u16 source = (value <<8);

    u16 dest = 0xFE00;

    //printf("INIT DMA: 0x%04X to 0x%04X\n", source, source_end);

    for(int i = 0; i < 160; i++){
        bus_write(bus_read(source+i), dest + i);
    }
}

#pragma endregion

#pragma region Graphics and Gamepad

void load_tiles() {
	int s = 0;
	int Rel_x = 0;
	int Rel_y = 0;
	int bitIndex;

	int address = 0x8000;

	while (s < 384) {
		Rel_y = 0;
		while (Rel_y < 8) {
			Rel_x = 0;
			while (Rel_x < 8) {
				bitIndex = 1 << (7 - Rel_x);
				Tiles[s][Rel_x][Rel_y] = (bus_read(address + 2 * Rel_y + 16 * s) & bitIndex ? 1 : 0) + ((bus_read(address + 1 + 2 * Rel_y + 16 * s) & bitIndex) ? 2 : 0);
				Rel_x++;
			}
			Rel_y++;
		}
		s++;
	}
}

void render_tile_map_line() {
	// Check if LCD is enabled
	if (!Bit_Test_no_flags(7, bus_read(0xFF40))) {
		return;
	}

	u8 currentline = bus_read(0xFF44);

	if (currentline >= 144) {
		return;
	}

	u8 ScrollY = bus_read(0xFF42);
	u8 ScrollX = bus_read(0xFF43);
	u8 WindowY = bus_read(0xFF4A);
	u8 WindowX = bus_read(0xFF4B);

	// Which tile value?
	bool unsig = true;
	if (!Bit_Test_no_flags(4, bus_read(0xFF40))) {
		unsig = false;
	}

	// Are we using windowing?
	bool windowingEnabled = false;
	if (Bit_Test_no_flags(5, bus_read(0xFF40))) {
		windowingEnabled = true;
	}

	// Check which tilemap to render.
	int address;
	if (Bit_Test_no_flags(3, bus_read(0xFF40))) {
		address = 0x9C00;
	}
	else {
		address = 0x9800;
	}

	int window_address;
	if (Bit_Test_no_flags(6, bus_read(0xFF40))) {
		window_address = 0x9C00;
	}
	else {
		window_address = 0x9800;
	}

	int yPos = currentline + ScrollY;
	int tileRow = (yPos / 8) % 32 * 32;

	int pixel = 0;
	// Draw non-windowed component
	for (; pixel < 160; pixel++) {
		if (windowingEnabled && pixel >= WindowX && currentline >= WindowY) {
			break;
		}
		int xPos = pixel + ScrollX;
		int tileColumn = (xPos / 8) % 32;

		int tileNum;
		if (unsig) {
			tileNum = bus_read(address + tileRow + tileColumn);
		}
		else {
			tileNum = (signed char)bus_read(address + tileRow + tileColumn) + 0x100;
		}

		//frame_buffer[currentline+(SCREEN_HEIGHT/4)][pixel+(SCREEN_WIDTH/3)] = color_pallete[Tiles[tileNum][xPos % 8][yPos % 8]];

		int baseX = pixel * scale;
		for (int dx = 0; dx < scale; dx++) {
			for (int dy = 0; dy < scale; dy++) {
				frame_buffer[currentline * scale + dy][baseX + dx] = color_pallete[Tiles[tileNum][xPos % 8][yPos % 8]];
			}
		}
	}

	// Draw windowed component
	yPos = currentline - WindowY;
	tileRow = yPos / 8 * 32;
	for (; pixel < 160; pixel++) {
		int xPos = scale * WindowX;
		int tileColumn = (xPos / 8) % 32;
		int tileNum;
		if (unsig) {
			tileNum = bus_read(window_address + tileRow + tileColumn);
		}
		else {
			tileNum = (signed char)bus_read(window_address + tileRow + tileColumn) + 0x100;
		}

		//frame_buffer[currentline][pixel] = color_pallete[Tiles[tileNum][xPos % 8][yPos % 8]];
			
		int baseX = pixel;
		for (int dx = 0; dx < scale; dx++) {
			for (int dy = 0; dy < scale; dy++) {
			frame_buffer[currentline + dy * scale][baseX * scale + dx] = color_pallete[Tiles[tileNum][xPos % 8][yPos % 8]];
		}
		}
		


	}
}

void render_sprites() {
	bool use8x16 = Bit_Test_no_flags(2, bus_read(0xFF40)) != 0; 

	for (int sprite = 0; sprite < 40; sprite++) {
		u8 index = sprite * 4;
		u8 ypos = bus_read(0xFE00 + index) - 16;
		u8 xpos = bus_read(0xFE00 + index + 1) - 8;
		u8 address = bus_read(0xFE00 + index + 2);
		u8 attributes = bus_read(0xFE00 + index + 3);

		bool yflip = Bit_Test_no_flags(6, attributes);
		bool xflip = Bit_Test_no_flags(5, attributes);

		if (ypos == 0 || xpos == 0 || ypos >= 160 || xpos >= 168) {
			continue;
		}

		for (int x = 0; x < 8; x++) {
			for (int y = 0; y < 8; y++) {
				if (Tiles[address][abs(8 * xflip - x)][abs(8 * yflip - y)]) {

					for(int dx = 0; dx<scale; dx++){
						for (int dy = 0; dy<scale; dy++){
							frame_buffer[(((y*scale + (ypos*scale+dy)+SCREEN_HEIGHT)) % (SCREEN_HEIGHT))][((((xpos*scale+dx) + (x*scale)+SCREEN_WIDTH)) % (SCREEN_WIDTH))] =
						color_pallete[Tiles[address][abs(8 * xflip - x)]
						[abs(8 * yflip - y)]];
						}
					}
					
				}

				
			}

		}
	}
}

// Copies frame_buffer to texture. Copies texture to renderer and then displays it.
void display_buffer() {
	SDL_UpdateTexture(texture, NULL, frame_buffer, SCREEN_WIDTH * sizeof(u8) * 3);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}

// Renders the graphics once per frame.
void render_graphics() {
	SDL_Delay(10);
	setup_color_pallete();
	load_tiles();
	render_sprites();
	display_buffer();
}

void increment_scan_line() {
	set_lcd_status();

	if (Bit_Test_no_flags(7, bus_read(0xFF40))) {
		scanline_count -= last_cycles_of_inst;
	}
	else {
		return;
	}

	if (scanline_count <= 0) {
		// Render tilemap line if scanline is completed.
		render_tile_map_line();
		ram[0xFF44]++;
		scanline_count = 456;
		// Check if all lines are finished and if so do a VBLANK.
		if (bus_read(0xFF44) == 144)  
		{
			render_graphics();
			enable_interrupt(0);
		}
		// Reset scanline once it reaches the end.
		else if (bus_read(0xFF44) > 153)  
		{
			ram[0xFF44] = 0;
		}
	}
}

void set_lcd_status() {
	u8 currentline = bus_read(0xFF44);
	u8 current_mode = bus_read(0xFF41) & 0x3;
	u8 new_mode = 0;
	bool interupt_request = false;

	u8 status = bus_read(0xFF41);
	if (!Bit_Test_no_flags(7, bus_read(0xFF40))) {
		// set the mode to 0 during lcd disabled and reset scanline
		scanline_count = 456;
		ram[0xFF44] = 0;
		status = Res(1, status);
		status = Res(0, status);
		bus_write(status, 0xFF41);
		return;
	}

	// Check if in VBLANK (mode 1)
	if (currentline >= 144) {
		new_mode = 1;  // Set status to 01.
		status = Set(0, status);
		status = Res(1, status);
		interupt_request = Bit_Test_no_flags(4, status);
	}
	else {
		// Check if Searching OAM (mode 2)
		if (scanline_count >= 376) {
			new_mode = 2;
			status = Set(1, status);
			status = Res(0, status);
			interupt_request = Bit_Test_no_flags(5, status);
		}
		// Check if Transferring value to LCD Controller (mode 3)
		else if (scanline_count >= 204) {
			new_mode = 3;
			status = Set(1, status);
			status = Set(0, status);
		}
		// Check if in HBLANK (mode 0)
		else {
			new_mode = 0;
			status = Res(1, status);
			status = Res(0, status);
			interupt_request = Bit_Test_no_flags(3, status);
		}
	}

	// Interupt on changing mode
	if (interupt_request && (new_mode != current_mode)) {
		enable_interrupt(1);
	}

	// Handle Coincidence Interupt
	if (bus_read(0xFF44) == bus_read(0xFF45)) {
		status = Set(2, status);
		if (Bit_Test_no_flags(6, status)) {
			enable_interrupt(1);
		}
	}
	else {
		status = Res(2, status);
	}
	bus_write(status, 0xFF41);
}

void handle_input() {
	
	if (event.type == SDL_KEYDOWN) {
		int key = -1;
		switch (event.key.keysym.sym) {
		case SDLK_TAB:
			key = 4; //A
			break;
		case SDLK_LCTRL:
			key = 5; //B
			break;
		case SDLK_RETURN:
			key = 7; //Start
			break;
		case SDLK_BACKSLASH:
			key = 6; //Select
			break;
		case SDLK_RIGHT:
			key = 0; //R
			break;
		case SDLK_LEFT: 
			key = 1; //L
			break;
		case SDLK_UP:
			key = 2; //U
			break;
		case SDLK_DOWN:
			key = 3; //D
			break;
		
		case SDLK_m:
			cpu_regs.pc = 0;
			break;

		case SDLK_ESCAPE:
			shutdown_emu();
		default:
			key = -1;
			break;
		}

		if (key != -1) {
			key_press(key);
		}
	}
	else if (event.type == SDL_KEYUP) {
		int key = -1;

		switch (event.key.keysym.sym) {
		case SDLK_TAB:
			key = 4;
			break;
		case SDLK_LCTRL:
			key = 5;
			break;
		case SDLK_RETURN:
			key = 7;
			break;
		case SDLK_BACKSLASH:
			key = 6;
			break;
		case SDLK_RIGHT:
			key = 0;
			break;
		case SDLK_LEFT:
			key = 1;
			break;
		case SDLK_UP:
			key = 2;
			break;
		case SDLK_DOWN:
			key = 3;
			break;
		default:
			key = -1;
			break;
		}

		if (key != -1) {
			key_release(key);
		}
	}
}

void key_press(int key) {
	bool previouslyUnset = false;

	if (!Bit_Test_no_flags(key, controller_state)) {
		previouslyUnset = true;
	}

	controller_state = Res(key, controller_state);

	// Standard or directional button?
	bool button = key > 3;

	// Check which keys game is interested in and perform check_interrupts
	bool request_interupt = false;
	if (button && !Bit_Test_no_flags(5, bus_read(0xFF00))) {
		request_interupt = true;
	}
	else if (!button && !Bit_Test_no_flags(4, bus_read(0xFF00))) {
		request_interupt = true;
	}

	if (request_interupt && !previouslyUnset) enable_interrupt(4);
}

void key_release(int key) { controller_state = Set(key, controller_state); }

u8 controller_reg_state() {
	u8 res = ram[0xFF00];
	res ^= 0xFF;

	// Are we interested in the standard buttons?
	if (!Bit_Test_no_flags(4, res)) {
		BYTE topJoypad = controller_state >> 4;
		topJoypad |= 0xF0;  // turn the top 4 bits on
		res &= topJoypad;   // show what buttons are pressed
	}
	// Or directional buttons?
	else if (!Bit_Test_no_flags(5, res))  // directional buttons
	{
		BYTE bottomJoypad = controller_state & 0xF;
		bottomJoypad |= 0xF0;
		res &= bottomJoypad;
	}
	return res;
}

#pragma endregion

#pragma region Startup and Shutdown

void init_HAL() {
	SDL_Init(SDL_INIT_VIDEO);
	SDL_Init(SDL_INIT_AUDIO);
	SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window,
		&renderer);
	logo = SDL_LoadBMP("projectLogo.bmp");
	SDL_SetWindowIcon(window, logo);
	
	//SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
		SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
		SCREEN_HEIGHT);
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);
}

void setup_color_pallete() {
	u8 color;
	for (int i = 0; i < 4; i++) {
		color = ((0x3 << 2 * i) & ram[0xFF47]) >> 2 * i;
		switch (color) {
		case (0x0):
			color_pallete[i].r = 255;
			color_pallete[i].g = 255;
			color_pallete[i].b = 255;
			break;
		case (0x1):
			color_pallete[i].r = 180;
			color_pallete[i].g = 180;
			color_pallete[i].b = 180;
			break;
		case (0x2):
			color_pallete[i].r = 110;
			color_pallete[i].g = 110;
			color_pallete[i].b = 110;
			break;
		case (0x3):
			color_pallete[i].r = 0;
			color_pallete[i].g = 0;
			color_pallete[i].b = 0;
			break;
		}
	}
}

void shutdown_emu() {
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	exit(1);
}

#pragma endregion

#pragma region Dbg
// Used for Debugging. (Prints out Registers)
void print_cpu_regs() {
	printf("af: %04x \n", cpu_regs.af);
	printf("bc: %04x \n", cpu_regs.bc);
	printf("de: %04x \n", cpu_regs.de);
	printf("hl: %04x \n", cpu_regs.hl);
	printf("sp: %04x \n", cpu_regs.sp);
	printf("pc: %04x \n", cpu_regs.pc);
	printf("Stack Value: %04x \n",
		((ram[cpu_regs.sp] << 8) | ram[cpu_regs.sp + 1]));
	printf("0x%x: %s ", cpu_regs.pc, instructions[ram[cpu_regs.pc]].name);
	printf("(0x%x)\n", ram[cpu_regs.pc]);
	printf("IME: %x\n", interrupt_master_enable);
	printf("Oper16: %04x \n", Oper16);
	printf("Oper8: %02x \n", Oper8);
}
#pragma endregion

#pragma region CPU

void cpu_cycle() {
	u8 opcode = bus_read(cpu_regs.pc);
	u8 num_o_cycles = instructions[opcode].num_o_cycles;

	switch (num_o_cycles) {
		case 2:
			Oper8 = bus_read(cpu_regs.pc + 1);
			break;
		case 3:

			Oper16 = bus_read(cpu_regs.pc + 1) | (bus_read(cpu_regs.pc + 2) << 8);
			break;
		default:
			break;
	}


	if (num_o_cycles == 0) {
    	cpu_regs.pc += 1;
	} else {
    	cpu_regs.pc += num_o_cycles;
	}

	if (opcode == 0xCB && num_o_cycles == 2) {
		CB_instructions[Oper8].fcnPtr();
	} else {
		instructions[opcode].fcnPtr();
	}

	//u8 cycles = Cycles[opcode];
	cur_cycle_count += num_o_cycles;//tied to framerate
	last_cycles_of_inst = num_o_cycles;//tied to framerate
}

void check_interrupts(){
    if (interrupt_master_enable){
        u8 current_IF_state = bus_read(0xFF0F);
        if (current_IF_state){
            for(int i = 0; i < 8; i ++){
                if(Bit_Test_no_flags(i, current_IF_state)){
                    if(Bit_Test_no_flags(i, bus_read(0xFFFF))){
                        execute_interrupt(i);
                    }
                }
            }
        }
    }
}

void execute_interrupt(u8 interupt) {
	Push(cpu_regs.pc);
    bus_write(Res(interupt, bus_read(0xFF0F)), 0xFF0F);
    interrupt_master_enable = false;

    switch (interupt)
    {
    case 0:
        cpu_regs.pc = 0x40; //VBLANK
        break;
    case 1:
        cpu_regs.pc = 0x48; //LCD
        break;
    case 2:
        cpu_regs.pc = 0x50; //Timer
        break;
    case 3: //interesting that the bprint does not allow for serial interrupts
        cpu_regs.pc = 0x58; //Serial
        break;
    case 4:
        cpu_regs.pc = 0x60; //Joypad
        break;
    default:
        break;
    }

    //cpu cycle?
    // cpu_cycle();
    // Pop(cpu_regs.pc);
    // interrupt_master_enable = true;
}

void enable_interrupt(u8 interrupt_type){
    bus_write(Set(interrupt_type, bus_read(0xFF0F)), 0xFF0F);
    //there's a few rundant things here if you ask me, the bus_write would be unnecessary if the set function would've actually modified the value
}

void update_timers() {
	// Update Divider Register
	u8 timer_control = bus_read(0xFF07);
	divider_count += last_cycles_of_inst;
	if (divider_count >= 256) {
		divider_count = 0;
		ram[0xFF04] = bus_read(0xFF04) + 1;
	}

	// Update Main Timer Clock Speed
	switch (timer_control & 0x3)
    {
    case 0:
        present_clock_speed = 1024;
        break;
    
    case 1:
        present_clock_speed = 16;
        break;

    case 2:
        present_clock_speed = 64;    
        break;

    case 3:
        present_clock_speed = 256;
        break;

    }

	// Tick Main Timer
	if (Bit_Test_no_flags(2, timer_control)) {
		timer_count += last_cycles_of_inst;
		u8 tima = bus_read(0xFF05);
		//(cur_cycle_count % present_clock_speed==0)-> mathematically elegant but apparently, taxing
		if (timer_count >= present_clock_speed) {
			timer_count = 0;
			bus_write(tima + 1, 0xFF05);

			if (tima == 0xFF) {
				bus_write(bus_read(0xFF06), 0xFF05);
				enable_interrupt(int_timer);
			}
		}
	}
}


#pragma endregion

#pragma region Flags
// Functions to Set Flags.
void set_flag(u8 flag_type){
	cpu_regs.f |= flag_type;
}

void clear_flag(u8 flag_type){
	cpu_regs.f &= ~flag_type;
}

bool is_flag_set(u8 flag_type){
    return (cpu_regs.f & flag_type);
}

#pragma endregion

#pragma region ALU
// Normal rotates (Set carry flag).
u8 RotByteLeft(u8 number) {
//The bit that's rotated, gets stor in carry
    
    u8 carry = number & 0x80;
    carry >>=7;
    number = (number << 1) | carry;
    
    if(carry){set_flag(c);}else{clear_flag(c);}

    if(number){set_flag(z);}else{clear_flag(z);}

    clear_flag(n);
    clear_flag(h);
    
    return number;
}

u8 RotByteRight(u8 number) {
	u8 carry = number & 0x01;
    carry <<=7;
    number = (number >> 1) | carry;
    
    if(carry){set_flag(c);}else{clear_flag(c);}

    if(number){set_flag(z);}else{clear_flag(z);}

    clear_flag(n);
    clear_flag(h);
    
    return number;
}

// Rotates through Carry.
u8 Rotate_Right_Carry(u8 number) {
	u8 carry = number & 0x01;
    //carry <<=7;
    number >>=1;

    if(is_flag_set(c)){number+= 0x80;}

    if(carry){set_flag(c);}else{clear_flag(c);}

    if(number){clear_flag(z);}else{set_flag(z);}

    clear_flag(n);
    clear_flag(h);

    return number;
}

u8 Rotate_Left_Carry(u8 number) {
	   u8 carry = number & 0x80;
    //carry >>=7;
    number <<=1;

    if(is_flag_set(c)){number++;}

    if(carry){set_flag(c);}else{clear_flag(c);}

    if(number){clear_flag(z);}else{set_flag(z);}

    clear_flag(n);
    clear_flag(h);

    return number;
}

// Shifts.

u8 Shift_Left(u8 number){
    if(number & 0x80){
        set_flag(c);
    }
    else{
        clear_flag(c);
    }

    number <<=1;

    clear_flag(h);
    clear_flag(n);

    if(number){
        clear_flag(z);
    }
    else{
        set_flag(z);
    }
    return number;
}               // Shift left into carry.
u8 Shift_Right(u8 number){
    if(number & 0x01){
        set_flag(c);
    }
    else{
        clear_flag(c);
    }

    number >>=1;

    clear_flag(h);
    clear_flag(n);

    if(number){
        clear_flag(z);
    }
    else{
        set_flag(z);
    }
    return number;
}             // Shift right into carry.

u8 Shift_Right_A(u8 number){
    if(number & 0x01){
        set_flag(c);
    }
    else{
        clear_flag(c);
    }

    number = (number >> 1) | (number % 0x80); //ummm ?

    clear_flag(h);
    clear_flag(n);

    if(number){
        clear_flag(z);
    }
    else{
        set_flag(z);
    }

    return number;

}            // Arithmetic Shift.


// Swap.
u8 Swap(u8 number){
    u8 lo = (number &0xF);
    u8 hi = (number & ~0xF);

    if(number){clear_flag(z);}else{set_flag(z);}

    clear_flag(h);
    clear_flag(c);
    clear_flag(n);

    return (lo << 4)|(hi >> 4);
}      

// Add and sub.
void add_byte(u8 Value2) {
    int res = cpu_regs.a + Value2;

    if(res & 0xff00){
        //set c flag -> this is an 8 bit op, only one turn on condition
        set_flag(c);
    }
    else{
        clear_flag(c);
    }
    
    if(((cpu_regs.a & 0xF) + (Value2 & 0xF)) > 0xF){set_flag(h);}
    else{clear_flag(h);}

    clear_flag(n);

    cpu_regs.a = (u8)(res & 0xFF); // wraps in case of 8 bit overflow
    //wayyy better than implicit casting cause I mean who would be dumb enough to do that(- __ -)

    if(cpu_regs.a){
        clear_flag(z);
    }
    else{
        set_flag(z);
    }


}  

u16 add_2_byte(u16 a, u16 b){
    //just a modified add_byte to 16 bit
    unsigned long res = a + b;

    if(res & 0xffff0000){
        //set c flag -> this is an 8 bit op, only one turn on condition
        set_flag(c);
    }
    else{
        clear_flag(c);
    }
    
    if(((a & 0xFF) + (b & 0xFF)) > 0xFF){set_flag(h);}
    else{clear_flag(h);}

    clear_flag(n);

    //cpu_regs.a = (u8)(res & 0xFF); // wraps in case of 8 bit overflow
    //wayyy better than implicit casting cause I mean who would be dumb enough to do that(- __ -)

    if(cpu_regs.a == 0){
        set_flag(z);
    }
    else{
        clear_flag(z);
    }


    return (u16)(res & 0xffff);

}    

void sub_byte(u8 value) {
    //int res = cpu_regs.a - value;

    set_flag(n); //always set n, since it's subtraction
    if(value > cpu_regs.a){set_flag(c);}else{clear_flag(c);} // if outcome is neg, set c

    if((cpu_regs.a & 0xF) < (value&0xF)){ //if outcome(lower nibble) is neg, set h
        set_flag(h);
    }
    else{
        clear_flag(h);
    }

    cpu_regs.a -= value;
    if(cpu_regs.a == 0){set_flag(z);}else{clear_flag(z);} //if outcome is 0, set z

}

// Does Subtraction only setting flags.
void cp(u8 value) {
    //int res = cpu_regs.a-value;

    set_flag(n);

    if((cpu_regs.a & 0xF) < (value &0xF)){
        set_flag(h);
    }else{clear_flag(h);}

    if(cpu_regs.a < value){set_flag(c);}else{clear_flag(c);}

    if(cpu_regs.a == value){set_flag(z);}else{clear_flag(z);}
}  

void adc(u8 a) {
    int value = a;

    //add carry adds one, if c flag is set
    if(cpu_regs.f & 0x10)
    {
        value++;
    }

    int res = cpu_regs.a + value;

    if(res & 0xff00){
        //set c flag -> this is an 8 bit op, only one turn on condition
        set_flag(c);
    }
    else{
        clear_flag(c);
    }
    
    if(((cpu_regs.a & 0xF) + (res & 0xF)) > 0xF){set_flag(h);}
    else{clear_flag(h);}

    clear_flag(n);

    cpu_regs.a = (u8)(res & 0xFF); // wraps in case of 8 bit overflow
    //wayyy better than implicit casting cause I mean who would be dumb enough to do that(- __ -)

    if(cpu_regs.a){clear_flag(z);} else{set_flag(z);}

}

void Sbc(u8 value) {
    int value_int = value;
    if(cpu_regs.f & 0x10){
        value++;
    }

    set_flag(n);

    if((value_int) > (cpu_regs.a)){
        set_flag(c);
    }
    else{
        clear_flag(c);
    }

    if((value_int & 0xF) > (cpu_regs.a & 0xF)){
        set_flag(h);
    }
    else{
        clear_flag(h);
    }

    cpu_regs.a -= value_int;

    if(cpu_regs.a){
        clear_flag(z);
    }else{
        set_flag(z);
    }
}

u8 inc(u8 value) {
  	clear_flag(n);

    if((value & 0xF) == 0xF){set_flag(h);}else{clear_flag(h);} //checks pre increment, to determine if it will carry upon increment

    value ++;

    if(value){clear_flag(z);}else{set_flag(z);}

    return value;
}

u8 dec(u8 value) {
    int res = value-1;

    set_flag(n);

    if((value &0xF)){clear_flag(h);}else{set_flag(h);} //if the lower nibble is 0000 it means the upper half will have stuff borrowed from it(ie half carry)

    value --;

    if(value){clear_flag(z);}else{set_flag(z);}

    return value;
}

void And(u8 a) {
    cpu_regs.a &= a;

    set_flag(h);

    clear_flag(c);
    clear_flag(n);

    if(cpu_regs.a){clear_flag(z);}else{set_flag(z);}
}

void Or(u8 a){

    cpu_regs.a |= a;

    clear_flag(h);
    clear_flag(c);
    clear_flag(n);

    if(cpu_regs.a){clear_flag(z);}else{set_flag(z);}

}

void Xor(u8 a){

    cpu_regs.a ^= a;

    clear_flag(h);
    clear_flag(c);
    clear_flag(n);

    if(cpu_regs.a){clear_flag(z);}else{set_flag(z);}

}

// Bit test.
u8 Bit_Test_w_flags(u8 bit, u8 number){
    clear_flag(n);
    set_flag(h);

    if(number & (1 << bit)){
        clear_flag(z);
        return 1;
    }
    else{
        set_flag(z);
        return 0;
    }

}

u8 Bit_Test_no_flags(u8 bit, u8 number) {
	u8 bitindex = 0x1 << bit;
	if (number & bitindex) {
		return 1;
	}
	else {
		return 0;
	}
}

// Bit sets.
u8 Res(u8 bit, u8 number){
    u8 mask = ~(1 << bit) ; //inverted bit mask
    return number & mask; //AND to reset the number regardless of it's initial value
}              // Resets specified bit.
u8 Set(u8 bit, u8 number){
    u8 mask = (1 << bit) ; //standard bit mask
    return number | mask; //OR to set the number regardless of it's initial value
} 

// Stack Instructions.
void Push(u16 a) {
	cpu_regs.sp -= 2;
	bus_write((u8)((a >> 8) & 0x00FF), cpu_regs.sp);
	bus_write((u8)(a & 0x00FF), cpu_regs.sp + 1);
}

u16 Pop() {
	u16 output;
	u8 lo = bus_read(cpu_regs.sp+1);
	u8 hi = bus_read(cpu_regs.sp);
	output = (hi << 8) + lo;
	cpu_regs.sp += 2;
	return output;
}

#pragma endregion

#pragma region Instructions_MAIN
// Instruction Implementations.
void NOP(){
}

void LD_BC_d16(){
    cpu_regs.bc = Oper16;
}   //    0x1
void LD_BCp_A(){
    bus_write(cpu_regs.a, cpu_regs.bc);
}    //    0x2
void INC_BC(){
    cpu_regs.bc ++;
}      //    0x3
void INC_B(){
    cpu_regs.b = inc(cpu_regs.b);
}       //    0x4
void DEC_B(){
    cpu_regs.b = dec(cpu_regs.b);
}       //    0x5
void LD_B_d8(){
    cpu_regs.b = Oper8;
}     //    0x6
void RLCA(){
    cpu_regs.a = RotByteLeft(cpu_regs.a);
    clear_flag(z);
}        //    0x7
void LD_a16p_SP(){
    bus_write((u8) (cpu_regs.sp & 0x00FF),Oper16);
    bus_write((u8) ((cpu_regs.sp>>8) & 0x00FF),Oper16+1);
}  //    0x8
void ADD_HL_BC(){
    //cpu_regs.hl += cpu_regs.bc;
    cpu_regs.hl = add_2_byte(cpu_regs.hl, cpu_regs.bc);
}   //    0x9
void LD_A_BCp(){
    cpu_regs.a = bus_read(cpu_regs.bc);
}    //    0xa
void DEC_BC(){
    cpu_regs.bc --;
}      //    0xb
void INC_C(){
    cpu_regs.c = inc(cpu_regs.c);
}       //    0xc
void DEC_C(){
    cpu_regs.c = dec(cpu_regs.c);
}       //    0xd
void LD_C_d8(){
    cpu_regs.c = Oper8;
}     //    0xe
void RRCA(){
    cpu_regs.a = RotByteRight(cpu_regs.a);
    clear_flag(z);
}        //    0xf
void STOP_0(){
    printf("\n***NON IMPL-STOP-***\n");
    printf("CURRENT INSTRUCTION: %02X", bus_read(cpu_regs.pc));
}      //    0x10
void LD_DE_d16(){
    cpu_regs.de = Oper16;
}   //    0x11
void LD_DEp_A(){
    bus_write(cpu_regs.a,cpu_regs.de);
}    //    0x12
void INC_DE(){
    cpu_regs.de ++;
}      //    0x13
void INC_D(){
    cpu_regs.d = inc(cpu_regs.d);
}       //    0x14
void DEC_D(){
    cpu_regs.d = dec(cpu_regs.d);
}       //    0x15
void LD_D_d8(){
    cpu_regs.d = Oper8;
}     //    0x16
void RLA(){
    cpu_regs.a = Rotate_Left_Carry(cpu_regs.a);
    clear_flag(z);
}         //    0x17
void JR_r8(){
    cpu_regs.pc += (signed char)Oper8; //why char?
}       //    0x18
void ADD_HL_DE(){
    cpu_regs.hl = add_2_byte(cpu_regs.hl, cpu_regs.de);
}   //    0x19
void LD_A_DEp(){
    cpu_regs.a = bus_read(cpu_regs.de);
}    //    0x1a
void DEC_DE(){
    cpu_regs.de --;
}      //    0x1b
void INC_E(){
    cpu_regs.e = inc(cpu_regs.e);
}       //    0x1c
void DEC_E(){
    cpu_regs.e = dec(cpu_regs.e);
}       //    0x1d
void LD_E_d8(){
    cpu_regs.e = Oper8;
}     //    0x1e
void RRA(){
    cpu_regs.a = Rotate_Right_Carry(cpu_regs.a);
    clear_flag(z);
}         //    0x1f
void JR_NZ_r8(){
    if(!is_flag_set(z)){
        cpu_regs.pc += (signed char)Oper8;
    }

}    //    0x20
void LD_HL_d16(){
    cpu_regs.hl = Oper16;
    
}   //    0x21
void LD_HLIp_A(){
    bus_write(cpu_regs.a,cpu_regs.hl);
    cpu_regs.hl ++;
}   //    0x22
void INC_HL(){
    cpu_regs.hl ++;
}      //    0x23
void INC_H(){
    cpu_regs.h = inc(cpu_regs.h);
}       //    0x24
void DEC_H(){
    cpu_regs.h = dec(cpu_regs.h);
}       //    0x25
void LD_H_d8(){
    cpu_regs.h = Oper8;
}     //    0x26
void DAA(){
    
    //Decimal Adjust Accumulator
    unsigned short test = cpu_regs.a; //more similar to an int as opposed to a u8
    if(!is_flag_set(n)){
        if((cpu_regs.a & 0xF) > 9 || is_flag_set(h)){
            test += 0x06;
        }
        if((cpu_regs.a > 0x9F) || is_flag_set(c)){
            test += 0x60;
            
            
        }
        
    }
    else{
        if(is_flag_set(h)){
            test = (test - 0x06) & 0xFF;
        }
        if(is_flag_set(c)){
            test -=0x60;
            
        }
    }   

    cpu_regs.a = test;

    clear_flag(h);

    if(test > 0x99){set_flag(c);}

    if(cpu_regs.a){clear_flag(z);}else{
        set_flag(z);
    }


}         //    0x27
void JR_Z_r8(){
    if(is_flag_set(z)){
        cpu_regs.pc += (signed char)Oper8;
    }
}     //    0x28
void ADD_HL_HL(){
    cpu_regs.hl = add_2_byte(cpu_regs.hl , cpu_regs.hl);
}   //    0x29
void LD_A_HLIp(){
    cpu_regs.a = bus_read(cpu_regs.hl);
    cpu_regs.hl ++;
}   //    0x2a
void DEC_HL(){
    cpu_regs.hl --;
}      //    0x2b
void INC_L(){
    cpu_regs.l = inc(cpu_regs.l);
}       //    0x2c
void DEC_L(){
    cpu_regs.l = dec(cpu_regs.l);
}       //    0x2d
void LD_L_d8(){
    cpu_regs.l = Oper8;
}     //    0x2e
void CPL(){
    cpu_regs.a = ~cpu_regs.a;
    clear_flag(n);
    clear_flag(h); 
}        //    0x2f

void JR_NC_r8(){
    if(!is_flag_set(c)){
        cpu_regs.pc += (signed char)Oper8;
    }
}    //    0x30
void LD_SP_d16(){
    cpu_regs.sp = Oper16;
}   //    0x31
void LD_HLdp_A(){
    bus_write(cpu_regs.a,cpu_regs.hl);
    cpu_regs.hl --;
}   //    0x32
void INC_SP(){
    cpu_regs.sp ++;
}      //    0x33
void INC_HLp(){
    bus_write(inc(bus_read(cpu_regs.hl)), cpu_regs.hl);
}     //    0x34
void DEC_HLp(){
    bus_write(dec(bus_read(cpu_regs.hl)), cpu_regs.hl);
}     //    0x35
void LD_HLp_d8(){
    bus_write(Oper8, cpu_regs.hl);
}   //    0x36
void SCF(){
    set_flag(c);
}         //    0x37
void JR_C_r8(){
    if(is_flag_set(c)){
        cpu_regs.pc += (signed char) Oper8;
    }
}     //    0x38
void ADD_HL_SP(){
    cpu_regs.hl = add_2_byte(cpu_regs.hl, cpu_regs.sp);
}   //    0x39
void LD_A_HLdp(){
    cpu_regs.a = (bus_read(cpu_regs.hl));
    cpu_regs.hl --;
}   //    0x3a
void DEC_SP(){
    cpu_regs.sp --;
}      //    0x3b
void INC_A(){
    cpu_regs.a = inc(cpu_regs.a);
}       //    0x3c
void DEC_A(){
    cpu_regs.a = dec(cpu_regs.a);
}       //    0x3d
void LD_A_d8(){
    cpu_regs.a = Oper8;
}     //    0x3e
void CCF(){
    if(is_flag_set(c)){
        clear_flag(c);
    }
    else set_flag(c);
}         //    0x3f


void LD_B_B(){
    cpu_regs.b = cpu_regs.b;
}      //    0x40
void LD_B_C(){
    cpu_regs.b = cpu_regs.c;
}      //    0x41
void LD_B_D(){
    cpu_regs.b = cpu_regs.d;
}      //    0x42
void LD_B_E(){
    cpu_regs.b = cpu_regs.e;
}      //    0x43
void LD_B_H(){
    cpu_regs.b = cpu_regs.h;
}      //    0x44
void LD_B_L(){
    cpu_regs.b = cpu_regs.l;
}      //    0x45
void LD_B_HLp(){
    cpu_regs.b = bus_read(cpu_regs.hl);
}    //    0x46
void LD_B_A(){
    cpu_regs.b = cpu_regs.a;
}      //    0x47
void LD_C_B(){
    cpu_regs.c = cpu_regs.b;
}      //    0x48
void LD_C_C(){
    cpu_regs.c = cpu_regs.c;
}      //    0x49
void LD_C_D(){
    cpu_regs.c = cpu_regs.d;
}      //    0x4a
void LD_C_E(){
    cpu_regs.c = cpu_regs.e;
}      //    0x4b
void LD_C_H(){
    cpu_regs.c = cpu_regs.h;
}      //    0x4c
void LD_C_L(){
    cpu_regs.c = cpu_regs.l;
}      //    0x4d
void LD_C_HLp(){
    cpu_regs.c = bus_read(cpu_regs.hl);
}    //    0x4e
void LD_C_A(){
    cpu_regs.c = cpu_regs.a;
}      //    0x4f



void LD_D_B(){
    cpu_regs.d = cpu_regs.b;
}      //    0x40
void LD_D_C(){
    cpu_regs.d = cpu_regs.c;
}      //    0x41
void LD_D_D(){
    cpu_regs.d = cpu_regs.d;
}      //    0x42
void LD_D_E(){
    cpu_regs.d = cpu_regs.e;
}      //    0x43
void LD_D_H(){
    cpu_regs.d = cpu_regs.h;
}      //    0x44
void LD_D_L(){
    cpu_regs.d = cpu_regs.l;
}      //    0x45
void LD_D_HLp(){
    cpu_regs.d = bus_read(cpu_regs.hl);
}    //    0x46
void LD_D_A(){
    cpu_regs.d = cpu_regs.a;
}      //    0x47
void LD_E_B(){
    cpu_regs.e = cpu_regs.b;
}      //    0x48
void LD_E_C(){
    cpu_regs.e = cpu_regs.c;
}      //    0x49
void LD_E_D(){
    cpu_regs.e = cpu_regs.d;
}      //    0x4a
void LD_E_E(){
    cpu_regs.e = cpu_regs.e;
}      //    0x4b
void LD_E_H(){
    cpu_regs.e = cpu_regs.h;
}      //    0x4c
void LD_E_L(){
    cpu_regs.e = cpu_regs.l;
}      //    0x4d
void LD_E_HLp(){
    cpu_regs.e = bus_read(cpu_regs.hl);
}    //    0x4e
void LD_E_A(){
    cpu_regs.e = cpu_regs.a;
}      //    0x4f



void LD_H_B(){
    cpu_regs.h = cpu_regs.b;
}      //    0x40
void LD_H_C(){
    cpu_regs.h = cpu_regs.c;
}      //    0x41
void LD_H_D(){
    cpu_regs.h = cpu_regs.d;
}      //    0x42
void LD_H_E(){
    cpu_regs.h = cpu_regs.e;
}      //    0x43
void LD_H_H(){
    cpu_regs.h = cpu_regs.h;
}      //    0x44
void LD_H_L(){
    cpu_regs.h = cpu_regs.l;
}      //    0x45
void LD_H_HLp(){
    cpu_regs.h = bus_read(cpu_regs.hl);
}    //    0x46
void LD_H_A(){
    cpu_regs.h = cpu_regs.a;
}      //    0x47
void LD_L_B(){
    cpu_regs.l = cpu_regs.b;
}      //    0x48
void LD_L_C(){
    cpu_regs.l = cpu_regs.c;
}      //    0x49
void LD_L_D(){
    cpu_regs.l = cpu_regs.d;
}      //    0x4a
void LD_L_E(){
    cpu_regs.l = cpu_regs.e;
}      //    0x4b
void LD_L_H(){
    cpu_regs.l = cpu_regs.h;
}      //    0x4c
void LD_L_L(){
    cpu_regs.l = cpu_regs.l;
}      //    0x4d
void LD_L_HLp(){
    cpu_regs.l = bus_read(cpu_regs.hl);
}    //    0x4e
void LD_L_A(){
    cpu_regs.l = cpu_regs.a;
}      //    0x4f



void LD_HLp_B(){
    bus_write(cpu_regs.b, cpu_regs.hl);
}    //    0x70
void LD_HLp_C(){
    bus_write(cpu_regs.c, cpu_regs.hl);
}    //    0x71
void LD_HLp_D(){
    bus_write(cpu_regs.d, cpu_regs.hl);
}    //    0x72
void LD_HLp_E(){
    bus_write(cpu_regs.e, cpu_regs.hl);
}    //    0x73
void LD_HLp_H(){
    bus_write(cpu_regs.h, cpu_regs.hl);
}    //    0x74
void LD_HLp_L(){
    bus_write(cpu_regs.l, cpu_regs.hl);
}    //    0x75
void HALT(){}        //    0x76
void LD_HLp_A(){
    bus_write(cpu_regs.a, cpu_regs.hl);
}    //    0x77

void LD_A_B(){
    cpu_regs.a = cpu_regs.b;
}      //    0x78
void LD_A_C(){
    cpu_regs.a = cpu_regs.c;
}      //    0x79
void LD_A_D(){
    cpu_regs.a = cpu_regs.d;
}      //    0x7a
void LD_A_E(){
    cpu_regs.a = cpu_regs.e;
}      //    0x7b
void LD_A_H(){
    cpu_regs.a = cpu_regs.h;
}      //    0x7c
void LD_A_L(){
    cpu_regs.a = cpu_regs.l;
}      //    0x7d
void LD_A_HLp(){
    cpu_regs.a = bus_read(cpu_regs.hl);
}    //    0x7e
void LD_A_A(){
    cpu_regs.a = cpu_regs.a;
}      //    0x7f




void ADD_A_B(){

    add_byte(cpu_regs.b);

}     //    0x80
void ADD_A_C(){

    add_byte(cpu_regs.c);

}     //    0x81
void ADD_A_D(){

    add_byte(cpu_regs.d);

}     //    0x82
void ADD_A_E(){

    add_byte(cpu_regs.e);

}     //    0x83
void ADD_A_H(){

    add_byte(cpu_regs.h);

}     //    0x84
void ADD_A_L(){

    add_byte(cpu_regs.l);

}     //    0x85
void ADD_A_HLp(){

    add_byte(bus_read(cpu_regs.hl));

}   //    0x86
void ADD_A_A(){

    add_byte(cpu_regs.a);

}     //    0x87
void ADC_A_B(){

    adc(cpu_regs.b);

}     //    0x88
void ADC_A_C(){

    adc(cpu_regs.c);

}     //    0x89
void ADC_A_D(){

    adc(cpu_regs.d);

}     //    0x8a
void ADC_A_E(){

    adc(cpu_regs.e);

}     //    0x8b
void ADC_A_H(){

    adc(cpu_regs.h);

}     //    0x8c
void ADC_A_L(){

    adc(cpu_regs.l);

}     //    0x8d
void ADC_A_HLp(){

    adc(bus_read(cpu_regs.hl));

}   //    0x8e
void ADC_A_A(){

    adc(cpu_regs.a);

}     //    0x8f
void SUB_B(){

    sub_byte(cpu_regs.b);

}       //    0x90
void SUB_C(){

    sub_byte(cpu_regs.c);

}       //    0x91
void SUB_D(){

    sub_byte(cpu_regs.d);

}       //    0x92
void SUB_E(){

    sub_byte(cpu_regs.e);

}       //    0x93
void SUB_H(){

    sub_byte(cpu_regs.h);

}       //    0x94
void SUB_L(){

    sub_byte(cpu_regs.l);

}       //    0x95
void SUB_HLp(){

    sub_byte(bus_read(cpu_regs.hl));

}     //    0x96
void SUB_A(){

    sub_byte(cpu_regs.a);

}       //    0x97
void SBC_A_B(){

    Sbc(cpu_regs.b);

}     //    0x98
void SBC_A_C(){

    Sbc(cpu_regs.c);

}     //    0x99
void SBC_A_D(){

    Sbc(cpu_regs.d);

}     //    0x9a
void SBC_A_E(){

    Sbc(cpu_regs.e);

}     //    0x9b
void SBC_A_H(){

    Sbc(cpu_regs.h);

}     //    0x9c
void SBC_A_L(){

    Sbc(cpu_regs.l);

}     //    0x9d
void SBC_A_HLp(){

    Sbc(bus_read(cpu_regs.hl));

}   //    0x9e
void SBC_A_A(){

    Sbc(cpu_regs.a);

}     //    0x9f
void AND_B(){

    And(cpu_regs.b);

}       //    0xa0
void AND_C(){

    And(cpu_regs.c);    

}       //    0xa1
void AND_D(){

    And(cpu_regs.d);

}       //    0xa2
void AND_E(){

    And(cpu_regs.e);

}       //    0xa3
void AND_H(){

    And(cpu_regs.h);

}       //    0xa4
void AND_L(){

    And(cpu_regs.l);

}       //    0xa5
void AND_HLp(){

    And(bus_read(cpu_regs.hl));

}     //    0xa6
void AND_A(){

    And(cpu_regs.a);

}       //    0xa7
void XOR_B(){

    Xor(cpu_regs.b);

}       //    0xa8
void XOR_C(){

    Xor(cpu_regs.c);

}       //    0xa9
void XOR_D(){

    Xor(cpu_regs.d);

}       //    0xaa
void XOR_E(){

    Xor(cpu_regs.e);

}       //    0xab
void XOR_H(){

    Xor(cpu_regs.h);

}       //    0xac
void XOR_L(){

    Xor(cpu_regs.l);

}       //    0xad
void XOR_HLp(){

    Xor(bus_read(cpu_regs.hl));

}     //    0xae
void XOR_A(){

    Xor(cpu_regs.a);

}       //    0xaf
void OR_B(){

    Or(cpu_regs.b);

}        //    0xb0
void OR_C(){

    Or(cpu_regs.c);

}        //    0xb1
void OR_D(){

    Or(cpu_regs.d);

}        //    0xb2
void OR_E(){

    Or(cpu_regs.e);

}        //    0xb3
void OR_H(){

    Or(cpu_regs.h);

}        //    0xb4
void OR_L(){

    Or(cpu_regs.l);

}        //    0xb5
void OR_HLp(){

    Or(bus_read(cpu_regs.hl));

}      //    0xb6
void OR_A(){

    Or(cpu_regs.a);

}        //    0xb7
void CP_B(){

    cp(cpu_regs.b);

}        //    0xb8
void CP_C(){

    cp(cpu_regs.c);

}        //    0xb9
void CP_D(){

    cp(cpu_regs.d);

}        //    0xba
void CP_E(){

    cp(cpu_regs.e);

}        //    0xbb
void CP_H(){

    cp(cpu_regs.h);

}        //    0xbc
void CP_L(){

    cp(cpu_regs.l);

}        //    0xbd
void CP_HLp(){

    cp(bus_read(cpu_regs.hl));

}      //    0xbe
void CP_A(){

    cp(cpu_regs.a);

}        //    0xbf



void RET_NZ(){
    if(!is_flag_set(z))
    {
        cpu_regs.pc = Pop();
    }
}      //    0xc0
void POP_BC(){

    cpu_regs.bc = Pop();

}      //    0xc1
void JP_NZ_a16(){

    if(!is_flag_set(z)){
        cpu_regs.pc = Oper16;
    }

}   //    0xc2
void JP_a16(){

    cpu_regs.pc = Oper16;

}      //    0xc3
void CALL_NZ_a16(){

    if(!is_flag_set(z)){
        Push(cpu_regs.pc);
        cpu_regs.pc = Oper16;
    }

} //    0xc4
void PUSH_BC(){

    Push(cpu_regs.bc);

}     //    0xc5
void ADD_A_d8(){

    add_byte(Oper8);

}    //    0xc6
void RST_00H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0000;

}     //    0xc7
void RET_Z(){

    if(is_flag_set(z)){
        cpu_regs.pc = Pop();
    }

}       //    0xc8
void RET(){

    cpu_regs.pc = Pop();

}         //    0xc9
void JP_Z_a16(){

    if(is_flag_set(z)){
        cpu_regs.pc = Oper16;
    }

}    //    0xca
void PREFIX_CB(){

    printf("***NON IMPL CB***\n");

}   //    0xcb
void CALL_Z_a16(){

    if(is_flag_set(z)){
        Push(cpu_regs.pc);
        cpu_regs.pc = Oper16;
    }

}  //    0xcc
void CALL_a16(){

    Push(cpu_regs.pc);
    cpu_regs.pc = Oper16;

}    //    0xcd
void ADC_A_d8(){

    adc(Oper8);

}    //    0xce
void RST_08H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0008;

}     //    0xcf
void RET_NC(){

    if(!is_flag_set(c)){
        cpu_regs.pc = Pop();
    }

}      //    0xd0
void POP_DE(){

    cpu_regs.de = Pop();

}      //    0xd1
void JP_NC_a16(){

    if(!is_flag_set(c)){
        cpu_regs.pc = Oper16;
    }

}   //    0xd2
void CALL_NC_a16(){

    if(!is_flag_set(c)){
        Push(cpu_regs.pc);
        cpu_regs.pc = Oper16;
    }

} //    0xd4
void PUSH_DE(){

    Push(cpu_regs.de);

}     //    0xd5
void SUB_d8(){

    sub_byte(Oper8);

}      //    0xd6
void RST_10H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0010;

}     //    0xd7
void RET_C(){

    if(is_flag_set(c)){
        cpu_regs.pc = Pop();
    }

}       //    0xd8
void RETI(){

    cpu_regs.pc = Pop();
    interrupt_master_enable = 1;

}        //    0xd9
void JP_C_a16(){

    if(is_flag_set(c)){
        cpu_regs.pc = Oper16;
    }

}    //    0xda
void CALL_C_a16(){

    if(is_flag_set(c)){
        Push(cpu_regs.pc);
        cpu_regs.pc = Oper16;
    }

}  //    0xdc
void SBC_A_d8(){

    Sbc(Oper8);

}    //    0xde
void RST_18H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0018;

}     //    0xdf
void LDH_a8p_A(){
    //0xFF is the offset
    bus_write(cpu_regs.a, 0xFF00 + Oper8);

}   //    0xe0
void POP_HL(){

    cpu_regs.hl = Pop();

}      //    0xe1
void LD_Cp_A(){

    bus_write(cpu_regs.a, 0xFF00 + cpu_regs.c);

}     //    0xe2
void PUSH_HL(){

    Push(cpu_regs.hl);

}     //    0xe5
void AND_d8(){

    And(Oper8);

}      //    0xe6
void RST_20H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0020;

}     //    0xe7
void ADD_SP_r8(){
    //16 is the two's compliment of the 8 bit?
    add_2_byte(cpu_regs.sp, (u16)Oper8);

}   //    0xe8
void JP_HLp(){

    cpu_regs.pc = cpu_regs.hl;

}      //    0xe9
void LD_a16p_A(){

    bus_write(cpu_regs.a, Oper16);

}   //    0xea
void XOR_d8(){

    Xor(Oper8);

}      //    0xee
void RST_28H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0028;

}     //    0xef
void LDH_A_a8p(){

    cpu_regs.a = bus_read(0xFF00 + Oper8);

}   //    0xf0
void POP_AF(){

    cpu_regs.af = Pop();

}      //    0xf1
void LD_A_Cp(){

    cpu_regs.a = bus_read(0xFF00 + cpu_regs.c);

}     //    0xf2
void DI(){

    interrupt_master_enable = 0;

}          //    0xf3
void PUSH_AF(){

    Push(cpu_regs.af);

}     //    0xf5
void OR_d8(){

    Or(Oper8);

}       //    0xf6
void RST_30H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0030;

}     //    0xf7
void LD_HL_SPr8(){

    cpu_regs.hl = cpu_regs.sp + Oper8;

}  //    0xf8
void LD_SP_HL(){

    cpu_regs.sp = cpu_regs.hl;

}    //    0xf9
void LD_A_a16p(){

    cpu_regs.a = bus_read(Oper16);

}   //    0xfa
void EI(){

    interrupt_master_enable = 1;

}          //    0xfb
void CP_d8(){

    cp(Oper8);

}       //    0xfe
void RST_38H(){

    Push(cpu_regs.pc);
    cpu_regs.pc = 0x0038;

}     //    0xff
#pragma endregion

#pragma region Instructios_CB
// Declarations for Cb prefixed instructions.

void RLC_B(){

    cpu_regs.b = RotByteLeft(cpu_regs.b);

}     //    0x0
void RLC_C(){

    cpu_regs.c = RotByteLeft(cpu_regs.c);

}     //    0x1
void RLC_D(){

    cpu_regs.d = RotByteLeft(cpu_regs.d);

}     //    0x2
void RLC_E(){

    cpu_regs.e = RotByteLeft(cpu_regs.e);

}     //    0x3
void RLC_H(){

    cpu_regs.h = RotByteLeft(cpu_regs.h);

}     //    0x4
void RLC_L(){

    cpu_regs.l = RotByteLeft(cpu_regs.l);

}     //    0x5
void RLC_HLp(){

    bus_write(RotByteLeft(bus_read(cpu_regs.hl)), cpu_regs.hl);

}   //    0x6
void RLC_A(){

    cpu_regs.a = RotByteLeft(cpu_regs.a);

}     //    0x7
void RRC_B(){

    cpu_regs.b = RotByteRight(cpu_regs.b);

}     //    0x8
void RRC_C(){

    cpu_regs.c = RotByteRight(cpu_regs.c);

}     //    0x9
void RRC_D(){

    cpu_regs.d =RotByteRight(cpu_regs.d);

}     //    0xa
void RRC_E(){

    cpu_regs.e =RotByteRight(cpu_regs.e);

}     //    0xb
void RRC_H(){

    cpu_regs.h =RotByteRight(cpu_regs.h);

}     //    0xc
void RRC_L(){

    cpu_regs.l =RotByteRight(cpu_regs.l);

}     //    0xd
void RRC_HLp(){

    bus_write(RotByteRight(bus_read(cpu_regs.hl)), cpu_regs.hl);

}   //    0xe
void RRC_A(){

    cpu_regs.a = RotByteRight(cpu_regs.a);

}     //    0xf
void RL_B(){

    cpu_regs.b = Rotate_Left_Carry(cpu_regs.b);

}      //    0x10
void RL_C(){

    cpu_regs.c = Rotate_Left_Carry(cpu_regs.c);

}      //    0x11
void RL_D(){

    cpu_regs.d = Rotate_Left_Carry(cpu_regs.d);

}      //    0x12
void RL_E(){

    cpu_regs.e =Rotate_Left_Carry(cpu_regs.e);

}      //    0x13
void RL_H(){

    cpu_regs.h = Rotate_Left_Carry(cpu_regs.h);

}      //    0x14
void RL_L(){

    cpu_regs.l = Rotate_Left_Carry(cpu_regs.l);

}      //    0x15
void RL_HLp(){

    bus_write(Rotate_Left_Carry(bus_read(cpu_regs.hl)), cpu_regs.hl);

}    //    0x16
void RL_A(){

    cpu_regs.a = Rotate_Left_Carry(cpu_regs.a);

}      //    0x17
void RR_B(){

    cpu_regs.b = Rotate_Right_Carry(cpu_regs.b);

}      //    0x18
void RR_C(){

    cpu_regs.c = Rotate_Right_Carry(cpu_regs.c);

}      //    0x19
void RR_D(){

    cpu_regs.d = Rotate_Right_Carry(cpu_regs.d);

}      //    0x1a
void RR_E(){

    cpu_regs.e = Rotate_Right_Carry(cpu_regs.e);

}      //    0x1b
void RR_H(){

    cpu_regs.h = Rotate_Right_Carry(cpu_regs.h);

}      //    0x1c
void RR_L(){

    cpu_regs.l = Rotate_Right_Carry(cpu_regs.l);

}      //    0x1d
void RR_HLp(){

    bus_write(Rotate_Right_Carry(bus_read(cpu_regs.hl)), cpu_regs.hl);

}    //    0x1e
void RR_A(){

    cpu_regs.a = Rotate_Right_Carry(cpu_regs.a);

}      //    0x1f
void SLA_B(){

    cpu_regs.b = Shift_Left(cpu_regs.b);

}     //    0x20
void SLA_C(){

    cpu_regs.c = Shift_Left(cpu_regs.c);

}     //    0x21
void SLA_D(){

    cpu_regs.d = Shift_Left(cpu_regs.d);

}     //    0x22
void SLA_E(){

    cpu_regs.e = Shift_Left(cpu_regs.e);

}     //    0x23
void SLA_H(){

    cpu_regs.h = Shift_Left(cpu_regs.h);

}     //    0x24
void SLA_L(){

    cpu_regs.l = Shift_Left(cpu_regs.l);

}     //    0x25
void SLA_HLp(){

    bus_write(Shift_Left(bus_read(cpu_regs.hl)), cpu_regs.hl);

}   //    0x26
void SLA_A(){

    cpu_regs.a = Shift_Left(cpu_regs.a);

}     //    0x27
void SRA_B(){

    cpu_regs.b = Shift_Right_A(cpu_regs.b);

}     //    0x28
void SRA_C(){

    cpu_regs.c = Shift_Right_A(cpu_regs.c);

}     //    0x29
void SRA_D(){

    cpu_regs.d = Shift_Right_A(cpu_regs.d);

}     //    0x2a
void SRA_E(){

    cpu_regs.e = Shift_Right_A(cpu_regs.e);

}     //    0x2b
void SRA_H(){

    cpu_regs.h = Shift_Right_A(cpu_regs.h);

}     //    0x2c
void SRA_L(){

    cpu_regs.l = Shift_Right_A(cpu_regs.l);

}     //    0x2d
void SRA_HLp(){

    bus_write(Shift_Right_A(bus_read(cpu_regs.hl)), cpu_regs.hl);

}   //    0x2e
void SRA_A(){

    cpu_regs.a = Shift_Right_A(cpu_regs.a);

}     //    0x2f
void SWAP_B(){

    cpu_regs.b = Swap(cpu_regs.b);

}    //    0x30
void SWAP_C(){

    cpu_regs.c = Swap(cpu_regs.c);

}    //    0x31
void SWAP_D(){

    cpu_regs.d = Swap(cpu_regs.d);

}    //    0x32
void SWAP_E(){

    cpu_regs.e = Swap(cpu_regs.e);

}    //    0x33
void SWAP_H(){

    cpu_regs.h = Swap(cpu_regs.h);

}    //    0x34
void SWAP_L(){

    cpu_regs.l = Swap(cpu_regs.l);

}    //    0x35
void SWAP_HLp(){

    bus_write(Swap(bus_read(cpu_regs.hl)), cpu_regs.hl);

}  //    0x36
void SWAP_A(){

    cpu_regs.a = Swap(cpu_regs.a);

}    //    0x37
void SRL_B(){

    cpu_regs.b = Shift_Right(cpu_regs.b);

}     //    0x38
void SRL_C(){

    cpu_regs.c = Shift_Right(cpu_regs.c);

}     //    0x39
void SRL_D(){

    cpu_regs.d = Shift_Right(cpu_regs.d);

}     //    0x3a
void SRL_E(){

    cpu_regs.e = Shift_Right(cpu_regs.e);

}     //    0x3b
void SRL_H(){

    cpu_regs.h = Shift_Right(cpu_regs.h);

}     //    0x3c
void SRL_L(){

    cpu_regs.l = Shift_Right(cpu_regs.l);

}     //    0x3d
void SRL_HLp(){

    bus_write(bus_read(cpu_regs.hl), cpu_regs.hl);

}   //    0x3e
void SRL_A(){

    cpu_regs.a = Shift_Right(cpu_regs.a);

}     //    0x3f
void BIT_0_B(){

    Bit_Test_w_flags(0, cpu_regs.b);

}   //    0x40
void BIT_0_C(){

    Bit_Test_w_flags(0, cpu_regs.c);

}   //    0x41
void BIT_0_D(){

    Bit_Test_w_flags(0, cpu_regs.d);

}   //    0x42
void BIT_0_E(){

    Bit_Test_w_flags(0, cpu_regs.e);

}   //    0x43
void BIT_0_H(){

    Bit_Test_w_flags(0, cpu_regs.h);

}   //    0x44
void BIT_0_L(){

    Bit_Test_w_flags(0, cpu_regs.l);

}   //    0x45
void BIT_0_HLp(){

    Bit_Test_w_flags(0, bus_read(cpu_regs.hl));

} //    0x46
void BIT_0_A(){

    Bit_Test_w_flags(0, cpu_regs.a);

}   //    0x47
void BIT_1_B(){

    Bit_Test_w_flags(1, cpu_regs.b);

}   //    0x48
void BIT_1_C(){

    Bit_Test_w_flags(1, cpu_regs.c);

}   //    0x49
void BIT_1_D(){

    Bit_Test_w_flags(1, cpu_regs.d);

}   //    0x4a
void BIT_1_E(){

    Bit_Test_w_flags(1, cpu_regs.e);

}   //    0x4b
void BIT_1_H(){

    Bit_Test_w_flags(1, cpu_regs.e);

}   //    0x4c
void BIT_1_L(){

    Bit_Test_w_flags(1, cpu_regs.l);

}   //    0x4d
void BIT_1_HLp(){

    Bit_Test_w_flags(1, bus_read(cpu_regs.hl));

} //    0x4e
void BIT_1_A(){

    Bit_Test_w_flags(1, cpu_regs.a);

}   //    0x4f
void BIT_2_B(){

    Bit_Test_w_flags(2, cpu_regs.b);

}   //    0x50
void BIT_2_C(){

    Bit_Test_w_flags(2, cpu_regs.c);

}   //    0x51
void BIT_2_D(){

    Bit_Test_w_flags(2, cpu_regs.d);

}   //    0x52
void BIT_2_E(){

    Bit_Test_w_flags(2, cpu_regs.e);

}   //    0x53
void BIT_2_H(){

    Bit_Test_w_flags(2, cpu_regs.h);

}   //    0x54
void BIT_2_L(){

    Bit_Test_w_flags(2, cpu_regs.l);

}   //    0x55
void BIT_2_HLp(){

    Bit_Test_w_flags(2, bus_read(cpu_regs.hl));

} //    0x56
void BIT_2_A(){

    Bit_Test_w_flags(2, cpu_regs.a);

}   //    0x57
void BIT_3_B(){

    Bit_Test_w_flags(3, cpu_regs.b);

}   //    0x58
void BIT_3_C(){

    Bit_Test_w_flags(3, cpu_regs.c);

}   //    0x59
void BIT_3_D(){

    Bit_Test_w_flags(3, cpu_regs.d);
    
}   //    0x5a
void BIT_3_E(){

    Bit_Test_w_flags(3, cpu_regs.e);

}   //    0x5b
void BIT_3_H(){

    Bit_Test_w_flags(3, cpu_regs.h);

}   //    0x5c
void BIT_3_L(){

    Bit_Test_w_flags(3, cpu_regs.l);

}   //    0x5d
void BIT_3_HLp(){

    Bit_Test_w_flags(3, bus_read(cpu_regs.hl));

} //    0x5e
void BIT_3_A(){

    Bit_Test_w_flags(3, cpu_regs.a);

}   //    0x5f
void BIT_4_B(){

    Bit_Test_w_flags(4, cpu_regs.b);

}   //    0x60
void BIT_4_C(){

    Bit_Test_w_flags(4, cpu_regs.c);

}   //    0x61
void BIT_4_D(){

    Bit_Test_w_flags(4, cpu_regs.d);

}   //    0x62
void BIT_4_E(){

    Bit_Test_w_flags(4, cpu_regs.e);

}   //    0x63
void BIT_4_H(){

    Bit_Test_w_flags(4, cpu_regs.h);

}   //    0x64
void BIT_4_L(){

    Bit_Test_w_flags(4, cpu_regs.l);

}   //    0x65
void BIT_4_HLp(){

    Bit_Test_w_flags(4, bus_read(cpu_regs.hl));

} //    0x66
void BIT_4_A(){

    Bit_Test_w_flags(4, cpu_regs.a);

}   //    0x67
void BIT_5_B(){

    Bit_Test_w_flags(5, cpu_regs.b);

}   //    0x68
void BIT_5_C(){

    Bit_Test_w_flags(5, cpu_regs.c);

}   //    0x69
void BIT_5_D(){

    Bit_Test_w_flags(5, cpu_regs.d);

}   //    0x6a
void BIT_5_E(){

    Bit_Test_w_flags(5, cpu_regs.e);

}   //    0x6b
void BIT_5_H(){

    Bit_Test_w_flags(5, cpu_regs.h);

}   //    0x6c
void BIT_5_L(){

    Bit_Test_w_flags(5, cpu_regs.l);

}   //    0x6d
void BIT_5_HLp(){

    Bit_Test_w_flags(5, bus_read(cpu_regs.hl));

} //    0x6e
void BIT_5_A(){

    Bit_Test_w_flags(5, cpu_regs.a);

}   //    0x6f
void BIT_6_B(){

    Bit_Test_w_flags(6, cpu_regs.b);

}   //    0x70
void BIT_6_C(){

    Bit_Test_w_flags(6, cpu_regs.c);

}   //    0x71
void BIT_6_D(){

    Bit_Test_w_flags(6, cpu_regs.d);

}   //    0x72
void BIT_6_E(){

    Bit_Test_w_flags(6, cpu_regs.e);

}   //    0x73
void BIT_6_H(){

    Bit_Test_w_flags(6, cpu_regs.h);

}   //    0x74
void BIT_6_L(){

    Bit_Test_w_flags(6, cpu_regs.l);

}   //    0x75
void BIT_6_HLp(){

    Bit_Test_w_flags(6, bus_read(cpu_regs.hl));

} //    0x76
void BIT_6_A(){

    Bit_Test_w_flags(6, cpu_regs.a);

}   //    0x77
void BIT_7_B(){

    Bit_Test_w_flags(7, cpu_regs.b);

}   //    0x78
void BIT_7_C(){

    Bit_Test_w_flags(7, cpu_regs.c);

}   //    0x79
void BIT_7_D(){

    Bit_Test_w_flags(7, cpu_regs.d);

}   //    0x7a
void BIT_7_E(){

    Bit_Test_w_flags(7, cpu_regs.e);

}   //    0x7b
void BIT_7_H(){

    Bit_Test_w_flags(7, cpu_regs.h);

}   //    0x7c
void BIT_7_L(){

    Bit_Test_w_flags(7, cpu_regs.l);

}   //    0x7d
void BIT_7_HLp(){

    Bit_Test_w_flags(7, bus_read(cpu_regs.hl));

} //    0x7e
void BIT_7_A(){

    Bit_Test_w_flags(7, cpu_regs.a);

}   //    0x7f
void RES_0_B(){

    cpu_regs.b = Res(0, cpu_regs.b);

}   //    0x80
void RES_0_C(){

    cpu_regs.c = Res(0, cpu_regs.c);

}   //    0x81
void RES_0_D(){

    cpu_regs.d = Res(0, cpu_regs.d);

}   //    0x82
void RES_0_E(){

    cpu_regs.e = Res(0, cpu_regs.e);

}   //    0x83
void RES_0_H(){

    cpu_regs.h = Res(0, cpu_regs.h);

}   //    0x84
void RES_0_L(){

    cpu_regs.l = Res(0, cpu_regs.l);

}   //    0x85
void RES_0_HLp(){

    bus_write(Res(0, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0x86
void RES_0_A(){

    cpu_regs.a = Res(0, cpu_regs.a);

}   //    0x87
void RES_1_B(){

    cpu_regs.b = Res(1, cpu_regs.b);

}   //    0x88
void RES_1_C(){

    cpu_regs.c = Res(1, cpu_regs.c);

}   //    0x89
void RES_1_D(){

    cpu_regs.d = Res(1, cpu_regs.d);

}   //    0x8a
void RES_1_E(){

    cpu_regs.e = Res(1, cpu_regs.e);

}   //    0x8b
void RES_1_H(){

    cpu_regs.h = Res(1, cpu_regs.h);

}   //    0x8c
void RES_1_L(){

    cpu_regs.l = Res(1, cpu_regs.l);

}   //    0x8d
void RES_1_HLp(){

    bus_write(Res(1, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0x8e
void RES_1_A(){

    cpu_regs.a = Res(1, cpu_regs.a);

}   //    0x8f
void RES_2_B(){

    cpu_regs.b = Res(2, cpu_regs.b);

}   //    0x90
void RES_2_C(){

    cpu_regs.c = Res(2, cpu_regs.c);

}   //    0x91
void RES_2_D(){

    cpu_regs.d = Res(2, cpu_regs.d);

}   //    0x92
void RES_2_E(){

    cpu_regs.e = Res(2, cpu_regs.e);

}   //    0x93
void RES_2_H(){

    cpu_regs.h = Res(2, cpu_regs.h);

}   //    0x94
void RES_2_L(){

    cpu_regs.l = Res(2, cpu_regs.l);

}   //    0x95
void RES_2_HLp(){

    bus_write(Res(2,bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0x96
void RES_2_A(){

    cpu_regs.a = Res(2, cpu_regs.a);

}   //    0x97
void RES_3_B(){

    cpu_regs.b = Res(3, cpu_regs.b);

}   //    0x98
void RES_3_C(){

    cpu_regs.c = Res(3, cpu_regs.c);

}   //    0x99
void RES_3_D(){

    cpu_regs.d = Res(3, cpu_regs.d);

}   //    0x9a
void RES_3_E(){

    cpu_regs.e = Res(3, cpu_regs.e);

}   //    0x9b
void RES_3_H(){

    cpu_regs.h = Res(3, cpu_regs.h);

}   //    0x9c
void RES_3_L(){

    cpu_regs.l = Res(3, cpu_regs.l);

}   //    0x9d
void RES_3_HLp(){

    bus_write(Res(3, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0x9e
void RES_3_A(){

    cpu_regs.a = Res(3, cpu_regs.a);

}   //    0x9f
void RES_4_B(){

    cpu_regs.b = Res(4, cpu_regs.b);

}   //    0xa0
void RES_4_C(){

    cpu_regs.c = Res(4, cpu_regs.c);

}   //    0xa1
void RES_4_D(){

    cpu_regs.d = Res(4, cpu_regs.d);

}   //    0xa2
void RES_4_E(){

    cpu_regs.e = Res(4, cpu_regs.e);

}   //    0xa3
void RES_4_H(){

    cpu_regs.h = Res(4, cpu_regs.h);

}   //    0xa4
void RES_4_L(){

    cpu_regs.l = Res(4, cpu_regs.l);

}   //    0xa5
void RES_4_HLp(){

    bus_write(Res(4, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xa6
void RES_4_A(){

    cpu_regs.a = Res(4, cpu_regs.a);

}   //    0xa7
void RES_5_B(){

    cpu_regs.b = Res(5, cpu_regs.b);

}   //    0xa8
void RES_5_C(){

    cpu_regs.c = Res(5, cpu_regs.c);

}   //    0xa9
void RES_5_D(){

    cpu_regs.d = Res(5, cpu_regs.d);
    
}   //    0xaa
void RES_5_E(){

    cpu_regs.e = Res(5, cpu_regs.e);

}   //    0xab
void RES_5_H(){

    cpu_regs.h = Res(5, cpu_regs.h);

}   //    0xac
void RES_5_L(){

    cpu_regs.l = Res(5, cpu_regs.l);

}   //    0xad
void RES_5_HLp(){

    bus_write(Res(5, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xae
void RES_5_A(){

    cpu_regs.a = Res(5, cpu_regs.a);

}   //    0xaf
void RES_6_B(){

    cpu_regs.b = Res(6, cpu_regs.b);

}   //    0xb0
void RES_6_C(){

    cpu_regs.c = Res(6, cpu_regs.c);

}   //    0xb1
void RES_6_D(){

    cpu_regs.d = Res(6, cpu_regs.d);

}   //    0xb2
void RES_6_E(){

    cpu_regs.e = Res(6, cpu_regs.e);

}   //    0xb3
void RES_6_H(){

    cpu_regs.h = Res(6, cpu_regs.h);

}   //    0xb4
void RES_6_L(){

    cpu_regs.l = Res(6, cpu_regs.l);

}   //    0xb5
void RES_6_HLp(){

    bus_write(Res(6, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xb6
void RES_6_A(){

    cpu_regs.a = Res(6, cpu_regs.a);

}   //    0xb7
void RES_7_B(){

    cpu_regs.b = Res(7, cpu_regs.b);

}   //    0xb8
void RES_7_C(){

    cpu_regs.c = Res(7, cpu_regs.c);

}   //    0xb9
void RES_7_D(){

    cpu_regs.d = Res(7, cpu_regs.d);

}   //    0xba
void RES_7_E(){

    cpu_regs.e = Res(7, cpu_regs.e);

}   //    0xbb
void RES_7_H(){

    cpu_regs.h = Res(7, cpu_regs.h);

}   //    0xbc
void RES_7_L(){

    cpu_regs.l = Res(7, cpu_regs.l);

}   //    0xbd
void RES_7_HLp(){

    bus_write(Res(7, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xbe
void RES_7_A(){

    cpu_regs.a = Res(7, cpu_regs.a);

}   //    0xbf
void SET_0_B(){

    cpu_regs.b = Set(0, cpu_regs.b);

}   //    0xc0
void SET_0_C(){

    cpu_regs.c = Set(0, cpu_regs.c);

}   //    0xc1
void SET_0_D(){

    cpu_regs.d = Set(0, cpu_regs.d);

}   //    0xc2
void SET_0_E(){

    cpu_regs.e = Set(0, cpu_regs.e);

}   //    0xc3
void SET_0_H(){

    cpu_regs.h = Set(0, cpu_regs.h);

}   //    0xc4
void SET_0_L(){

    cpu_regs.l = Set(0, cpu_regs.l);

}   //    0xc5
void SET_0_HLp(){

    bus_write(Set(0, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xc6
void SET_0_A(){

    cpu_regs.a = Set(0, cpu_regs.a);

}   //    0xc7
void SET_1_B(){

    cpu_regs.b = Set(1, cpu_regs.b);

}   //    0xc8
void SET_1_C(){

    cpu_regs.c = Set(1, cpu_regs.c);

}   //    0xc9
void SET_1_D(){

    cpu_regs.d = Set(1, cpu_regs.d);

}   //    0xca
void SET_1_E(){

    cpu_regs.e = Set(1, cpu_regs.e);

}   //    0xcb
void SET_1_H(){

    cpu_regs.h = Set(1, cpu_regs.h);

}   //    0xcc
void SET_1_L(){

    cpu_regs.l = Set(1, cpu_regs.l);

}   //    0xcd
void SET_1_HLp(){

    bus_write(Set(1, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xce
void SET_1_A(){

    cpu_regs.a = Set(1, cpu_regs.a);

}   //    0xcf
void SET_2_B(){

    cpu_regs.b = Set(2, cpu_regs.b);

}   //    0xd0
void SET_2_C(){

    cpu_regs.c = Set(2, cpu_regs.c);

}   //    0xd1
void SET_2_D(){

    cpu_regs.d = Set(2, cpu_regs.d);

}   //    0xd2
void SET_2_E(){

    cpu_regs.e = Set(2, cpu_regs.e);

}   //    0xd3
void SET_2_H(){

    cpu_regs.h = Set(2, cpu_regs.h);

}   //    0xd4
void SET_2_L(){

    cpu_regs.l = Set(2, cpu_regs.l);

}   //    0xd5
void SET_2_HLp(){

    bus_write(Set(2, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xd6
void SET_2_A(){

    cpu_regs.a = Set(2, cpu_regs.a);

}   //    0xd7
void SET_3_B(){

    cpu_regs.b = Set(3, cpu_regs.b);

}   //    0xd8
void SET_3_C(){

    cpu_regs.c = Set(3, cpu_regs.c);

}   //    0xd9
void SET_3_D(){

    cpu_regs.d = Set(3, cpu_regs.d);

}   //    0xda
void SET_3_E(){

    cpu_regs.e = Set(3, cpu_regs.e);

}   //    0xdb
void SET_3_H(){

    cpu_regs.h = Set(3, cpu_regs.h);

}   //    0xdc
void SET_3_L(){

    cpu_regs.l = Set(3, cpu_regs.l);

}   //    0xdd
void SET_3_HLp(){

    bus_write(Set(3, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xde
void SET_3_A(){

    cpu_regs.a = Set(3, cpu_regs.a);

}   //    0xdf
void SET_4_B(){

    cpu_regs.b = Set(4, cpu_regs.b);

}   //    0xe0
void SET_4_C(){

    cpu_regs.c = Set(4, cpu_regs.c);

}   //    0xe1
void SET_4_D(){

    cpu_regs.d = Set(4, cpu_regs.d);

}   //    0xe2
void SET_4_E(){

    cpu_regs.e = Set(4, cpu_regs.e);

}   //    0xe3
void SET_4_H(){

    cpu_regs.h = Set(4, cpu_regs.h);

}   //    0xe4
void SET_4_L(){

    cpu_regs.l = Set(4, cpu_regs.l);

}   //    0xe5
void SET_4_HLp(){

    bus_write(Set(4, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xe6
void SET_4_A(){

    cpu_regs.a = Set(4, cpu_regs.a);

}   //    0xe7
void SET_5_B(){

    cpu_regs.b = Set(5, cpu_regs.b);

}   //    0xe8
void SET_5_C(){

    cpu_regs.c = Set(5, cpu_regs.c);

}   //    0xe9
void SET_5_D(){

    cpu_regs.d = Set(5, cpu_regs.d);

}   //    0xea
void SET_5_E(){

    cpu_regs.e = Set(5, cpu_regs.e);

}   //    0xeb
void SET_5_H(){

    cpu_regs.h = Set(5, cpu_regs.h);

}   //    0xec
void SET_5_L(){

    cpu_regs.l = Set(5, cpu_regs.l);

}   //    0xed
void SET_5_HLp(){

    bus_write(Set(5, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xee
void SET_5_A(){

    cpu_regs.a = Set(5, cpu_regs.a);

}   //    0xef
void SET_6_B(){

    cpu_regs.b = Set(6, cpu_regs.b);

}   //    0xf0
void SET_6_C(){

    cpu_regs.c = Set(6, cpu_regs.c);

}   //    0xf1
void SET_6_D(){

    cpu_regs.d = Set(6, cpu_regs.d);

}   //    0xf2
void SET_6_E(){

    cpu_regs.e = Set(6, cpu_regs.e);

}   //    0xf3
void SET_6_H(){

    cpu_regs.h = Set(6, cpu_regs.h);

}   //    0xf4
void SET_6_L(){

    cpu_regs.l = Set(6, cpu_regs.l);

}   //    0xf5
void SET_6_HLp(){

    bus_write(Set(6, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xf6
void SET_6_A(){

    cpu_regs.a = Set(6, cpu_regs.a);

}   //    0xf7
void SET_7_B(){

    cpu_regs.b = Set(7, cpu_regs.b);

}   //    0xf8
void SET_7_C(){

    cpu_regs.c = Set(7, cpu_regs.c);

}   //    0xf9
void SET_7_D(){

    cpu_regs.d = Set(7, cpu_regs.d);

}   //    0xfa
void SET_7_E(){

    cpu_regs.e = Set(7, cpu_regs.e);

}   //    0xfb
void SET_7_H(){

    cpu_regs.h = Set(7, cpu_regs.h);

}   //    0xfc
void SET_7_L(){

    cpu_regs.l = Set(7, cpu_regs.l);

}   //    0xfd
void SET_7_HLp(){

    bus_write(Set(7, bus_read(cpu_regs.hl)), cpu_regs.hl);

} //    0xfe
void SET_7_A(){

    cpu_regs.a = Set(7, cpu_regs.a);

}   //    0xff

#pragma endregion
