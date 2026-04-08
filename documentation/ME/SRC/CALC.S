/*
***************************THINGS TO REMEMBER TO DO:***************************
a beta site wants non decimal mode to trunc rather than round
*/
macro_file CALC;
/* ******************************************************************************
													 MULTI-EDIT MACRO FILE CALC
CALC - The calculator
PASTECALC - Pastes the current calculator value in at the current cursor position

							 (C) Copyright 1991 by American Cybernetics, Inc.
****************************************************************************** */
macro CALC {
/* *******************************MULTI-EDIT MACRO******************************

Name:  CALC

Description:  The pop-up calculator.

Parameters:
							/NT= If 1, disables the tape.
							/TL= Determines the tape length.  Defaults to 10.
							/STR= If 1, tells the calc to not actually invoke the calculator,
										but instead return a string representing a certain value
										passed to it in a certain base passed to it.  Primarily
										created for pasting in the calculator result to the current
										window.  The following parameters are only pertinent to the
										existance of this parameter:
										/RS= The string representation in decimal of the real value.
										/BASE= The desired base.
										/DPO= If 1, then there is a fraction behind the decimal
													point.  Only applicable if /BASE=10.
										/DPL= Amount of decimal places of accuracy desired.  Only
													applicable if /BASE=10 and /DPO=1.

							 (C) Copyright 1991 by American Cybernetics, Inc.
****************************************************************************** */

	int Word_Length,Base,T_Int,Decimal,Bin,Oct,Hex,Decimal_Places,Decimal_Point,
					Shift_Places,Function_Pending,Calc_X,Calc_Y,T_Explosions,Which_One,
					Word_Length_Mask,Visable_Tape_Length,Tape_On,
					Last_Key_Type,Actual_Tape_Length,Legal_Count,Error_Flag,
					Tape_Length,T_Flag,Ev_Count,Active_Window = Window_Id,
					Tape_Window = 0,t_insert_mode = insert_mode,
					t_shift_stat,
					orig_shift_stat,
					fc,
					t_box_count = box_count,
					No_Numlock = Global_Int("!CALC_NO_NUMLOCK");
					;
	real T_Real,Accumulator,Entry,Memory,Max_Pos,Min_Neg;
	str Numeric_Chars[16],T_Str,Func_Str1[3],Func_Str2[3],Disp_Str[39],
					T_Tape[60];
	char Input_Char,T_Char;

	set_global_int("MENU_LEVEL",global_int("MENU_LEVEL") + 1);

/*
Last_Key_Type is used as a flag to tell what was done before the present
keystroke.  Here are the legal values and their meanings:
0 = Clear
1 = Numeric input
2 = Operation
3 = Result
 */
/* These are variables used as constants */
	Refresh = False;
	Numeric_Chars = '0123456789ABCDEF';
	Max_Pos = 2147483647.0;
	Min_Neg = -2147483647.0;
/* Need to investigate if it is possible for INT_R to work with this value.
	Min_Neg = -2147483648.0;
*/
	Bin = 2;
	Oct = 8;
	Decimal = 10;
	Hex = 16;
	Ev_Count = 3;
/* Word_Length is a constant here.  I've allowed for the possibility of saving
into a global variable.  In this case, Word_Length_Mask must be calculated. */
	Word_Length = 32;
	if(  (Word_Length < 32)  ) {
		Word_Length_Mask = ($FFFFFFFF >> (32 - Word_Length));
	} else {
		Word_Length_Mask = $FFFFFFFF;
	}
	Set_Global_Str('Calc_Base_Display2',' Bin ');
	Set_Global_Str('Calc_Base_Display8',' Oct ');
	Set_Global_Str('Calc_Base_Display10',' Dec ');
	Set_Global_Str('Calc_Base_Display16',' Hex ');

/* initialize calculator variables according to Calc_Params and check and correct
for anything out of range */
	Tape_On = (Parse_Int('/NT=',MParm_Str) == 0);
	Tape_Length = Parse_Int('/TL=',MParm_Str);
	if(  (Tape_Length == 0)  ) {
		Tape_Length = 20;
	}
	Calc_X = Parse_Int('/X=',Global_Str('Calc_Params'));
	Calc_Y = Parse_Int('/Y=',Global_Str('Calc_Params'));
	if(  (Calc_X < 1)  ) {
		Calc_X = 20;
	}
	if(  (Calc_Y == 0)  ) {
		Calc_Y = Min_Window_Row + 12;
	}
	Return_Int = Min_Window_Row + 2;
	if(  (Calc_Y < Return_Int)  ) {
		Calc_Y = Return_Int;
	}
	Return_Int = Screen_Width - 46;
	if(  (Calc_X > Return_Int)  ) {
		Calc_X = Return_Int;
	}
	Return_Int = Max_Window_Row - 11;
	if(  (Calc_Y > Return_Int)  ) {
		Calc_Y = Return_Int;
	}
	Which_One = Parse_Int('/WO=',Global_Str('Calc_Params'));
	if(  ((Which_One > 1) || (Which_One < 0))  ) {
		Which_One = 0;
	}
	Last_Key_Type = Parse_Int('/LK=',Global_Str('Calc_Params'));
	if(  ((Last_Key_Type > 3) || (Last_Key_Type < 0))  ) {
		Last_Key_Type = 0;
	}
	Decimal_Places = Parse_Int('/DPL=',Global_Str('Calc_Params'));
	if(  ((Decimal_Places > 10) || (Decimal_Places < 0))  ) {
		Decimal_Places = 0;
	}
	Decimal_Point = Parse_Int('/DPO=',Global_Str('Calc_Params'));
	if(  ((Decimal_Point > 1) || (Decimal_Point < 0))  ) {
		Decimal_Point = 0;
	}
	Func_Str1 = Parse_Str('/FS1=',Global_Str('Calc_Params'));
	Func_Str2 = Parse_Str('/FS2=',Global_Str('Calc_Params'));
	Function_Pending = Parse_Int('/FP=',Global_Str('Calc_Params'));
	if(  ((Function_Pending > 1) || (Function_Pending < 0))  ) {
		Function_Pending = 0;
	}
	if(  (RVal(Memory,Parse_Str('/MEM=',Global_Str('Calc_Params'))))  ) {
		Memory = 0.0;
	}
	if(  (RVal(Accumulator,Parse_Str('/ACC=',Global_Str('Calc_Params'))))  ) {
		Accumulator = 0.0;
	}
	if(  (RVal(Entry,Parse_Str('/ENT=',Global_Str('Calc_Params'))))  ) {
		Entry = 0.0;
	}
	Base = Parse_Int('/BASE=',Global_Str('Calc_Params'));
	if(  (XPos(' ' + Str(Base) + ' ',' 2 8 10 16 ',1) == 0)  ) {
		Base = 10;
	}

/* This is the part for paste in of calculator result.  Actually, it is more
generic than that.  You actually pass as parameters the string representation
of the real value and the base, and it will return a string based on that.  So
it does not neccesarily have to be what's in the calculator. */
	if(  (Parse_Int('/STR=',MParm_Str))  ) {
		if(  (RVal(T_Real,Parse_Str('/RS=',MParm_Str)))  ) {
			T_Real = 0.0;
		}
		Base = Parse_Int('/BASE=',MParm_Str);
		if(  (XPos(' ' + Str(Base) + ' ',' 2 8 10 16 ',1) == 0)  ) {
			Base = 10;
		}
		Decimal_Places = Parse_Int('/DPL=',MParm_Str);
		if(  ((Decimal_Places > 10) | (Decimal_Places < 0))  ) {
			Decimal_Places = 0;
		}
		Decimal_Point = Parse_Int('/DPO=',MParm_Str);
		if(  ((Decimal_Point > 1) | (Decimal_Point < 0))  ) {
			Decimal_Point = 0;
		}
		Call PASTE_IN;
		Goto SPECIAL_EXIT;
	}

	Create_Window;
/*
	cb_t_color = m_b_color;
	cb_s_color = m_b_color;
*/
	Tape_Window = Window_Id;
	actual_tape_length = tape_length;
	while (actual_tape_length > 0) {
		t_tape = Global_Str("Calc_Tape" + Str(actual_tape_length));
		if (svl(t_tape)) {
			Put_Line(t_tape);
			Set_Global_Str("Calc_Tape" + Str(actual_tape_length),"");
			down;
		}
		--actual_tape_Length;
	}
	call PAD_TAPE;

/* Save some stuff into variables so we can restore them later */
	T_Explosions = Explosions;

	Push_Labels;
	Flabel('Help  ',1,All);
	Flabel('Clear ',2,All);
	Flabel('ClrEnt',3,All);
	Flabel('And   ',4,All);
	Flabel('Or    ',5,All);
	Flabel('Xor   ',6,All);
	Flabel('Base  ',7,All);
	Flabel('+/-   ',8,All);
	Flabel('Memory',9,All);
	Flabel('Paste',12,All);
	Flabel('ClrTap',13,All);
	Call MAKE_FACEPLATE;

	Explosions = False;
	Visable_Tape_Length = 0;
	Call SAVE_SHIFT_STAT;
	orig_shift_stat = t_shift_stat;
	Call NUM_LOCK_ON;

	if(  (Which_One == 1)  ) {
		T_Real = Accumulator;
	} else {
		T_Real = Entry;
	}
/*
	Save_Box(Calc_X,Calc_Y - 2,Calc_X + 44,Calc_Y - 1);
*/
	Call DISPLAY_TAPE;
	if(  (Function_Pending)  ) {
		if(  (Which_One)  ) {
			call WRITE_FUNC_STR;
		}
		if(  ((Last_Key_Type == 2) & (Base == Decimal))  ) {
/* In this instance, we need to display what was in the accumulator, but we
don't know how what the decimal point parameters are.  We need to create
the display string ourselves. */
			Disp_Str = Parse_Str('/ACC=',Global_Str('Calc_Params'));
			Error_Flag = Xpos('.',Disp_Str,1);
			if(  (Error_Flag)  ) {
				while(  (Str_Char(Disp_Str,Svl(Disp_Str)) == '0')  ) {
					Disp_Str = Str_Del(Disp_Str,Svl(Disp_Str),1);
				}
				if(  (Str_Char(Disp_Str,Svl(Disp_Str)) == '.')  ) {
					Disp_Str = Str_Del(Disp_Str,Svl(Disp_Str),1);
				}
			}
			Call PAD_DISPLAY;
			Call WRITE_TO_DISPLAY;
			T_Real = 0.0;
			Decimal_Point = 0;
			Decimal_Places = 0;
			Goto GET_KEY;
		}
		Call MAKE_DISPLAY;
		Call WRITE_TO_DISPLAY;
		T_Real = 0.0;
		Goto GET_KEY;
	}

DISPLAY_AND_GET_KEY:
	Call MAKE_DISPLAY;
	Call WRITE_TO_DISPLAY;
GET_KEY:
/*
Call DISPLAY_VARS;
 */
	Read_Key;
	if(  (Key1 == 0)  ) {
		if(  (Key2 == 250)  ) {
			RM('USERIN^CHECKEVENTS /M=1/G=CALCEV/#=' + Str(Ev_Count));

			if(  (Return_Int)  ) {

				RM('USERIN^CHECKEVENTS /M=2/G=CALCEV/#=' + Str(Ev_Count));
				Return_Int = Parse_Int('/R=',Return_Str);
				if(  (Return_Int > 127)  ) {
					Key2 = Return_Int - 128;
					Goto MOUSE_KEY2;
				} else {
					Key1 = Return_Int;
					Goto MOUSE_KEY1;
				}
			}
/* Mouse event */
/*
ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄCalculatorÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ·
³É Dec <F7>ÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¸º±±
³º                                      0  º±±
³º                                      0  ±º±±
³º                                      0  ±º±±
³º                                      0  º±±
³º                                      0  ³º±±
³ÓÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙº±±
³ And<F4>   A   B    7   8   9     Clr<F2>  º±±
³ Or <F5>   C   D    4   5   6     *   /    º±±
³ Xor<F6>   E   F    1   2   3     +   -    º±±
³ Mem<F9>   CE<F3>   0   .  ñ<F8>  =<ENTER> º±±
ÔÍClrTape<ShftF3>ÍÍDone<ESC>ÍÍPaste<ShftF2>Í¼±±
	±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±
ÚÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ·
ÃÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄCalculatorÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ¶
³É Dec <F7>ÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¸º±±
³º                                      0³º±±
³ÓÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙº±±
³And<F4>   A   B    7   8   9     Clr<F2> º±±
³Or <F5>   C   D    4   5   6     *   /   º±±
³Xor<F6>   E   F    1   2   3     +   -   º±±
³Mem<F9>   CE<F3>   0   .  ñ<F8>  =<ENTER>º±±
ÔClrTape<ShftF3>ÍÍDone<ESC>ÍÍPaste<ShftF2>¼±±
	±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±±
 */
			if(  (Mou_Last_Y == (Calc_Y + 4))  ) {
/* First row of "buttons"
And<F4>   A   B    7   8   9     Clr<F2>
 */
				if(  ((Mou_Last_X > (Calc_X + 1)) & (Mou_Last_X < (Calc_X + 9)))  ) {
					Key2 = 62;
					Goto MOUSE_KEY2;
				}
				if(  (Mou_Last_X == (Calc_X + 12))  ) {
					Key1 = 65;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 16))  ) {
					Key1 = 66;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 21))  ) {
					Key1 = 55;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 25))  ) {
					Key1 = 56;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 29))  ) {
					Key1 = 57;
					Goto MOUSE_KEY1;
				}
				if(  ((Mou_Last_X > (Calc_X + 34)) & (Mou_Last_X < (Calc_X + 42)))  ) {
					Key2 = 60;
					Goto MOUSE_KEY2;
				}
			}
			if(  (Mou_Last_Y == (Calc_Y + 5))  ) {
/* Second row of "buttons"
Or <F5>   C   D    4   5   6     *   /
 */
				if(  ((Mou_Last_X > (Calc_X + 1)) & (Mou_Last_X < (Calc_X + 9)))  ) {
					Key2 = 63;
					Goto MOUSE_KEY2;
				}
				if(  (Mou_Last_X == (Calc_X + 12))  ) {
					Key1 = 67;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 16))  ) {
					Key1 = 68;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 21))  ) {
					Key1 = 52;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 25))  ) {
					Key1 = 53;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 29))  ) {
					Key1 = 54;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 35))  ) {
					Key1 = 42;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 39))  ) {
					Key1 = 47;
					Goto MOUSE_KEY1;
				}
			}
			if(  (Mou_Last_Y == (Calc_Y + 6))  ) {
/* Third row of "buttons"
Xor<F6>   E   F    1   2   3     +   -
 */
				if(  ((Mou_Last_X > (Calc_X + 1)) & (Mou_Last_X < (Calc_X + 9)))  ) {
					Key2 = 64;
					Goto MOUSE_KEY2;
				}
				if(  (Mou_Last_X == (Calc_X + 12))  ) {
					Key1 = 69;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 16))  ) {
					Key1 = 70;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 21))  ) {
					Key1 = 49;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 25))  ) {
					Key1 = 50;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 29))  ) {
					Key1 = 51;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 35))  ) {
					Key1 = 43;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 39))  ) {
					Key1 = 45;
					Goto MOUSE_KEY1;
				}
			}
			if(  (Mou_Last_Y == (Calc_Y + 7))  ) {
/* Fourth row of "buttons"
Mem<F9>   CE<F3>   0   .  ñ<F8>  =<ENTER>
 */
				if(  ((Mou_Last_X > (Calc_X + 1)) & (Mou_Last_X < (Calc_X + 9)))  ) {
					Key2 = 67;
					Goto MOUSE_KEY2;
				}
				if(  ((Mou_Last_X > (Calc_X + 11)) & (Mou_Last_X < (Calc_X + 18)))  ) {
					Key2 = 61;
					Goto MOUSE_KEY2;
				}
				if(  (Mou_Last_X == (Calc_X + 21))  ) {
					Key1 = 48;
					Goto MOUSE_KEY1;
				}
				if(  (Mou_Last_X == (Calc_X + 25))  ) {
					Key1 = 46;
					Goto MOUSE_KEY1;
				}
				if(  ((Mou_Last_X > (Calc_X + 27)) & (Mou_Last_X < (Calc_X + 33)))  ) {
					Key2 = 66;
					Goto MOUSE_KEY2;
				}
				if(  ((Mou_Last_X > (Calc_X + 34)) & (Mou_Last_X < (Calc_X + 43)))  ) {
					Key1 = 13;
					Goto MOUSE_KEY1;
				}
			}
			if(  (Mou_Last_Y == Fkey_Row)  ) {
				RM('USERIN^MOUSEFKEY');
				Goto GET_KEY;
			}

			if(  (Mou_Last_Y == (Calc_Y - 3))  ) {
				if(  ((Mou_Last_X > (Calc_X + 19)) &  (Mou_Last_X < (Calc_X + 23)))  ) {
					if(  (Memory != 0.0)  ) {
						Key2 = 67;
						Goto MOUSE_KEY2;
					}
				}
				if(  ((Mou_Last_X > (Calc_X + 2)) & (Mou_Last_X < (Calc_X + 11)))  ) {
					Key2 = 65;
					Goto MOUSE_KEY2;
				}
			}

			if (((Mou_Last_Y < (Calc_Y + 2)) &&
					(Mou_Last_Y > (Calc_Y - 3))) &&
					((Mou_Last_X > (Calc_X + 1)) &&
					(Mou_Last_X < (Calc_X + 44)))) {
/*
Tape menu
*/
				Push_Key(key1,key2);
				Key2 = 72;
				Goto MOUSE_KEY2;
			}

			if (Mou_Last_Y == (Calc_Y - 4)) {
				if(  ((Mou_Last_X >= Calc_X) && (Mou_Last_X < (Calc_X + 43)))  ) {
					Call MOUSE_MOVE;
					Goto DISPLAY_AND_GET_KEY;
				}
			}
/* if mouse was clicked outside the boundry of the calculator, exit */
			if(  ((Mou_Last_X < Calc_X) || (Mou_Last_X > (Calc_X + 46)) ||
					(Mou_Last_Y < (Calc_Y - 4)) ||
					(Mou_Last_Y > (Calc_Y + 11)))  ) {
				Push_Key(Key1,Key2);
				Goto GO_BYE_BYE;
			}
			Goto GET_KEY;
		}
MOUSE_KEY2:
		if(  (Key2 == 244)  ) {
			Goto GO_BYE_BYE;
		}
		if(  (Key2 == 86)  ) {
			Call CLEAR_TAPE;
			Goto GET_KEY;
		}
		if(  (Key2 == 85)  ) {
//			Kill_Box;
//			Kill_Box;
			Return_Int = 1;
			Goto EXIT;
		}
		if(  (Key2 == 59)  ) {
			Help('CA');
			Goto GET_KEY;
		}
		if(  (Key2 == 60)  ) {
			Call CLEAR;
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (Key2 == 61)  ) {
			Entry = 0.0;
			T_Real = 0.0;
			Decimal_Places = 0;
			Decimal_Point = False;
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (Key2 == 62)  ) {
			Func_Str2 = 'And';
			Input_Char = 'A';
			Goto MATH_OPERATION;
		}
		if(  (Key2 == 63)  ) {
			Func_Str2 = 'Or ';
			Input_Char = 'O';
			Goto MATH_OPERATION;
		}
		if(  (Key2 == 64)  ) {
			Func_Str2 = 'Xor';
			Input_Char = 'X';
			Goto MATH_OPERATION;
		}
		if (Key2 == 65) {
			if (Base == Bin) {
				Base = Oct;
			} else if (Base == Oct) {
				Base = Decimal;
			} else if (Base == Decimal) {
				Base = Hex;
			} else if (Base == Hex) {
				Base = Bin;
			}
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (Key2 == 66)  ) {
			Entry = 0.0 - Entry;
			T_Real = Entry;
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (Key2 == 67)  ) {
			Call MEMORY_MENU;
			Goto DISPLAY_AND_GET_KEY;
		}
		if (Key2 == 72) {
/* Up cursor key  gets into tape */
			Call TAPE_MENU;
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (Key2 == 238)  ) {
			Call KEY_MOVE;
			Goto DISPLAY_AND_GET_KEY;
		}
	} else {
MOUSE_KEY1:
		if(  (Key1 == 27)  ) {
GO_BYE_BYE:
//			Kill_Box;
//			Kill_Box;
			Return_Int = 0;
			Goto EXIT;
		}
		Input_Char = Caps(Char(Key1));
		if(  (Base == Decimal)  ) {
			if(  ((Input_Char == '.') & ((Decimal_Point == False) | (Last_Key_Type == 3)))  ) {
				Decimal_Point = True;
				Goto NUMERIC_CHAR;
			}
		}
		if(  (XPos(Input_Char,Copy(Numeric_Chars,1,Base) + '|8',1))  ) {
NUMERIC_CHAR:
			Func_Str2 = '';
/* Be sure that we are not about to exceed the amount of significant digits */
			if(  ((Input_Char != '|8') && (Last_Key_Type == 1))  ) {
				Call CHECK_WORD_LENGTH;
				if(  (Return_Int)  ) {
					Goto GET_KEY;
				}
			}
			if(  (Last_Key_Type != 1)  ) {
				Entry = 0.0;
				T_Real = 0.0;
				if(  (Input_Char != '.')  ) {
					Decimal_Point = 0;
				}
				Decimal_Places = 0;
				Disp_Str = '0';
			}
			call CLEAR_FUNC_STR;
			Which_One = 0;
			Call INPUT_NUMERIC;
			if(  (Error_Flag)  ) {
				Goto ERROR;
			}
			Entry = T_Real;
			Last_Key_Type = 1;
			Goto DISPLAY_AND_GET_KEY;
		}
		if(  (XPos(Input_Char,'+-*/',1))  ) {
				Func_Str2 = ' ' + Input_Char + ' ';
MATH_OPERATION:
			T_Real = Entry;
			Return_Str = Input_Char;
			Call PUSH_TAPE;
			if(  (Function_Pending)  ) {
				T_Real = Accumulator;
				Call DO_FUNCTION;
				if(  (Error_Flag)  ) {
					Goto ERROR;
				}
				Accumulator = T_Real;
				Call MAKE_DISPLAY;
				Call WRITE_TO_DISPLAY;
			} else {
				Function_Pending = True;
				Accumulator = Entry;
			}
			Which_One = 1;
			Decimal_Point = False;
			Decimal_Places = 0;
			Func_Str1 = Func_Str2;
			Entry = 0.0;
			T_Real = 0.0;
			call WRITE_FUNC_STR;
			Last_Key_Type = 2;
			Goto GET_KEY;
		}
		if(  (XPos(Input_Char,'=|13',1))  ) {
			if(  (Func_Str2 != '')  ) {
				call CLEAR_FUNC_STR;
				Entry = Accumulator;
			}
			Which_One = 1;
			Return_Str = '=';
			Call PUSH_TAPE;

			if(  ((Func_Str1 != '') & (Function_Pending == True))  ) {

				T_Real = Accumulator;
				Function_Pending = False;
				Call DO_FUNCTION;
				if(  (Error_Flag)  ) {
					Goto ERROR;
				}

			} else {
				T_Real = Entry;
			}

			Accumulator = T_Real;
			Entry = T_Real;
			Call MAKE_DISPLAY;
			Return_Str = ' ';
			Call PUSH_TAPE;
			Return_Str = 'Ä';
			Call PUSH_TAPE;
			Func_Str2 = ' = ';
			Last_Key_Type = 3;
			Goto DISPLAY_AND_GET_KEY;

		}
	}
	Goto GET_KEY;

ERROR:
	Write('               Error!  Press any key...',Calc_X + 2,Calc_Y + 2,0,M_S_Color);
	Read_Key;
	Call CLEAR;
	Goto DISPLAY_AND_GET_KEY;

/* ********************************** SUBROUTINES ****************************** */
KEY_MOVE:
	Return_Int = 1;
	Goto CALC_MOVE;
MOUSE_MOVE:
	Return_Int = 0;
CALC_MOVE:
	RM("WINDOW^MOVE_WIN /M=2/X1=" + str(calc_x) +
			"/Y1=" + str(calc_y - 4) +
			"/X2=" + str(calc_x + 44) +
			"/Y2=" + str(calc_y + 10) +
			"/MX1=1/MY1=" + str(min_window_row) +
			"/MX2=" + str(screen_width) +
			"/MY2=" + Str(Max_window_row - 2) +
			"/K=" + Str(Return_Int));
	calc_x = parse_int('/X1=',return_str);
	calc_y = parse_int('/Y1=',return_str) + 4;
	if(  (virtual_display == 0)  ) {
		kill_box;
	}
	override_screen_seg;
	Call MAKE_FACEPLATE;
	Call DISPLAY_TAPE;
	reset_screen_seg;
	update_virtual_display;
	reset_virtual_display;
	RET;

NUM_LOCK_ON:
	if (!No_Numlock)
		Poke(0,$417,Peek(0,$417) | $20);
	RET;

NUM_LOCK_OFF:
	if (!No_Numlock)
		Poke(0,$417,Peek(0,$417) & $DF);
	RET;

SAVE_SHIFT_STAT:
	t_shift_stat = peek(0,$417) & 0x20;
	RET;

RESTORE_SHIFT_STAT:
	poke(0,$417,(peek(0,0x417) & 0xdf) | t_shift_stat);
	RET;


CHECK_WORD_LENGTH:
	if(  (Base == Decimal)  ) {
		T_Real = Entry;
		Call GET_DECIMAL_PLACES;
		Return_Str = RStr(Entry,0,Return_Int);
		Return_Int = ((Length(Return_Str) - (XPos('.',Return_Str,1) > 0) -
			(Entry < 1.0)) > 9);
	} else {
		Return_Int = (Copy(Remove_Space(Disp_Str),1,1) != '0');
	}
	RET;

TAPE_MENU:
	if (cur_char == '|0') {
		rm("MEERROR^BEEPS /C=1");
		check_key;
		RET;
	}
	if (xpos('|254',get_line,1)) {
// Adjust if last line is not a valid choice
		up;
	}
	return_int = base;
	return_str = disp_str;
	Call STRING_TO_REAL;
	Call SAVE_SHIFT_STAT;
	Call NUM_LOCK_OFF;



	Draw_Char(32,Calc_X + 1,Calc_Y + 8,M_B_Color,43);
	Draw_Char(32,Calc_X + 1,Calc_Y + 9,M_B_Color,43);
	Set_Global_Str('CAIPARM_1', '/TP=15/W=42/HT=5/C=1/L=0/DC=1/NCWA=1/WIN=' +
			str(cur_window));
// This is just to move the buttons to the bottom of the calc box
	Set_Global_Str("CAIPARM_2", "/TP=10/W=0/C=1/L=9");
	l_color = m_h_color;

	RM("USERIN^DATA_IN /NB=1/A=2/X=" + str(calc_x) + "/Y=" + str(calc_y - 3) +
			 "/H=CA/#=2/PRE=CA/NM=1");


	Draw_Char(32,Calc_X + 1,Calc_Y + 8,M_B_Color,43);
	Draw_Char(32,Calc_X + 1,Calc_Y + 9,M_B_Color,43);

	if(  (Return_Int == 1)  ) {
/* Turn the string into a real value, then stuff it into the entry */
		Return_Str = Remove_Space(Copy(get_line,1,39));
		T_Int = Parse_Int('/B=',get_line);
		if(  (T_Int == Bin)  ) {
			Call STRIP_SPACES;
		}
		Return_Int = T_Int;
		if(  (Base == Decimal)  ) {
			Decimal_Point = XPos('.',Return_Str,1);
			if(  (Decimal_Point)  ) {
				Decimal_Places = Length(Return_Str) - Decimal_Point;
				Decimal_Point = True;
			}
		}
		Call STRING_TO_REAL;
		Entry = T_Real;
		Which_One = 0;
		Func_Str2 = '';
	}

	Call FILL_FACEPLATE;
	if((Which_One) && (function_pending)) {
		call WRITE_FUNC_STR;
	}
	Call RESTORE_SHIFT_STAT;
	Call DISPLAY_TAPE;
	RET;

/*
PROCESS_TAPE:
	RM("USERIN^DI_LIST_FIELD /DC=1/TP=15/HT=5/W=42/WIN=" + Str(Cur_Window) +
			"/#=" + Str(Actual_Tape_Length) +
			"/FC=" + Str(fc) +
			"/X=" + Str(win_x1) +
			"/Y=" + Str(win_y1) +
			"/M=" + Str(Return_Int)
			);
	RET;
*/

CLEAR_TAPE:
	Tof;
	while (not(at_eof)) {
		del_line;
	}
	call PAD_TAPE;
	Call DISPLAY_TAPE;
	RET;

PUSH_TAPE:
	mark_pos;
	actual_tape_length = c_line;
	tof;
	if ((actual_tape_length >= tape_length) || (cur_char == '|0')) {
		del_line;
	}
	goto_mark;
	goto_col(1);
	if ((c_line > 1) || (at_eof == false)) {
		down;
	}
	if(  (Return_Str == 'C')  ) {
		t_tape = '                                      0   ';
	} else if(  (Return_Str == 'Ä')  ) {
		t_tape = 'ÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄ|254';
	} else {
		 t_tape = Disp_Str + " " + Return_Str + " ";
	}
	put_line(t_tape + '/B=' + Str(base));

	Call DISPLAY_TAPE;
	RET;

PAD_TAPE:
	eof;
	if (c_col == 1) {
		up;
	}
	actual_tape_length = c_line;
	tof;
	while (actual_tape_length < 5) {
		insert_mode = true;
		cr;
		up;
		put_line('|0                                        |254');
		++actual_tape_length;
	}
	eof;
	if (c_col == 1) {
		up;
	}
	goto_col(1);
	RET;

DISPLAY_TAPE:
	size_window(calc_x + 1,calc_y - 3,calc_x + 43,calc_y + 2);
	window_attr = $16;
	t_color = m_b_color;
	eof_color = (t_color & $F0) | ((t_color & $70) >> 4);
	l_color = t_color;
	c_color = t_color;
	s_color = cb_s_color;
	b_color = t_color;
	h_color = M_h_color;
	Eof;
	if (c_col == 1) {
		up;
	}
	down;
	goto_line(c_line - 1);
	goto_col(1);
	refresh = true;
	redraw;
	refresh = false;
	Call POSITION_CURSOR;
	RET;

MEMORY_MENU:
	Write('MEM:',Calc_X + 2,Calc_Y - 3,0,M_T_Color);
	Set_Global_Str('MEMEV1','/T=MIn/KC=<ENTER>/W=10/K1=13/K2=28/R=13/Y=' +
			Str(Calc_Y - 3) + '/X=' + Str(Calc_X + 7));
	Set_Global_Str('MEMEV2','/T=M+/KC=<+>/W=5/K1=43/K2=78/R=43/Y=' +
			Str(Calc_Y - 3) + '/X=' + Str(Calc_X + 18));
	Set_Global_Str('MEMEV3','/T=MRec/KC=<R>/W=7/K1=82/K2=19/R=82/Y=' +
			Str(Calc_Y - 3) + '/X=' + Str(Calc_X + 24));
	Set_Global_Str('MEMEV4','/T=Exit/KC=<ESC>/W=9/K1=27/K2=1/R=27/Y=' +
			Str(Calc_Y - 3) + '/X=' + Str(Calc_X + 32));
	RM('USERIN^CHECKEVENTS /M=2/G=MEMEV/#=4');

	Read_Key;
	if(  ((Key1 == 0) & (key2 == 250))  ) {
		RM('USERIN^CHECKEVENTS /M=1/G=MEMEV/#=4');
		if(  (Return_Int)  ) {
			Key1 = Parse_Int('/R=',Global_Str('MEMEV' + Str(Return_Int)));
		} else {
			Push_Key(Key1,Key2);
			Goto MEMORY_EXIT;
		}
	}
	if(  (Key1 == 27)  ) {
		Goto MEMORY_EXIT;
	}
	if(  (Key1 == 13)  ) {
		if(  (Which_One == 1)  ) {
			Memory = Accumulator;
		} else {
			Memory = Entry;
		}
		Goto MEMORY_EXIT;
	}
	if(  (Key1 == 43)  ) {
		if(  (Which_One == 1)  ) {
			Memory = Memory + Accumulator;
		} else {
			Memory = Memory + Entry;
		}
		Goto MEMORY_EXIT;
	}
	if(  ((Key1 == 114) | (Key1 == 82))  ) {
		Entry = Memory;
		T_Real = Memory;
		Which_One = 0;
		Func_Str2 = '';
		call CLEAR_FUNC_STR;
		Goto MEMORY_EXIT;
	}
	Goto MEMORY_MENU;

MEMORY_EXIT:
	Write('<F7>ÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ',Calc_X + 7,Calc_Y - 3,0,M_B_Color);
	RET;

MAKE_FACEPLATE:
	set_virtual_display;
	Put_Box(Calc_X,Calc_Y - 4,Calc_X + 46,Calc_Y + 11,0,M_B_Color,'Calculator',True);
	call FILL_FACEPLATE;
	update_virtual_display;
	reset_virtual_display;
	RET;

FILL_FACEPLATE:
	Write('É     <F7> ÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍÍ¸',Calc_X + 1,Calc_Y - 3,0,M_B_Color);
	Write('º',Calc_X + 1,Calc_Y - 2,0,M_B_Color);
	Write('º',Calc_X + 1,Calc_Y - 1,0,M_B_Color);
	Write('º',Calc_X + 1,Calc_Y    ,0,M_B_Color);
	Write('º',Calc_X + 1,Calc_Y + 1,0,M_B_Color);
	Write('º                                         ³',Calc_X + 1,Calc_Y + 2,0,M_B_Color);
	Write('ÓÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÄÙ',Calc_X + 1,Calc_Y + 3,0,M_B_Color);
	Write('And<F4>   A   B    7   8   9     Clr<F2>',Calc_X + 2,Calc_Y + 4,0,M_B_Color);
	Write('Or <F5>   C   D    4   5   6     *   /',Calc_X + 2,Calc_Y + 5,0,M_B_Color);
	Write('Xor<F6>   E   F    1   2   3     +   -',Calc_X + 2,Calc_Y + 6,0,M_B_Color);
	Write('Mem<F9>   CE<F3>   0   .  ñ<F8>  =<ENTER>',Calc_X + 2,Calc_Y + 7,0,M_B_Color);

	Set_Global_Str('CALCEV1','/3D=1/T=Done/KC=<ESC>/W=9/K1=27/K2=1/R=27/Y=' +
		Str(Calc_Y + 8) + '/X=' + Str(Calc_X + 19));
	Set_Global_Str('CALCEV2','/3D=1/T=ClrTape/KC=<ShftF3>/W=15/K1=0/K2=86/R=214/Y=' +
		Str(Calc_Y + 8) + '/X=' + Str(Calc_X + 2));
	Set_Global_Str('CALCEV3','/3D=1/T=Paste/KC=<ShftF2>/W=13/K1=0/K2=85/R=213/Y=' +
		Str(Calc_Y + 8) + '/X=' + Str(Calc_X + 30));

	RM('USERIN^CHECKEVENTS /M=2/G=CALCEV/#=' + Str(Ev_Count));
	RET;

CLEAR_FACEPLATE:
	Draw_Char(32,Calc_X + 1,Calc_Y + 4,M_B_Color,43);
	Draw_Char(32,Calc_X + 1,Calc_Y + 5,M_B_Color,43);
	Draw_Char(32,Calc_X + 1,Calc_Y + 6,M_B_Color,43);
	Draw_Char(32,Calc_X + 1,Calc_Y + 7,M_B_Color,43);
	Draw_Char(205,Calc_X + 1,Calc_Y + 8,M_B_Color,43);
	RET;

WRITE_TO_DISPLAY:
	Write(Copy('ÍÍÍ M ',((Memory != 0.0) * 3) + 1,3),Calc_X + 20,Calc_Y - 3,0,
					M_B_Color);
	Write(Global_Str('Calc_Base_Display' + Str(Base)),Calc_X + 2,Calc_Y - 3,0,
				M_B_Color);
	Write(Disp_Str,Calc_X + 2,Calc_Y + 2,0,M_S_Color);
POSITION_CURSOR:
	GotoXY(Calc_X + 40,Calc_Y + 2);
	RET;

CLEAR:
	Which_One = 0;
	Decimal_Places = 0;
	Decimal_Point = False;
	Accumulator = 0.0;
	Entry = 0.0;
	T_Real = 0.0;
	Func_Str1 = '';
	Func_Str2 = '';
	Last_Key_Type = 0;
	Function_Pending = False;
	call CLEAR_FUNC_STR;
	Return_Str = 'C';
	Call PUSH_TAPE;
	RET;

DO_FUNCTION:
	Error_Flag = False;
	if(  (Func_Str1 == ' + ')  ) {
		T_Real = T_Real + Entry;
		if(  (T_Real > Max_Pos)  ) {
			Error_Flag = True;
		}
	}
	if(  (Func_Str1 == ' - ')  ) {
		T_Real = T_Real - Entry;
		if(  (T_Real < Min_Neg)  ) {
			Error_Flag = True;
		}
	}
	if(  (Func_Str1 == ' * ')  ) {
		T_Real = T_Real * Entry;
		if(  (T_Real > Max_Pos)  ) {
			Error_Flag = True;
		}
	}
	if(  (Func_Str1 == ' / ')  ) {
		if(  (Entry == 0.0)  ) {
			Error_Flag = True;
			Ret;
		}
		T_Real = T_Real / Entry;
	}
/* if we are doing And Or Xor, we need to convert to integer type first,
do the operation, then convert back to real. */
	if(  (Func_Str1 == 'And')  ) {
		T_Real = Real_I(Int_R(T_Real) & Int_R(Entry));
	}
	if(  (Func_Str1 == 'Or ')  ) {
		T_Real = Real_I(Int_R(T_Real) | Int_R(Entry));
	}
	if(  (Func_Str1 == 'Xor')  ) {
		T_Real = Real_I(Int_R(T_Real) ^ Int_R(Entry));
	}
	if(  ((Base == Decimal) & (Error_Flag == False))  ) {
/* Adjust Decimal_Places */
		Call GET_DECIMAL_PLACES;
		Decimal_Places = Return_Int;
	}
	RET;

GET_DECIMAL_PLACES:
	Return_Str = RStr(T_Real,0,10);
	Return_Str = Copy(Return_Str,XPos('.',Return_Str,1),11);
	T_Int = 11;
	while(  (Copy(Return_Str,T_Int,1) == '0')  ) {
		--T_Int;
	}
	--T_Int;
	Return_Int = T_Int;
	RET;

STRIP_SPACES:
	Return_Int = XPos(' ',Return_Str,1);
	if(  (Return_Int)  ) {
		Return_Str = Str_Del(Return_Str,Return_Int,1);
		Goto STRIP_SPACES;
	}
	RET;

INPUT_NUMERIC:
	Call MAKE_DISPLAY;
	Return_Str = Remove_Space(Disp_Str);
	if(  (Base == Bin)  ) {
		Call STRIP_SPACES;
	}
	if(  (Base == Decimal)  ) {
		if(  ((Decimal_Point == True) & (Input_Char != '.'))  ) {
			if(  (Input_Char == '|8')  ) {
				if(  (Decimal_Places == 0)  ) {
					Decimal_Point = False;
					Goto DELETE_DEC;
				}
				--Decimal_Places;
				Goto DELETE_DEC;
			} else {
				++Decimal_Places;
			}
		} else {
			if(  (Input_Char == '|8')  ) {
DELETE_DEC:
				Return_Str = Copy(Return_Str,1,Length(Return_Str) - 1);
			}
		}
	} else {
		if(  (Input_Char == '|8')  ) {
			Return_Str = Copy('0' + Return_Str,1,Length(Return_Str));
		} else {
			Return_Str = Copy(Return_Str,2,31);
		}
	}
	if(  (XPos(Input_Char,'.|8',1) == 0)  ) {
		Return_Str = Return_Str + Input_Char;
	}
/* now, convert it back to a real */
	Return_Int = Base;
/* We are storing Base into Return_Int just so we can accomodate a call to the
following label */
STRING_TO_REAL:
	if(  (Return_Int == Decimal)  ) {
		Return_Int = RVal(T_Real,Return_Str);
	} else {
		T_Real = 0.0;
		T_Int = Length(Return_Str);
		while(  (T_Int > 0)  ) {
			T_Real = T_Real + Real_I((XPos(Copy(Return_Str,Length(Return_Str) - T_Int
					+ 1,1),'0123456789ABCDEF',1) - 1) << (Shift_Places * (T_Int - 1)));
			--T_Int;
		}
	}

	if(  ((T_Real > Max_Pos) | (T_Real < Min_Neg))  ) {
		Error_Flag = True;
	} else {
		Error_Flag = False;
	}
	RET;

MAKE_DISPLAY:
/* This routine will change T_Real into a string representation according to the
base */
	if(  (Base == Decimal)  ) {
		Disp_Str = RStr(T_Real,0,Decimal_Places);
		if(  ((Decimal_Point == True) & (Decimal_Places == 0))  ) {
			Disp_Str = Disp_Str + '.';
		}
	} else {
		Return_Int = Int_R(T_Real) & Word_Length_Mask;
		Disp_Str = '';
		if(  (Base == Bin)  ) {
			Shift_Places = 1;
		}
		if(  (Base == Oct)  ) {
			Shift_Places = 3;
		}
		if(  (Base == Hex)  ) {

			Shift_Places = 4;
		}

		while(  (Return_Int != 0)  ) {
			Disp_Str = Copy('0123456789ABCDEF',((Return_Int & (Base - 1)) + 1),1) + Disp_Str;
			Return_Int = Return_Int >> Shift_Places;
		}
		Return_Int = Svl(Disp_Str);
		if(  (Return_Int < (Word_Length / Shift_Places))  ) {
			Disp_Str = Copy('00000000000000000000000000000000',1,(Word_Length / Shift_Places) - Return_Int) + Disp_Str;
		}
	}
	if(  (Base == Bin)  ) {
/* Put in spaces every 4 bits for clarity */
		T_Int = (Word_Length / 4) - 1;
		while(  (T_Int)  ) {
			Disp_Str = Str_Ins(' ',Disp_Str,(T_Int * 4) + 1);
			--T_Int;
		}
	}
PAD_DISPLAY:
	Disp_Str = Copy('                                       ',1,39 - (Svl(Disp_Str))) + Disp_Str;
	RET;

PASTE_IN:
	Call MAKE_DISPLAY;
	Return_Str = Remove_Space(Disp_Str);
	RET;

WRITE_FUNC_STR:
	Write(Func_Str1,Calc_X + 39,Calc_Y + 3,0,M_T_Color);
	RET;

CLEAR_FUNC_STR:
	Write("ÄÄÄ",Calc_X + 39,Calc_Y + 3,0,M_T_Color);
	RET;

/* ***************************************************************************** */

EXIT:
	if(  (Switch_Win_Id(Tape_Window))  ) {
		eof;
		if (c_col > 1) {
			down;
		}
		goto_col(1);
		tape_length = 0;
		while (c_line > 1) {
			up;
			if (cur_char != '|0') {
				++tape_Length;
				Set_Global_Str("Calc_Tape" + Str(tape_length),get_line);
			}
		}
		Delete_Window;
	}
	Switch_Win_Id(Active_Window);
/* Save all pertainant stuff into a global */

	Set_Global_Str('Calc_Params',
		'/X=' + Str(Calc_X) +
		'/Y=' + Str(Calc_Y) +
		'/WO=' + Str(Which_One) +
		'/LK=' + Str(Last_Key_Type) +
		'/BASE=' + Str(Base) +
		'/DPL=' + Str(Decimal_Places) +
		'/DPO=' + Str(Decimal_Point) +
		'/FS1=' + Func_Str1 +
		'/FS2=' + Func_Str2 +
		'/FP=' + Str(Function_Pending) +
		'/MEM=' + RStr(Memory,10,10) +
		'/ACC=' + RStr(Accumulator,10,10) +
		'/ENT=' + RStr(Entry,10,10)
	);
/* Restore all altered system vars, etc. */
	RM('USERIN^CHECKEVENTS /M=3/G=CALCEV/#=' + Str(Ev_Count));
	RM('USERIN^CHECKEVENTS /M=3/G=MEMEV/#=4');
	Set_Global_Str('CALCEV1','');
	Set_Global_Str('Calc_Base_Display2','');
	Set_Global_Str('Calc_Base_Display8','');
	Set_Global_Str('Calc_Base_Display10','');
	Set_Global_Str('Calc_Base_Display16','');
	t_shift_stat = orig_shift_stat;
	call RESTORE_SHIFT_STAT;
	Explosions = T_Explosions;
/*
	m_s_color = t_m_s_color;
	m_h_color = t_m_h_color;
	cb_t_color = t_cb_t_color;
	cb_s_color = t_cb_s_color;
*/
	Pop_Labels;
	if(  (Return_Int)  ) {
/* If they want to paste in the result, do it */
		Call PASTE_IN;
		Text(Return_Str);
	}


SPECIAL_EXIT:
	Return_Int = 1;
	set_global_int("MENU_LEVEL",global_int("MENU_LEVEL") - 1);
	insert_mode = t_insert_mode;
	while (box_count > t_box_count) {
		kill_box;
	}
}

macro PASTE_CALC {
/* *******************************MULTI-EDIT MACRO******************************

Name:  PASTE_CALC

Description:  Will place the value last displayed on the calculator at the
							current cursor position.

							 (C) Copyright 1991 by American Cybernetics, Inc.
****************************************************************************** */

/* Determine if the value last displayed is an entry or a result, then use the
appropriate value */
	if(  (Parse_Int('/WO=',Global_Str('Calc_Params')) == 1)  ) {
		Return_Str = Parse_Str('/ACC=',Global_Str('Calc_Params'));
	} else {
		Return_Str = Parse_Str('/ENT=',Global_Str('Calc_Params'));
	}
	RM('CALC /STR=1/RS=' + Return_Str + '/BASE=' +
		Parse_Str('/BASE=',Global_Str('Calc_Params')) + '/DPL=' +
		Parse_Str('/DPL=',Global_Str('Calc_Params')) + '/DPO=' +
		Parse_Str('/DPO=',Global_Str('Calc_Params')));

	Text(Return_Str);
}
