macro_file DOCUMENT;
/*******************************************************************************
													 MULTI-EDIT MACRO FILE DOCUMENT
FPRINT					- The print formatter
FCMD						- Top level menu for inserting print formatter
F_CODE_SUBMENU	- Sub level menu for inserting print formatter codes
INSERT_F_CODES	- Inserts codes for the print formatter
INDXMAC					- Converts a single line Column or Stream block into an index
									entry

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

macro FPRINT TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name: FPRINT

Description: This macro is a pretty printer utility which decodes special
						 formatting chars to perform word processing formatting.

Current Formatting Commands:

Command    Default   Function

.pb n      n=+1      Page break(start page number n)
The form feed character(ASCII 12) will also perform a page break.  The
difference is there is no page number parameter.  Instead, there is an optional
parameter that will force the next printed page to be even or odd.  If it is a
1 then it will be odd.  If 2, it will be even.  If omitted or anything else, it
will paginate normally.  A closely related command is the ".bp".

.ff n      n=0       Use form feed characters at the end of each page. 0 = off
										 anything else = on.
.bp n      n=0       Put headers an footers on blank pages due to forced even/
										 odd parameter after form feed page break.  0 means no, 1
										 means yes.
.ce                  Center this line
.df n str  Empty     Define footer. if n = 2 then footer will be footer 2
.dh n str  Empty     Define header. if n = 2 then header will be header 2
.sf                  Supress footer this page
.sh                  Supress header this page
.ls n      n=1       Line spacing is n
.pl n      n=66      Set page length to n
.rm n      n=80      Set right margin to n
.lm n      n=1       Set left margin to n
.ig                  Ignore, do not decode this line.
.tm n      n=5       Set top margin to n
.bm n      n=60      Set bottom margin to n
.hm n      n=2       Set header margin to n
.fm n      n=64      Set footer margin to n
.np                  Don't print this line

User definable formatting commands:
These are "macro" type formatting commands which the user can define.  Since
they are processed in a "search and replace" type manner, they can be very
versitile.  In defining, everything to the right of the command is considered
to be part of the definition.  Whenever a mactching execute command is
encountered, the command is replaced by the definition string.  Anything(normal
text, other commands, etc.)  will be processed as though it were imbedded at
the point of the execution command.  The value of n can be from 1 to 10.
One caution:  If the insertion of the definition string results in a string
length which exeeds 255, the definition string will be truncated until it fits,
yielding unpredictable results.

.du n      n=1       Define user formatting command n
.ud n      n=1       Execute user formmating command n

Printer specific formatting commands:
These commands are used in conjuction with the global variable "Printer_Type".
These codes are set up via the Modify Printer Codes option in either the Print
macro or the Install macro.  Typical examples would be:
.ub       Underline begin
.ue				Underline end

These correspond to printer specific control codes set up by the user.  Any code
in the text which matches a code set up for the current printer type will be
replaced by that code.  Since these are completely user definable, it is the
responsibility of the user to put the correct codes as defined in his printer
type, and to insure there are no conflicts between user defined printer codes
and fprint's formatting codes.  As it stands right now, fprint's codes will
take precedence.

Current Formatting variables:

Code      Name

.fn       File name
.ti       Time
.da       Date
.pa       Page number

Table of contents generator:
To create a table of contents, the user must define a table of contents, then
add table entry codes.

.tc       Define table of contents. At least 2 parameters are expected, and up
					to 12 will be recognized.  The first parameter will determine the
					right margin for all levels.  The second will determine whether or
					not to pad the void between each table entry and the corresponding
					page number with spaces or with periods(.).  The third will determine
					the amount of indenting for the first level.  The fourth determines
					the format of the first level.  The 3rd and 4th are repeated for each
					of the 5 possible levels, and are read by the program as pairs.

0 = plain               EXAMPLE:  COMPILING A MACRO                        10
1 = numeric             EXAMPLE:  1. COMPILING A MACRO                     10
2 = alpha               EXAMPLE:  A. COMPILING A MACRO                     10
3 = roman numeral       EXAMPLE:  I. COMPILING A MACRO                     10

.te       One parameter is expected, which is the table level of the current
					Table entry. Everything on this line beginning with the first word
					after the parameter and up to the delimiter "(" or more than one
					space will be considered part of the table entry.

															INDEX GENERATOR
We have started an index generator.  Although too primitive to mention in the
manual, it does have the ability to create an unformatted index.  The index
generator is activated automatically by the first occurance of an index
definition:

.X[heading^sub heading]

Whatever is inside the [] will appear in the index.  The entire definition will
be stripped from the body of the document.  Subheadings are optional, but there
must be a main heading before the caret^.  The format of the generated index
is:

heading (page number)
				subheading1 (page number)
				subheading2 (page number)

The index is sorted in alphabetically by heading and by subheading.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

	str Line_In[2048],Line_Out,Command[2],Commands[80],Header1,Header2,
					Footer1,Footer2,Cur_Time[7],Cur_Date[8],Cur_File_Name[40],Blank_Line,
					Temp_String,Temp_Line_Out,Table_Line,Table_Page_Str[4],
					Temp_Param[10],Printer_Type[25],Table_Delimits[40],Index_Main[80],
					Index_Sub[80],Percentage[4];

	char Code_Delimiter;

	int End_Of_File,Cur_Page_Num,Text_Length,Val_Error,Param_Space,
					Page_Length,Cur_Line_Num,Top_Margin,Bottom_Margin,Left_Margin,
					Rght_Margin,Header_Margin,Footer_Margin,Comment,Supress_Header,
					Supress_Footer,Page_Break,Line_Spacing,New_Page_Num,Param_Pos,
					Delimiter_Pos,Temp_Integer,Text_Window,Print_Window,Move_Count,
					Making_Margin,Print_HF,Table_Window,Table_Style,Table_Indent,
					Table_Level,Table_Count,Table_Margin,Table_Count1,Table_Indent1,
					Table_Style1,Table_Count2,Table_Indent2,Table_Style2,Table_Count3,
					Table_Indent3,Table_Style3,Table_Count4,Table_Indent4,Table_Style4,
					Table_Count5,Table_Indent5,Table_Style5,Table_Defined,Periods,
					Table_Line_Num,Table_Page_Num,Abort,Todds_Jx,Todds_Jx2,Todds_Jx3,
					Todds_Jx4,HF_Toggle,For_Table,Parameter,Default,Temp_Flag,
					Leave_Blank,Blank_Page,Jx,Jy,Temp_Undo,Form_Feed,T_Reg_Exp_Stat,
					T_Insert_Mode,Index_Window,Footer_Set,Bottom_Set,Header_Copied,
					Footer_Copied,Supress_Table_Header,Supress_Table_Footer,F_Mode,
					Attribute_Count = 0;

	Temp_Undo = Undo_Stat;
	Undo_Stat = False;
	T_Reg_Exp_Stat = Reg_Exp_Stat;
	Reg_Exp_Stat = True;
	T_Insert_Mode = Insert_Mode;
	Insert_Mode = True;
	Refresh = False;
	Messages = False;
	Abort = False;
	F_Mode = Parse_Int('/M=',MParm_Str);
	Code_Delimiter = Copy(Global_Str('Format_Code_Delimit') + '.',1,1);

	Table_Delimits = '!@#^&()[]`"~\|<>{}*';
	Jx = XPos(Code_Delimiter,Table_Delimits,1);
	if(  (Jx)  ) {
		Table_Delimits = Str_Del(Table_Delimits,Jx,1);
	}
	Working;


	Blank_Line = '                                                                                ';
	Printer_Type = Global_Str('PRINTER_TYPE');
	Put_Box(2,4,61,10,0,M_B_Color,'PRINT FORMATTER',True);
	Set_Global_Str('@PFEV#1','/T=Cancel/KC=<ESC>/K1=27/K2=1/R=1/W=11/X=25/Y=9');
	RM('CheckEvents /M=2/G=@PFEV#/#=1');
	if(  (Printer_Type == '')  ) {
		Write('Warning!  Printer type is unspecified.',4,5,0,M_B_Color);
	} else {
		Write('Printer type: '  + Printer_Type,4,5,0,M_B_Color);
	}
	Write('Format code delimiter: '  + Code_Delimiter,4,6,0,M_B_Color);
	Write('Formatting: '  + Truncate_Path(File_Name) + ' for printing.  ',4,7,0,M_B_Color);
	Write('같같같같같같같같같같같같같같같같같같같같   0% Complete.',4,8,0,M_B_Color);

	EOF;
/* Store last line of the file into a variable */
	End_Of_File = C_LINE;
	TOF;
/* Initialize Variables to defaults */
	Header1 = '';
	Header2 = '';
	Footer1 = '';
	Footer2 = '';
	HF_Toggle = True;
	Cur_Page_Num = 1;
	Cur_Line_Num = 1;
	Page_Length = 66;
	Left_Margin = 0;
	Rght_Margin = 80;
	Top_Margin = 5;
	Bottom_Margin = 60;
	Header_Margin = 2;
	Footer_Margin = 64;
	Footer_Set = False;
	Bottom_Set = False;
	Line_Spacing = 1;
	Supress_Header = False;
	Supress_Footer = False;
	Page_Break = False;
	Cur_File_Name = Caps(File_Name);
	Cur_Date = Date;
	Cur_Time = Copy(Time,1,5) + Copy(Time,9,2);
	Making_Margin = False;
/* Defaults for table of contents */
	Table_Defined = False;
	Table_Margin = 75;
	Periods = False;
	Table_Count1 = 0;
	Table_Count2 = 0;
	Table_Count3 = 0;
	Table_Count4 = 0;
	Table_Count5 = 0;
	Table_Indent1 = 0;
	Table_Indent2 = 5;
	Table_Indent3 = 10;
	Table_Indent4 = 15;
	Table_Indent5 = 20;
	Table_Style1 = 0;
	Table_Style2 = 0;
	Table_Style3 = 0;
	Table_Style4 = 0;
	Table_Style5 = 0;
	For_Table = False;
	Leave_Blank = 0;
	Blank_Page = 0;
	Form_Feed = 0;
	Print_HF = False;
	Supress_Table_Header = False;
	Supress_Table_Footer = False;

/* This is a global you would initialize in STARTUP if you wanted different
defaults */
	if(  (Global_Str('@FPRINT_DEFAULT@') != '')  ) {
		Line_In = Global_Str('@FPRINT_DEFAULT@');
		if(  (XPos('/MODE=',Line_In,1))  ) {
			F_MODE = Parse_Int('/MODE=',Line_In);
		}
		if(  (XPos('/PL=',Line_In,1))  ) {
			Page_Length = Parse_Int('/PL=',Line_In);
		}
		if(  (XPos('/LM=',Line_In,1))  ) {
			Left_Margin = Parse_Int('/LM=',Line_In);
		}
		if(  (XPos('/RM=',Line_In,1))  ) {
			Rght_Margin = Parse_Int('/RM=',Line_In);
		}
		if(  (XPos('/TM=',Line_In,1))  ) {
			Top_Margin = Parse_Int('/TM=',Line_In);
		}
		if(  (XPos('/BM=',Line_In,1))  ) {
			Bottom_Margin = Parse_Int('/BM=',Line_In);
		}
		if(  (XPos('/HM=',Line_In,1))  ) {
			Header_Margin = Parse_Int('/HM=',Line_In);
		}
		if(  (XPos('/FM=',Line_In,1))  ) {
			Footer_Margin = Parse_Int('/FM=',Line_In);
		}
		if(  (XPos('/LS=',Line_In,1))  ) {
			Line_Spacing = Parse_Int('/LS=',Line_In);
		}
		if(  (XPos('/FF=',Line_In,1))  ) {
			Form_Feed = Parse_Int('/FF=',Line_In);
		}
		if(  (XPos('/DH1=',Line_In,1))  ) {
			Header1 = Parse_Str('/DH1=',Line_In);
			Header2 = Header1;
		}
		if(  (XPos('/DF1=',Line_In,1))  ) {
			Footer1 = Parse_Str('/DF1=',Line_In);
			Footer2 = Footer1;
		}
	}

/* The ascii 127 char delimiting the commands is there to prevent false
interpretation of commands */
	if(  (F_MODE)  ) {
		Commands = '';
	} else {
		Commands = 'PBDFDHSHSFLSPLRMLMNPTMBMHMFMTCTEIGBPDUUDFFX[';
	}
	Text_Window = Window_Id;
	Temp_String = Caps(Truncate_Extension(File_Name));
	Line_In = Format_Line;

/* Create a window for the printable file if one does not already exist */
	Jx = 0;

SEEK_PRT_WINDOW:
	++Jx;
	Switch_Window(Jx);
	if(  ((File_Name != (Temp_String + '.PRT')) &
			(Jx < Window_Count))  ) {
		Goto SEEK_PRT_WINDOW;
	}

	if(  ((File_Name == (Temp_String + '.PRT')) &
			(Window_Id != Text_Window))  ) {
		Erase_Window;
	} else {
		Create_Window;
	}

	Format_Line = Line_In;
	Print_Window = Window_Id;
	Line_Terminator = '|13';
	File_Name = Temp_String + '.PRT';
	Create_Window;
	Line_Terminator = '|13';
	Index_Window = Window_Id;
	File_Name = Temp_String + '.NDX';

/* Get printer specific data */
/* Create a global array of all printer commands for this printer */
	Attribute_Count = 0;
	if(  (F_Mode == 0)  ) {
		RM('SETCONFIG /DB=PRINTERS/T=' + PRINTER_TYPE + '.PRT');
		if(  (Return_Int)  ) {
			Reg_Exp_Stat = True;
			Eol;
			if(  (Search_Fwd('%|||12',0))  ) {
				if(  (Cur_Char == '')  ) {
					while(  (Cur_Char == '')  ) {
						Command = Parse_Str('F=',Get_Line);
						if(  (Svl(Command) == 2)  ) {
							++Attribute_Count;
							Set_Global_Str('@PA' + Str(Attribute_Count),'F=' + Caps(Command)
															 + 'C=' + Parse_Str('C=',Get_Line));
						}
						Down;
					}
				}
			}
		}
	}

/*  special development code for testing execution time
Def_Int(Start_Minutes,Start_Seconds,End_Minutes,End_Seconds);
If ((Val(Start_Minutes,Copy(Time,4,2))) and (Val(Start_Seconds,Copy(Time,7,2)))) Then
End;
 */
	Switch_Win_Id(Text_Window);

/* Set up variables for reporting percentage done */
	Todds_Jx3 = End_Of_File;
	if(  Todds_Jx3 < 20  ) {
		Todds_Jx3 = 20;
	}
	Todds_Jx3 = Todds_Jx3 / 20;

	Todds_Jx4 = 0;

/* Main program loop */

	while(  (C_Line <= End_Of_File)  ) {
/* Report percentage done */
		++Todds_Jx4;
		if(  Todds_Jx4 > 4  ) {
			Todds_Jx = (C_line * 5) / Todds_Jx3;
			if(  (Todds_Jx > 99)  ) { Todds_Jx = 99; }
			Todds_Jx2 = (Todds_Jx * 2) / 5;
			Percentage = Str(Todds_jx);

			Write(Copy('께께께께께께께께께께께께께께께께께께께께',1,Todds_Jx2) +
						Copy('같같같같같같같같같같같같같같같같같같같같',1,40 - Todds_Jx2) + ' ' +
						Copy('    ',1,3 - Svl(Percentage)) + Percentage + '% Complete.',4,8,0,M_B_Color);
			Todds_jx4 = 0;
		}

/* Check for the user pressing <ESC> */
		if(  (Check_Key)  ) {
			if(  (Key1 == 27)  ) {
		MOU_ESC:
				Abort = True;
				Kill_Box;
				Goto END_OF_MAC;
			} else if(  (key1 == 0) & (key2 == 250)  ) {
				RM('CheckEvents /G=@PFEV#/M=1/#=1');
				if(  (Return_Int)  ) {
					Goto MOU_ESC;
				}
			}
		}

		Line_Out = '';
		Comment = False;
		Line_In = Get_Line;
/* Strip out HARD_CR if present */
		if(  (Hard_Cr != '|0')  ) {
STRIP_HARD_CR:
			Jx = XPos(Hard_Cr,Line_In,1);
			if(  (Jx)  ) {
				Line_In = Str_Del(Line_In,Jx,1);
				Goto STRIP_HARD_CR;
			}
		}

/* Page break char */
		if(  (XPos('|12',Line_In,1))  ) {
/* This is the part that can force a new page to start on an even or odd page number */
			Default = 0;
			Line_In = Str_Del(Line_In,1,1);
			Call GET_PARAMETER;
			New_Page_Num = Cur_Page_Num + 1;
			Blank_Page = False;
			if(  (Parameter == 2)  ) {
				if(  (New_Page_Num % 2)  ) {
					Blank_Page = True;
				}
			}
			if(  (Parameter == 1)  ) {
				if(  ((New_Page_Num % 2) == 0)  ) {
					Blank_Page = True;
				}
			}
			Page_Break = True;
			Goto SKIP_PRINTING;
		}

/* If the format code delimiter('.' default) is not in the line of text, bypass
all this stuff and use as is. */
		if(  (XPos(Code_Delimiter,Line_In,1) == 0)  ) {
			Line_Out = Line_In;

			Goto NO_COMMAND;
		}

		Temp_Integer = XPos(Code_Delimiter + 'IG',Caps(Line_In),1);
		if(  (Temp_Integer)  ) {                                             /* Ignore */
			if(  (Temp_Integer ==  XPos(Code_Delimiter,Line_In,1))  ) {
/* If there is some text before this command, then output the line so far */
				if(  (Temp_Integer > 1)  ) {
					Temp_String = Copy(Line_In,1,Temp_Integer - 1);
					Line_Out = Line_Out + Temp_String;
				}
				Line_In = Copy(Line_In,Temp_Integer + 3,SVL(Line_In) - Temp_Integer - 2);
				if(  (Line_Out != '')  ) {
					Call Print_Line;
				}

/* We put the formatted lines into a second window instead of sending directly to
the printer or altering the original file.  It would be possible to alter this
macro to do either option.  Feel especially ambitious today? */

				SWITCH_WIN_Id(Print_Window);
				Put_Line('|10' + Copy(Blank_Line,1,Left_Margin) + Line_In);
				DOWN;
				++Cur_Line_Num;
				GOTO_COL(1);
				SWITCH_WIN_Id(Text_Window);
				Goto SKIP_PRINTING;
			}
		}

		while(  (SVL(Line_In) > 0)  ) {
			Delimiter_Pos = XPos(Code_Delimiter,Line_In,1);
			if(  (Delimiter_Pos == 0)  ) {
				Line_Out = Line_Out + Line_In;
				Line_In = '';
				Goto NO_COMMAND;
			} else {
/* When a format code delimiter is detected, see if it is a command. */
				Line_Out = Line_Out + Copy(Line_In,1,Delimiter_Pos - 1);
				Temp_String = Copy(Line_In,Delimiter_Pos + 1,2);
				Line_In = Str_Del(Line_In,1,Delimiter_Pos + 2);
				Command = Caps(Temp_String);
				if(  ((XPos('' + Command + '',Commands,1) == 0) | (Command == ''))  ) {
/* If the suspected command is invalid, then treat it as if it were text. */
					Line_Out = Line_Out + Code_Delimiter + Temp_String;
				} else {
/* At this point we are certain that we have a valid command */

					if(  (Command == 'DU')  ) {             /* Define user formatting command */
/* The remainder of the line is assumed to be part of the command definition so
output the line so far */
						if(  (Line_Out != '') & Not(Comment)  ) {
							Call Print_Line;
						}
						Default = 1;
						Call GET_PARAMETER;
						if(  ((Parameter > 0) & (Parameter < 11))  ) {
							Set_Global_Str(Code_Delimiter + 'UD' + Str(Parameter),Line_In);
						}
						Line_Out = '';
						Goto SKIP_PRINTING;
					}

					if(  (Command == 'UD')  ) {            /* Execute user formatting command */
						Default = 1;
						Call GET_PARAMETER;
						if(  ((Parameter > 0) & (Parameter < 11))  ) {
							Line_In = Copy(Global_Str(Code_Delimiter + 'UD' + Str(Parameter)),1,255 - SVL(Line_In)) + Line_In;
						}
						Goto COMMAND_DECODED;
					}

					if(  Command == 'NP'  ) {                                      /* Comment */
						Comment = TRUE;
/* If there is some text before the comment, then output the line so far */
						if(  (Line_Out != '')  ) {
							Call Print_Line;
							Line_Out = '';
						}
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'PB')  ) {                                 /* Page break */
						Default = Cur_Page_Num + 1;
						Call GET_PARAMETER;
						New_Page_Num = Parameter;
						Page_Break = True;
						Goto COMMAND_DECODED;
					}

					if(  (Command == 'X[')  ) {                                      /* INDEX */
/* Parse out index definition, process it, then print out line */
						Call PROCESS_INDEX;
						Goto COMMAND_DECODED;
					}


					if(  (Command == 'DF')  ) {                             /* Define footer */
/* The remainder of the line is assumed to be part of the footer definition so
output the line so far */
						if(  (Line_Out != '') & Not(Comment)  ) {
							Call Print_Line;
						}
						Default = 1;
						Call GET_PARAMETER;
						if(  (Parameter == 2)  ) {
							Footer_Copied = False;
							Footer2 = Line_In;
						} else {
							Footer1 = Line_In;
							if(  ((Footer2 == '') | (Footer_Copied == True))  ) {
								Footer_Copied = True;
								Footer2 = Line_In;
							}
						}
						Line_Out = '';
						Goto SKIP_PRINTING;
					}
					if(  (Command == 'DH')  ) {                             /* Define header */
/* The remainder of the line is assumed to be part of the header definition so
output the line so far */
						if(  (Line_Out != '') & Not(Comment)  ) {
							Call Print_Line;
						}
						Default = 1;
						Call GET_PARAMETER;
						if(  (Parameter == 2)  ) {
							Header2 = Line_In;
							Header_Copied = False;
						} else {
							Header1 = Line_In;
							if(  ((Header2 == '') | (Header_Copied == True))  ) {
								Header_Copied = True;
								Header2 = Line_In;
							}
						}
						Line_Out = '';
						Goto SKIP_PRINTING;
					}

					if(  (Command == 'SF')  ) {                            /* Supress footer */
						Supress_Footer = True;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'SH')  ) {                            /* Supress header */
						Supress_Header = True;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'LS')  ) {                              /* Line spacing */
/* See if we have a parameter */
						Default = 1;
						Call GET_PARAMETER;
						Line_Spacing = Parameter;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'BP')  ) { /* print Headers/Footers on blank pages */
						Default = 0;
						Call GET_PARAMETER;
						Leave_Blank = Parameter;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'FF')  ) { /* insert form feed characters at page break */
						Default = 0;
						Call GET_PARAMETER;
						Form_Feed = Parameter;
						Goto COMMAND_DECODED;
					}

/* This is the table of contents stuff */

					if(  (Command == 'TE')  ) {                                /* Table entry */
/* Ignore table entry if table is not defined */
						if(  (Table_Defined == False)  ) {
							Goto SKIP_ENTRY;
						}

						Default = 1;
						Call GET_PARAMETER;

						Table_Level = Parameter;

						Table_Line = Line_In;

/* Delete leading spaces */
						while(  (Copy(Table_Line,1,1) == ' ')  ) {
							Table_Line = Str_Del(Table_Line,1,1);
						}
/* This is a special case of striping out any index entry, although later on
a routine is run to strip out all codes, the [ character of the index code
is striped out beforehand, which will confuse the code stripping routine. */
STRIP_IDX:
						Jx = XPos('.X[',Caps(Table_Line),1);
						if(  (Jx)  ) {
							Jy = XPos(']',Table_Line,Jx);
							if(  (Jy == 0)  ) {
								Jy = Svl(Table_Line);
							}
							Table_Line = Str_Del(Table_Line,Jx,Jy - Jx + 1);
							Goto STRIP_IDX;
						}

/* Truncate line according to the presense of certain delimiters */
/* The following are considered by M.E. to be word delimiters !@#$^&()[]''`"~\|,<>
also the curly braces used to enclose this comment. */
/* We had to remove the $ char and add the * char for the macro manual */
						Temp_Integer = Svl(Table_Delimits);
						while(  (Temp_Integer > 0)  ) {
							if(  XPos(Copy(Table_Delimits,Temp_Integer,1),Table_Line,1)  ) {
								Table_Line = Copy(Table_Line,1,XPos(Copy(Table_Delimits,
																			 Temp_Integer,1),Table_Line,1) - 1);
							}
							--Temp_Integer;
						}
/* We want to truncate if there is more than one consecutive space char */
						if(  XPos('  ',Table_Line,1)  ) {
							Table_Line = Copy(Table_Line,1,XPos('  ',Table_Line,1) - 1);
						}

						if(  ((Table_Level < 1) | (Table_Level > 5))  ) {
							Table_Level = 1;
						}

						if(  Table_Level == 1  ) {
							Table_Count = Table_Count1;
							Table_Indent = Table_Indent1;
							Table_Style = Table_Style1;
/* We must reset all subseqent Table_Counts */
							Table_Count2 = 0;
							Table_Count3 = 0;
							Table_Count4 = 0;
							Table_Count5 = 0;
						}
						if(  Table_Level == 2  ) {
							Table_Count = Table_Count2;
							Table_Indent = Table_Indent2;
							Table_Style = Table_Style2;
							Table_Count3 = 0;
							Table_Count4 = 0;
							Table_Count5 = 0;
						}
						if(  Table_Level == 3  ) {
							Table_Count = Table_Count3;
							Table_Indent = Table_Indent3;
							Table_Style = Table_Style3;
							Table_Count4 = 0;
							Table_Count5 = 0;
						}
						if(  Table_Level == 4  ) {
							Table_Count = Table_Count4;
							Table_Indent = Table_Indent4;
							Table_Style = Table_Style4;
							Table_Count5 = 0;
						}
						if(  Table_Level == 5  ) {
							Table_Count = Table_Count5;
							Table_Indent = Table_Indent5;
							Table_Style = Table_Style5;
						}

						if(  (Table_Style == 1)  ) {
							Table_Line = Str(Table_Count + 1) + '. ' + Table_Line;
							Line_In = Str(Table_Count + 1) + '. ' + Line_In;
						}

						if(  (Table_Style == 2)  ) {
							Table_Line = Char(Table_Count + 65) + '. ' + Table_Line;
							Line_In = Char(Table_Count + 65) + '. ' + Line_In;
						}

						if(  (Table_Style == 3)  ) {
							Table_Line = Remove_Space(Copy(
										 'I.   II.  III. IV.  V.   VI.  VII. VIII.IX.  X.   XI.  ',
																	(Table_Count * 5) + 1,5)) + ' ' + Table_Line;
							Line_In = Remove_Space(Copy(
										 'I.   II.  III. IV.  V.   VI.  VII. VIII.IX.  X.   XI.  ',
																		 (Table_Count * 5) + 1,5)) + ' ' + Line_In;
						}

						++Table_Count;
						if(  Table_Level == 1  ) {
							Table_Count1 = Table_Count;
						}
						if(  Table_Level == 2  ) {
							Table_Count2 = Table_Count;
						}
						if(  Table_Level == 3  ) {
							Table_Count3 = Table_Count;
						}
						if(  Table_Level == 4  ) {
							Table_Count4 = Table_Count;
						}
						if(  Table_Level == 5  ) {
							Table_Count5 = Table_Count;
						}
						Switch_Win_Id(Table_Window);


						if(  (Table_Line_Num == Bottom_Margin)  ) {
							if(  ((Footer_Margin > (Page_Length + 1)) |
									(Supress_Table_Footer == True))  ) {
								Goto NO_TABLE_FOOTER;
							}

							if(  (XPos('|10',Get_Line,1) == 0)  ) {
								Goto_Col(1);
								Text('|10');
							}
							while(  ((Table_Line_Num < (Footer_Margin - 1)) &
																						(Table_Line_Num < Page_Length))   ) {
								Down;
								Put_Line('|10');
								++Table_Line_Num;
							}
							if(  (Table_Line_Num <= Page_Length)  ) {
								Temp_Line_Out = Line_Out;
								if(  (Table_Page_Num == ((Table_Page_Num / 2) * 2))  ) {
									Line_Out = Footer2;
								} else {
									Line_Out = Footer1;
								}

								Temp_String = Caps(Line_Out);

								if(  (XPos(Code_Delimiter + 'PA',Temp_String,1))  ) {
									Table_Page_Str = Remove_Space(Copy(
																'i   ii  iii iv  v   vi  vii viiiix  x   xi  ',
																						((Table_Page_Num - 1) * 4) + 1,4));

									Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'PA',Temp_String,1) - 1)
										+ Table_Page_Str +
										Copy(Line_Out,XPos(Code_Delimiter + 'PA',Temp_String,1) + 3,254);
							}
								For_Table = True;
								Call PRINT_LINE;
								Jx = XPos(Code_Delimiter + 'CE',Temp_String,1);
								if(  (Jx)  ) {
									Line_Out = Str_Del(Line_Out,Jx,3);
									Call CENTER_LINE;
								}
								Switch_Win_Id(Table_Window);

								Put_Line('|10' + Copy(Blank_Line,1,Left_Margin) + Line_Out);
								Line_Out = Temp_Line_Out;
							}
NO_TABLE_FOOTER:
							if(  (Form_Feed)  ) {
								Eol;
								Text('|12|13');
							} else {
								while(  (Table_Line_Num <= Page_Length)   ) {
									Down;
									Put_Line('|10');
									++Table_Line_Num;
								}
							}
							++Table_Page_Num;
							Table_Line_Num = 1;
						}

						if(  (Table_Line_Num == 1)  ) {
							while(  ((Table_Line_Num <= Header_Margin) &
																						(Table_Line_Num < Page_Length))   ) {
								Down;
								Put_Line('|10');
								++Table_Line_Num;
							}
							Goto_Col(1);
							Temp_Line_Out = Line_Out;
							if(  (Supress_Table_Header)  ) {
								Line_Out = '';
							} else {
								if(  (Table_Page_Num == ((Table_Page_Num / 2) * 2))  ) {
									Line_Out = Header2;
								} else {
									Line_Out = Header1;
								}
								Temp_String = Caps(Line_Out);
								if(  (XPos(Code_Delimiter + 'PA',Temp_String,1))  ) {
									Table_Page_Str = Remove_Space(Copy(
																	'i   ii  iii iv  v   vi  vii viiiix  x   xi  ',
																							((Table_Page_Num - 1) * 4) + 1,4));
									Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'PA',Temp_String,1) - 1)
										+ Table_Page_Str +
										Copy(Line_Out,XPos(Code_Delimiter + 'PA',Temp_String,1) + 3,254);
								}
							}
							For_Table = True;
							Call PRINT_LINE;
							Jx = XPos(Code_Delimiter + 'CE',Temp_String,1);
							if(  (Jx)  ) {
								Line_Out = Str_Del(Line_Out,Jx,3);
								Call CENTER_LINE;
							}
							Switch_Win_Id(Table_Window);
							Eol;

							Text(Copy(Blank_Line,1,Left_Margin) + Line_Out);

							Line_Out = Temp_Line_Out;
							while(  ((Table_Line_Num <= Top_Margin) &
																						 (Table_Line_Num < Page_Length))  ) {
								++Table_Line_Num;
								Down;
								Put_Line('|10');
							}
							if(  (Table_Page_Num == 1)  ) {
								Line_Out = 'Table of Contents';
								Call CENTER_LINE;
								Switch_Win_Id(Table_Window);
								Put_Line('|10' + Copy(Blank_Line,1,Left_Margin) + Line_Out);
								Down;
								Put_Line('|10');
								Down;
								Table_Line_Num = Table_Line_Num + 2;
								Line_Out = Temp_Line_Out;
							}
						}
/* Strip out any remaining formatting codes */
						Temp_String = Table_Line;
						Call STRIP_CODES;
						Table_Line = Temp_String;
						Put_Line('|10' + Copy(Blank_Line,1,Left_Margin + Table_Indent) + Temp_String);
						Eol;
						if(  (Periods)  ) {
							Text(Copy('...................................................................................'
												,1,(Table_Margin - Table_Indent) - SVL(Table_Line) -
																 Left_Margin - Length(Str(Cur_Page_Num)) - 2));
						}
						Table_Line = Str(Cur_Page_Num);
						Goto_Col(Table_Margin - SVL(Table_Line) + 1);
						Text(Table_Line);
						++Table_Line_Num;
						Down;
						Goto_Col(1);
						Switch_Win_Id(Text_Window);
						Goto COMMAND_DECODED;

SKIP_ENTRY:
					}

					if(  (Command == 'TC')  ) {                   /* Define table of contents */
/* Ignore table definition if table is already defined */
						if(  (Table_Defined)  ) {
							Goto SKIP_DEFINITION;
						} else {
							Table_Defined = True;
						}

						Table_Line_Num = 1;
						Table_Page_Num = 1;
						Temp_String = File_Name;

/* Create a window for the Table of contents */
						Create_Window;
						Line_Terminator = '|13';
						Table_Window = Window_Id;
						File_Name = Truncate_Extension(Temp_String) + '.TOC';
						Switch_Win_Id(Text_Window);

/* See if we have a parameter */
						Default = 75;
						Call GET_PARAMETER;
						Table_Margin = Parameter;
/* See if user wants to pad between table entry and page number with spaces or
periods */
						Default = 0;
						Call GET_PARAMETER;
						Periods = Parameter;

						Default = 0;
						Call GET_PARAMETER;
						Supress_Table_Header = Parameter > 0;
						Default = 0;
						Call GET_PARAMETER;
						Supress_Table_Footer = Parameter > 0;

						Default = 0;
						Call GET_PARAMETER;
						Table_Indent1 = Parameter;
						Default = 0;
						Call GET_PARAMETER;
						Table_Style1 = Parameter;
						Default = 5;
						Call GET_PARAMETER;
						Table_Indent2 = Table_Indent1 + Parameter;
						Default = 0;
						Call GET_PARAMETER;
						Table_Style2 = Parameter;
						Default = 5;
						Call GET_PARAMETER;
						Table_Indent3 = Table_Indent2 + Parameter;
						Default = 0;
						Call GET_PARAMETER;
						Table_Style3 = Parameter;
						Default = 5;
						Call GET_PARAMETER;
						Table_Indent4 = Table_Indent3 + Parameter;
						Default = 0;
						Call GET_PARAMETER;
						Table_Style4 = Parameter;
						Default = 5;
						Call GET_PARAMETER;
						Table_Indent5 = Table_Indent4 + Parameter;
						Default = 0;
						Call GET_PARAMETER;
						Table_Style5 = Parameter;
						Goto COMMAND_DECODED;
SKIP_DEFINITION:
					}


					if(  (Command == 'TM')  ) {                                 /* Top Margin */
/* See if we have a parameter */
						Default = 5;
						Call GET_PARAMETER;
						Top_Margin = Parameter;
						Call CHECK_MARGINS;
						Goto COMMAND_DECODED;
					}

					if(  (Command == 'BM')  ) {                              /* Bottom Margin */
/* See if we have a parameter */
						Default = Page_Length - 6;
						Call GET_PARAMETER;
						Bottom_Margin = Parameter;
						Call CHECK_MARGINS;
						Bottom_Set = True;
						Goto COMMAND_DECODED;
					}

					if(  (Command == 'PL')  ) {                               /* Page Length */
/* See if we have a parameter */
						Default = 66;
						Call GET_PARAMETER;
						Page_Length = Parameter;
						if(  (Bottom_Set == False)  ) {
							Bottom_Margin = Page_Length - 6;
						}
						if(  (Footer_Set == False)  ) {
							Footer_Margin = Page_Length - 2;
						}
						Call CHECK_MARGINS;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'RM')  ) {                              /* Right Margin */
/* See if we have a parameter */
						Default = 80;
						Call GET_PARAMETER;
						Rght_Margin = Parameter;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'LM')  ) {                               /* Left Margin */
/* See if we have a parameter */
						Print_margin = 0;
						Default = 1;
						Call GET_PARAMETER;
						Left_Margin = Parameter - 1;
						Goto COMMAND_DECODED;
					}
					if(  (Command == 'HM')  ) {                              /* Header Margin */
/* See if we have a parameter */
						Default = 2;
						Call GET_PARAMETER;
						Header_Margin = Parameter;
						Call CHECK_MARGINS;
						Goto COMMAND_DECODED;
					}

					if(  (Command == 'FM')  ) {                              /* Footer Margin */
/* See if we have a parameter */
						Default = Page_Length - 2;
						Call GET_PARAMETER;
						Footer_Margin = Parameter;
						Call CHECK_MARGINS;
						Footer_Set = True;
						Goto COMMAND_DECODED;
					}

COMMAND_DECODED:
				}
			}
		}

NO_COMMAND:
/* Process top and header margins, print header */
		if(  ((Cur_Line_Num == 1) & (Comment == false))  ) {
/* If we have an unprinted line in memory, we need to save it */
			Temp_Line_Out = Line_Out;
			Temp_Flag = Comment;
/* Put in header(if header is null, just another blank line will be printed) */
			Comment = False;
			if(  Header_Margin < 0  ) {
				Goto NO_HEADER;
			}
/* fill in header margin */
			Move_Count = Header_Margin;
			Call MOVE_DOWN;
			if(  (Supress_Header == False)  ) {
/* If on an even numbered page then */
				if(  (Cur_Page_Num == ((Cur_Page_Num / 2) * 2))  ) {
					Line_Out = Header2;
				} else {
					Line_Out = Header1;
				}
			} else {
				Line_Out = '';
			}
			Supress_Header = False;
			Print_HF = True;
			Call PRINT_LINE;
NO_HEADER:
/* fill in top margin */
			Move_Count = (Top_Margin - Cur_Line_Num) + 1;
			Call MOVE_DOWN;
			Line_Out = Temp_Line_Out;
			Comment = Temp_Flag;
			if(  Line_Out == ''  ) {
				Goto SKIP_PRINTING;
			}
		}
HERE:
		if(  (Comment)  ) {
			Goto SKIP_PRINTING;
		}

PREPARE_TO_PRINT:
		Call PRINT_LINE;
		if(  Line_Spacing > 1  ) {
			Move_Count = Line_Spacing - 1;
			Call MOVE_DOWN;
		}

SKIP_PRINTING:
		if(  ((Cur_Line_Num >= Bottom_Margin) | ((Page_Break == True) & (Cur_Line_Num > 1))) ) {
/* fill in footer margin */

			if(  ((Footer_Margin > (Page_Length + 1)) | (Supress_Footer == True) |
																			((Footer1 == '') & (Footer2 == '')))  ) {
				Goto NO_FOOTER;
			}

			Making_Margin = True;
			Move_Count = (Footer_Margin - Cur_Line_Num) - 1;
			Call MOVE_DOWN;
/* If on an even numbered page then */
				if(  (Cur_Page_Num == ((Cur_Page_Num / 2) * 2))  ) {
					Line_Out = Footer2;
				} else {
					Line_Out = Footer1;
				}
			Print_HF = True;
/* Put in footer(if footer is null, just another blank line will be printed) */
			Call PRINT_LINE;
NO_FOOTER:
			Supress_Footer = False;
/* fill in bottom margin */
/* we go one line beyond the page length to get on line one of the next page */
			if(  (Form_Feed)  ) {
				Print_HF = True;
				if(  (Switch_Win_Id(Print_Window))  ) {
					Up;
					Eol;
					Text('|12|13');
				}
			} else {
				Move_Count = (Page_Length - Cur_Line_Num) + 1;
				Call MOVE_DOWN;
			}

			if(  (Page_Break)  ) {
				Cur_Page_Num = New_Page_Num;
				Page_Break = False;
			} else {
				++Cur_Page_Num;
			}
			Cur_Line_Num = 1;
			Making_Margin = False;
			HF_Toggle = Not(HF_Toggle);
/* This is the part for the forced odd/even page numbers */
			if(  (Blank_Page)  ) {
				HF_Toggle = Not(HF_Toggle);
				if(  (Leave_Blank)  ) {
					Page_Break = True;
					Blank_Page = False;
					Goto NO_COMMAND;
				} else {
					++Cur_Page_Num;

					if(  (Form_Feed)  ) {
						Print_HF = True;
						if(  (Switch_Win_Id(Print_Window))  ) {
							Up;
							Eol;
							Text('|12|13');
						}
					} else {
						Move_Count = Page_Length;
						Call MOVE_DOWN;
					}

					Blank_Page = False;
					Cur_Line_Num = 1;
				}
			}
		} else {
			Page_Break = False;
		}
		Switch_Win_Id(Text_Window);
		Down;
		Goto_Col(1);
	}

/* End of main program loop */
	if(  (Line_Out == '|12')  ) {
		Line_Out = '';
	}

	if(  ((Cur_Line_Num == 1) & (Line_Out != ''))  ) {
/* If we have an unprinted line in memory, we need to save it */
		Temp_Line_Out = Line_Out;
		Temp_Flag = Comment;
/* Put in header(if header is null, just another blank line will be printed) */
		Comment = False;
		if(  ((Header_Margin < 0) | (Header1 == ''))  ) {
			Goto NO_LAST_HEADER;
		}
/* fill in header margin */
		Move_Count = Header_Margin;
		Call MOVE_DOWN;
		if(  (Supress_Header == False)  ) {
/* If on an even numbered page then */
			if(  (Cur_Page_Num == ((Cur_Page_Num / 2) * 2))  ) {
				Line_Out = Header2;
			} else {
				Line_Out = Header1;
			}
		} else {
			Line_Out = '';
		}
		Supress_Header = False;
		Print_HF = True;
		Call PRINT_LINE;
NO_LAST_HEADER:
/* fill in top margin */
		Move_Count = (Top_Margin - Cur_Line_Num) + 1;
		Call MOVE_DOWN;
		Line_Out = Temp_Line_Out;
		Comment = Temp_Flag;
	}

	if(  Cur_Line_Num > 1  ) {
		if(  ((Footer_Margin > (Page_Length + 1)) | (Footer1 == ''))  ) {
			Goto DONE;
		}

/* fill in bottom margin and footer for last page */
		Making_Margin = True;
		Move_Count = (Footer_Margin - Cur_Line_Num) - 1;
		Call MOVE_DOWN;

		if(  (Supress_Footer == False)  ) {
/* If on an even numbered page then */
				if(  (Cur_Page_Num == ((Cur_Page_Num / 2) * 2))  ) {
					Line_Out = Footer2;
				} else {
					Line_Out = Footer1;
				}
			} else {
				Line_Out = '';
		}
		Print_HF = True;
		Supress_Footer = False;
/* Put in footer(if footer is null, just another blank line will be printed) */
		Call PRINT_LINE;
	}

DONE:
/* Get rid of blank lines at the end this is a kludge, but I don't have time
right now to fix it right */
	SWITCH_WIN_Id(Print_Window);
KILL_BLANKS:
	EOL;
	if(  ((C_Col == 1) & (C_Line > 1))  ) {
		Back_Space;
		Goto KILL_BLANKS;
	}
	Temp_Flag = (Get_Line == '|12');
	Cr;
	SWITCH_WIN_Id(Text_Window);
	if(  ((Form_Feed) & (Temp_Flag == False))  ) {
		Print_HF = True;
		if(  (Switch_Win_Id(Print_Window))  ) {
			Up;
			Eol;
			Text('|12|13');
		}
	}

	if(  (Table_Defined)  ) {
		Switch_Win_Id(Table_Window);

		if(  (XPos('|10',Get_Line,1) == 0)  ) {
			Goto_Col(1);
			Text('|10');
		}
		while(  ((Table_Line_Num < (Footer_Margin - 1)) &
																						(Table_Line_Num < Page_Length))   ) {
			Down;
			Put_Line('|10');
			++Table_Line_Num;
		}
		if(  (Table_Line_Num <= Page_Length)  ) {
			if(  ((Footer_Margin > (Page_Length + 1)) |
					(Footer1 == '') | (Supress_Table_Footer == true))  ) {
				Goto NO_LAST_TABLE_FOOTER;
			}
			if(  (Table_Page_Num == ((Table_Page_Num / 2) * 2))  ) {
				Line_Out = Footer2;
			} else {
				Line_Out = Footer1;
			}

			Temp_String = Caps(Line_Out);

			if(  (XPos(Code_Delimiter + 'PA',Temp_String,1))  ) {
				Table_Page_Str = Remove_Space(Copy(
																'i   ii  iii iv  v   vi  vii viiiix  x   xi  ',
																						((Table_Page_Num - 1) * 4) + 1,4));
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'PA',Temp_String,1) - 1)
					+ Table_Page_Str +
					Copy(Line_Out,XPos(Code_Delimiter + 'PA',Temp_String,1) + 3,254);

			}

			For_Table = True;
			Call PRINT_LINE;
			Jx = XPos(Code_Delimiter + 'CE',Temp_String,1);
			if(  (Jx)  ) {
				Line_Out = Str_Del(Line_Out,Jx,3);
				Call CENTER_LINE;
			}
			Switch_Win_Id(Table_Window);
			Put_Line('|10' + Copy(Blank_Line,1,Left_Margin) + Line_Out);

NO_LAST_TABLE_FOOTER:
			if(  (Form_Feed)  ) {
				Eol;
				Text('|12|13');
			} else {
				while(  (Table_Line_Num < Page_Length)  ) {
					Down;
					Put_Line('|10');
					++Table_Line_Num;
				}
			}
		}
		Block_Begin;
		Tof;
		Block_End;
		Jx = Cur_Window;
		Switch_Win_Id(Print_Window);
		Tof;

		Window_Move(Jx);
		if(  (Form_Feed)  ) {
			Goto_Line(Block_Line2);
			Eol;
			Del_Char;
			Del_Char;
		}

		Block_Off;
		Tof;

		Switch_Win_Id(Table_Window);
		Delete_Window;
	}
	Write('께께께께께께께께께께께께께께께께께께께께 100% Complete.',4,8,0,M_B_Color);
/* If an index was generated, merge it to the bottom of the document. */
	if(  (Switch_Win_Id(Index_Window))  ) {
		Tof;
		if(  (Not(At_Eof))  ) {
			Cr;
			Up;
			Put_Line('|10' + '******INDEX******');
			Block_Begin;
			Eof;
			Block_End;
			Jx = Cur_Window;
			Switch_Win_Id(Print_Window);
			Eof;
			if(  (C_Col > 1)  ) {
				Down;
			}
			Window_Move(Jx);
			Block_Off;
		}
		if(  (Switch_Win_Id(Index_Window))  ) {
			Delete_Window;
		}
	}
	Kill_Box;
	Switch_Win_Id(Text_Window);
	Tof;
	Switch_Win_Id(Print_Window);
	Tof;
	if(  (Cur_Char == '|10')  ) {
		Del_Char;
	}
	Refresh = True;
	New_Screen;
	Make_Message('Print file name is: ' + File_Name);
	Goto END_OF_MAC;

/*********************************** SUBROUTINES *******************************/

PROCESS_INDEX:
	Jx = XPos(']',Line_In,1);
	if(  (Jx == 0)  ) {
		Index_Main = Line_In;
		Line_In = '';
	} else {
		Index_Main = Copy(Line_In,1,Jx - 1);
		Line_In = Str_Del(Line_In,1,Jx);
	}
	if(  (Svl(Index_Main))  ) {
		if(  (Switch_Win_Id(Index_Window))  ) {
			Jx = XPos('^',Index_Main,1);
			if(  (Jx)  ) {
				if(  (Jx < Svl(Index_Main))  ) {
					Index_Sub = '     ' + Copy(Index_Main,Jx + 1,75);
				} else {
					Index_Sub = '';
				}
				Index_Main = Copy(Index_Main,1,Jx - 1);
			} else {
				Index_Sub = '';
			}
			if(  (Svl(Index_Main))  ) {
				Tof;
				if(  (At_Eof)  ) {
/* If this is the first entry, skip the unnecessary garbage and put the stuff in */
					Put_Line('|10' + Caps(Index_Main));
					if(  (Svl(Index_Sub))  ) {
						Down;
						Goto_Col(1);
						Text('|10' + Caps(Index_Sub));
					}
					Text(' (' + Str(Cur_Page_Num) + ')');
					RET;
				}

				if(  (Search_Fwd('%|10' + Index_Main + '{ (}||$',0))  ) {
/* If we found an identical index entry, simply add the page number to it */

					if(  (Svl(Index_Sub))  ) {
						Down;
FIND_SUB:

						if(  ((Caps(Copy(Get_Line,2,Svl(Index_Sub))) <= Caps(Index_Sub)) &
								(At_Eof == False))  ) {
							if(  (Copy(Get_Line,2,5) == '     ')  ) {
								Down;
								Goto FIND_SUB;
							}
						} else {
							Up;
						}
						if(  (Caps(Copy(Get_Line,2,Svl(Index_Sub))) != Caps(Index_Sub))  ) {
							Eol;
							Cr;
							Text('|10' + Caps(Index_Sub) + ' (' + Str(Cur_Page_Num) + ')');
						}
					}
					if(  (XPos('(' + Str(Cur_Page_Num) + ')',Get_Line,1) == 0)  ) {
						Eol;
/* If we already have a page number on this main heading, put in a comma */
						if(  (Copy(Get_Line,Length(Get_Line),1) == ')')  ) {
							Text(',');
						}
						Text(' (' + Str(Cur_Page_Num) + ')');
					}
				} else {
/* Search for the closest thing to our entry */
FIND_MAIN:
					if(  (Caps(Copy(Get_Line,2,Svl(Index_Main))) < Caps(Index_Main))  ) {
						Down;
						while(  (Copy(Get_Line,2,5) == '     ')  ) {
							Down;
						}
						if(  (Not(At_Eof))  ) {
							Goto FIND_MAIN;
						}
					}
/* We should now be at the line below where we want to insert the new index */
					Cr;
					Up;
					Text('|10' + Caps(Index_Main));
					if(  (Svl(Index_Sub))  ) {
						Eol;
						Cr;
						Text('|10' + Caps(Index_Sub));
					}
					Text(' (' + Str(Cur_Page_Num) + ')');
				}
			}
		}

		Switch_Win_Id(Text_Window);
	}
	RET;

PRINT_LINE:
	if(  ((F_MODE == 0) | (PRINT_HF == TRUE))  ) {
/* the following lines will decode any variables imbedded in the line */
		if(  XPos(Code_Delimiter,Line_Out,1)  ) {
			Temp_String = Caps(Line_Out);

			while(  XPos(Code_Delimiter + 'PA',Temp_String,1)  ) {
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'PA',Temp_String,1) - 1) +
										Str(Cur_Page_Num) +
										Copy(Line_Out,XPos(Code_Delimiter + 'PA',Temp_String,1) + 3,
												SVL(Line_Out) - (XPos(Code_Delimiter + 'PA',Temp_String,1)) - 2);
				Temp_String = Caps(Line_Out);
			}
			while(  XPos(Code_Delimiter + 'DA',Temp_String,1)  ) {
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'DA',Temp_String,1) - 1) + Cur_Date + Copy(Line_Out,Pos(Code_Delimiter + 'DA',Temp_String) + 3,SVL(Line_Out) - (XPos(Code_Delimiter + 'DA',Temp_String,1)) - 2);
				Temp_String = Caps(Line_Out);
			}
			while(  XPos(Code_Delimiter + 'FN',Temp_String,1)  ) {
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'FN',Temp_String,1) - 1) + Cur_File_Name + Copy(Line_Out,Pos(Code_Delimiter + 'FN',Temp_String) + 3,SVL(Line_Out) - (XPos(Code_Delimiter + 'FN',Temp_String,1)) - 2);
				Temp_String = Caps(Line_Out);
			}
			while(  XPos(Code_Delimiter + 'TI',Temp_String,1)  ) {
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'TI',Temp_String,1) - 1) + Cur_Time + Copy(Line_Out,XPos(Code_Delimiter + 'TI',Temp_String,1) + 3,SVL(Line_Out) - (XPos(Code_Delimiter + 'TI',Temp_String,1)) - 2);
				Temp_String = Caps(Line_Out);
			}

			while(  XPos(Code_Delimiter + 'CE',Temp_String,1)  ) {
				Line_Out = Copy(Line_Out,1,XPos(Code_Delimiter + 'CE',Temp_String,1) - 1) +
											Copy(Line_Out,XPos(Code_Delimiter + 'CE',Temp_String,1) + 3,SVL(Line_Out) -
																									(XPos(Code_Delimiter + 'CE',Temp_String,1)) - 2);
				Call CENTER_LINE;
				Temp_String = Caps(Line_Out);
			}

/* Decode any possible printer specific codes */
			Temp_Integer = 1;
GET_PS_CODE:
			Delimiter_Pos = XPos(Code_Delimiter,Line_Out,Temp_Integer);
			if(  ((Delimiter_Pos > 0) & (Delimiter_Pos < (Svl(Line_Out) - 1)))  ) {
				Temp_Integer = Delimiter_Pos + 1;
				Command = Caps(Copy(Line_Out,Temp_Integer,2));
				Call GET_PRINTER_CODE;
				if(  (Return_Int)  ) {
					Line_Out = Copy(Line_Out,1,Delimiter_Pos - 1) + Return_Str
								+ Copy(Line_Out,Delimiter_Pos + 3,255);
				}
				Goto GET_PS_CODE;
			}
		}
	}

	if(  (For_Table)  ) {
		For_Table = False;
		RET;
	}
	SWITCH_WIN_Id(Print_Window);
	Tabs_To_Spaces(Line_Out); /* convert tabs to spaces */
	/* Add in margin unless we are printing a header or a footer */
	Eol;
	if(  (C_Col == 1)  ) {
		Put_Line('|10' + Copy(Blank_Line,1,Left_Margin) + Line_Out);
	} else {
		TEXT(Copy(Blank_Line,1,Left_Margin) + Line_Out);
	}
	DOWN;
	GOTO_COL(1);

	Print_HF = False;
	++Cur_Line_Num;
	SWITCH_WIN_Id(Text_Window);
	RET;

MOVE_DOWN:
	SWITCH_WIN_Id(Print_Window);
	if(  (Xpos('|10',Get_Line,1) == 0)  ) {
		Goto_Col(1);
		Text('|10');
	}
	while(  (Move_Count > 0)  ) {
		DOWN;
		Put_Line('|10');
		--Move_Count;
		++Cur_Line_Num;
		if(  ((Cur_Line_Num ==  Bottom_Margin) & Not(Making_Margin) & (Blank_Page == 0))  ) {
			Goto NO_MORE_SPACES;
		}
	}

	NO_MORE_SPACES:
	Goto_Col(1);
	SWITCH_WIN_Id(Text_Window);
	RET;

GET_PARAMETER:
	if(  (Line_In == '')  ) {
		Parameter = Default;
		Ret;
	}
	Temp_Param = '';
	if(  (Copy(Line_In,1,1) == ' ')  ) {
/* Strip off leading space */
		Line_In = Str_Del(Line_In,1,1);
		Param_Space = True;
	} else {
		Param_Space = False;
	}
	Temp_Integer = Val(Parameter,Line_In);
	if(  (Temp_Integer == 0)  ) {
		Temp_Integer = SVL(Line_In) + 1;
	}
	if(  (Temp_Integer > 1)  ) {
		Val_Error = Val(Parameter,Copy(Line_In,1,Temp_Integer - 1));
		Line_In = Str_Del(Line_In,1,Temp_Integer - 1);
	} else {
		Parameter = Default;
		if(  (Param_Space)  ) {
			Line_In = ' ' + Line_In;
		}
	}
	RET;

IMBEDDED_PARAMETER:
	Temp_Param = '';
	if(  Line_Out == ''  ) {
		Parameter = Default;
		Ret;
	}
	if(  (Copy(Line_Out,Param_Pos,1) == ' ')  ) {
/* Strip off leading space */
		Line_Out = Str_Del(Line_Out,Param_Pos,1);
		Param_Space = True;
	} else {
		Param_Space = False;
	}
	while(  ((XPos(Copy(Line_Out,Param_Pos,1),'0123456789',1) > 0) & (Line_Out != '') & (Copy(Line_Out,Param_Pos,1) != ''))  ) {
		Temp_Param = Temp_Param + Copy(Line_Out,Param_Pos,1);
		Line_Out = Str_Del(Line_Out,Param_Pos,1);
	}
/* If no parameter, then add the space back in if there was one before */
	if(  (Temp_Param == '')  ) {
		Parameter = Default;
		if(  (Param_Space)  ) {
			Line_Out = Str_Ins(' ',Line_Out,Param_Pos + 1);
		}
	} else {
		Val_Error = Val(Parameter,Temp_Param);
	}
	RET;

CENTER_LINE:
/* Determine the length of the text */
	if(  (Line_Out == '')  ) {
		RET;
	}
	if(  (Print_HF)  ) {
		SWITCH_WIN_Id(Print_Window);
		Goto_Col(Left_Margin + 1);
		SWITCH_WIN_Id(Text_Window);
	}
/* Strip Out leading spaces */


/* We must strip out any printer specific formatting codes and any associated
parameters to calculate the proper text length */
	Temp_String = Caps(Line_Out);
	CALL STRIP_ATTRIBUTES;
	while(  (Copy(Temp_String,1,1) == ' ')  ) {
		Temp_String = Str_Del(Temp_String,1,1);
	}
	while(  (Copy(Line_Out,1,1) == ' ')  ) {
		Line_Out = Str_Del(Line_Out,1,1);
	}
	Return_Str = Line_Out;
	Call FIND_TEXT_BEGINNING;
	while(  (Copy(Line_Out,Return_Int,1) == ' ')  ) {
		Line_Out = Str_Del(Line_Out,Return_Int,1);
	}

	Text_Length = SVL(Temp_String);

	if(  Text_Length > (Rght_Margin - Left_Margin - 2)  ) {
		RET;
	}
	Line_Out = Copy(Blank_Line,1,((((Rght_Margin - Left_Margin) - Text_Length) / 2))) + Line_Out;
	RET;

STRIP_CODES:
/* This subroutine strips out all legal fprint codes from Temp_String */
	if(  XPos(Code_Delimiter,Temp_String,1)  ) {
/* Strip out all codes which use no parameters */
		Temp_Integer = 0;
		while(  (Temp_Integer < 9)  ) {
STRIP_NO_PARAM:
			Jx = XPos(Code_Delimiter + Copy('THCESFSHIGFNTIDAPA',(Temp_Integer * 2) + 1,2),
																												 Caps(Temp_String),1);
			if(  (Jx)  ) {
				Temp_String = Str_Del(Temp_String,Jx,3);
				Goto STRIP_NO_PARAM;
			}
			++Temp_Integer;
		}

/* Strip out all codes wich use 1 parameter */
		Temp_Integer = 0;
		while(  (Temp_Integer < 19)  ) {
STRIP_1_PARAM:
			Jx = XPos(Code_Delimiter + Copy('TMBMHMFMNPPBDFDHLSPLRMLMULEMITDWTEDUUD'
																 ,(Temp_Integer * 2) + 1,2),Caps(Temp_String),1);
			if(  (Jx)  ) {
				Temp_String = Str_Del(Temp_String,Jx,3);
/* Strip out parameter if present */
				if(  (XPos(' ',Temp_String,Jx) == Jx)  ) {
					Param_Space = True;
					Temp_String = Str_Del(Temp_String,Jx,1);
				} else {
					Param_Space = False;
				}
STRIP_1_NUMERIC:
				if(  ((XPos(Copy(Temp_String,Jx,1),'1234567890-',1) > 0)
																				 & (Jx <= SVL(Temp_String)))  ) {
					Temp_String = Str_Del(Temp_String,Jx,1);
					Param_Space = False;
					Goto STRIP_1_NUMERIC;
				}
				if(  (Param_Space)  ) {
					Temp_String = Str_Ins(' ',Temp_String,Jx);
				}
				Goto STRIP_1_PARAM;
			}
			++Temp_Integer;
		}

/* Strip out .tc which has up to 12 paramters */
STRIP_TC:
		Jx = XPos(Code_Delimiter + 'TC',Caps(Temp_String),1);
		if(  (Jx)  ) {
			Temp_String = Str_Del(Temp_String,Jx,3);
/* Strip out parameters if present */
			while(  ((XPos(Copy(Temp_String,Jx,1),'1234567890- ',1) > 0)
																				 & (Jx <= SVL(Temp_String)))  ) {
				Temp_String = Str_Del(Temp_String,Jx,1);
			}
			GOTO STRIP_TC;
		}
	}

/* Strip out any printer specific commands */
STRIP_ATTRIBUTES:
	if(  XPos(Code_Delimiter,Temp_String,1)  ) {
		Jx = 0;
		while(  (Jx < Attribute_Count)  ) {
			++Jx;
STRIP_ATTR:
			Jy = XPos(Code_Delimiter +
				Parse_Str('F=',Global_Str('@PA' + Str(Jx))),Caps(Temp_String),1);
			if(  (Jy)  ) {
				Temp_String = Str_Del(Temp_String,Jy,3);
				Goto STRIP_ATTR;
			}
		}
	}

/* Strip out index entry */
	Jx = XPos(Code_Delimiter + 'X[',Caps(Temp_String),1);
	if(  (Jx)  ) {
		Jy = XPos(']',Temp_String,Jx);
		if(  (Jy == 0)  ) {
			Jy = Svl(Temp_String);
		}
		Temp_String = Str_Del(Temp_String,Jx,Jy - Jx + 1);
	}
	RET;

CHECK_MARGINS:
/* This checks to make sure that all margins and the page length are all in
proper numeric order */
	if(  (Header_Margin >= Top_Margin)  ) {
		Header_Margin = Top_Margin - 1;
	}
	if(  (Top_Margin >= Bottom_Margin)  ) {
		Bottom_Margin = Top_Margin + 1;
	}
	if(  (Bottom_Margin >= Footer_Margin)  ) {
		Footer_Margin = Bottom_Margin + 1;
	}
	RET;

GET_PRINTER_CODE:
	Return_Int = False;

/* See if the print formatter code is in the printer code list */
	Jx = 0;
	while(  (Jx < Attribute_Count)  ) {
		++Jx;
		if(  (Parse_Str('F=',Global_Str('@PA' + Str(Jx))) == Caps(Command))  ) {
			Return_Int = 1;
			Return_Str = Parse_Str('C=',Global_Str('@PA' + Str(Jx)));
			RET;
		}
	}
	RET;

FIND_TEXT_BEGINNING:
/* This routine finds the position of the first non-code character */
	Return_Str = Caps(Return_Str);
	Return_Int = 1;
	if(  (Xpos(Code_Delimiter,Return_Str,1) == 0)  ) {
		Ret;
	}

/* Skip over printer attribute commands */
LOOK_AGAIN:
	Temp_Integer = 0;
	while(  (Temp_Integer < Attribute_Count)  ) {
		++Temp_Integer;
		if(  (Copy(Return_Str,Return_Int,3) == (Code_Delimiter +
				Parse_Str('F=',Global_Str('@PA' + Str(Temp_Integer)))))  ) {
			Return_Int = Return_Int + 3;
			Goto LOOK_AGAIN;
		}
	}
	RET;

END_OF_MAC:
	RM("SETWINDOWNAMES");
/* Deallocate user defined commands */
	Temp_Integer = 1;
	while(  (Temp_Integer < 11)  ) {
		Set_Global_Str('.UD' + Str(Temp_Integer),'');
		++Temp_Integer;
	}

/* Deallocate printer specific codes */
	while(  (Attribute_Count)  ) {
		Set_Global_Str('@PA' + Str(Attribute_Count),'');
		--Attribute_Count;
	}

	if(  (Abort)  ) {
		Refresh = True;
		Redraw;
		Make_Message('Print formatter aborted by user.');
	}
	Refresh = True;
	Messages = True;
	Undo_Stat = Temp_Undo;

	Reg_Exp_Stat = T_Reg_Exp_Stat;
	Insert_Mode = T_Insert_Mode;
	return_int = 100;
	error_level = 0;

/*  special development code for testing execution time
If ((Val(End_Minutes,Copy(Time,4,2))) and (Val(End_Seconds,Copy(Time,7,2)))) Then
End;
If (Start_Seconds > End_Seconds) Then
	End_Seconds := End_Seconds + 60;
	--End_Minutes;
End;
If (Start_Minutes > End_Minutes) Then
	End_Minutes := End_Minutes + 60;
End;
Switch_Win_Id(Print_Window);
Text('Elapsed time ' + Str(End_Minutes - Start_Minutes) + ':' + Str(End_Seconds - Start_Seconds));
 */
}

macro FCMD TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	FCMD

Description: The top level menu for inserting print formatting codes.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	Set_Global_Str('FCT_1','/H=PRNFORM^FMTCODEMISC/M=F_CODE_SUBMENU /MC=1');
	Set_Global_Str('XFCT_1','Misc');
	Set_Global_Str('FCT_2','/H=PRNFORM^FMTCODEVAR/M=F_CODE_SUBMENU /MC=2');
	Set_Global_Str('XFCT_2','Variables');
	Set_Global_Str('FCT_3','/H=PRNFORM^FMTCODEATT/M=F_CODE_SUBMENU /MC=3/S=1');
	Set_Global_Str('XFCT_3','Attributes');
	Set_Global_Str('FCT_4','/H=PRNFORM^FMTCODEPGSTUP/M=F_CODE_SUBMENU /MC=4');
	Set_Global_Str('XFCT_4','Page-setup');
	Set_Global_Str('FCT_5','/H=PRNFORM^FMTCODEHDRFTR/M=F_CODE_SUBMENU /MC=5');
	Set_Global_Str('XFCT_5','Headers-footers');
	Set_Global_Str('FCT_6','/H=PRNFORM^TOFC/M=F_CODE_SUBMENU /MC=6');
	Set_Global_Str('XFCT_6','Table-of-contents');

	RM('USERIN^TOPMENU /M=XFCT_/G=FCT_/#=6/X=3/S=1/BC=1/L=INSERT PRINT FORMATTER CODES/Y=4');

	Set_Global_Str('FCT_1','');
	Set_Global_Str('XFCT_1','');
	Set_Global_Str('FCT_2','');
	Set_Global_Str('XFCT_2','');
	Set_Global_Str('FCT_3','');
	Set_Global_Str('XFCT_3','');
	Set_Global_Str('FCT_4','');
	Set_Global_Str('XFCT_4','');
	Set_Global_Str('FCT_5','');
	Set_Global_Str('XFCT_5','');
	Set_Global_Str('FCT_6','');
	Set_Global_Str('XFCT_6','');
	return_int = 100;
}

macro F_CODE_SUBMENU {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	F_CODE_SUBMENU

Description: The sub level menu for inserting print formatting codes.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/
	int Main_Choice,Sub_Count;
	str Sub_Str,Sub_Title;

	Main_Choice = Parse_Int('/MC=',MParm_Str);

	if(  (Main_Choice == 1)  ) {
		Set_Global_Str('FCS_1','/H=PRNFORM^FMTCODEMISC/M=INSERT_F_CODES /MC=1/SC=1');
		Set_Global_Str('XFCS_1','page Break');
		Set_Global_Str('FCS_2','/M=INSERT_F_CODES /MC=1/SC=2');
		Set_Global_Str('XFCS_2','Center');
		Set_Global_Str('FCS_3','/M=INSERT_F_CODES /MC=1/SC=3');
		Set_Global_Str('XFCS_3','cOmment');
		Set_Global_Str('FCS_4','/M=INSERT_F_CODES /MC=1/SC=4');
		Set_Global_Str('XFCS_4','iGnore');
		Set_Global_Str('FCS_5','/M=INSERT_F_CODES /MC=1/SC=5');
		Set_Global_Str('XFCS_5','Define user code');
		Set_Global_Str('FCS_6','/M=INSERT_F_CODES /MC=1/SC=6');
		Set_Global_Str('XFCS_6','Execute user code');
		Sub_Count = 6;
		Sub_Title = 'MISCELLANEOUS';
	}

	if(  (Main_Choice == 2)  ) {
		Set_Global_Str('FCS_1','/H=PRNFORM^FMTCODEVAR/M=INSERT_F_CODES /MC=2/SC=1');
		Set_Global_Str('XFCS_1','File name');
		Set_Global_Str('FCS_2','/M=INSERT_F_CODES /MC=2/SC=2');
		Set_Global_Str('XFCS_2','Time');
		Set_Global_Str('FCS_3','/M=INSERT_F_CODES /MC=2/SC=3');
		Set_Global_Str('XFCS_3','Date');
		Set_Global_Str('FCS_4','/M=INSERT_F_CODES /MC=2/SC=4');
		Set_Global_Str('XFCS_4','Page number');
		Sub_Count = 4;
		Sub_Title = 'VARIABLES';
	}

	if(  (Main_Choice == 3)  ) {
					RM('INSERT_F_CODES /MC=3' + mparm_str);

		if(  (return_int < 1)  ) {
			Goto EXIT2;
		}
		Goto EXIT;
	}


	if(  (Main_Choice == 4)  ) {
		Set_Global_Str('FCS_1','/H=PRNFORM^FMTCODEPGSTUP/M=INSERT_F_CODES /MC=4/SC=1');
		Set_Global_Str('XFCS_1','line Spacing');
		Set_Global_Str('FCS_2','/M=INSERT_F_CODES /MC=4/SC=2');
		Set_Global_Str('XFCS_2','Page length');
		Set_Global_Str('FCS_3','/M=INSERT_F_CODES /MC=4/SC=3');
		Set_Global_Str('XFCS_3','Right margin');
		Set_Global_Str('FCS_4','/M=INSERT_F_CODES /MC=4/SC=4');
		Set_Global_Str('XFCS_4','Left margin');
		Set_Global_Str('FCS_5','/M=INSERT_F_CODES /MC=4/SC=5');
		Set_Global_Str('XFCS_5','Top margin');
		Set_Global_Str('FCS_6','/M=INSERT_F_CODES /MC=4/SC=6');
		Set_Global_Str('XFCS_6','Bottom margin');
		Set_Global_Str('FCS_7','/M=INSERT_F_CODES /MC=4/SC=7');
		Set_Global_Str('XFCS_7','Form feeds');
		Sub_Count = 7;
		Sub_Title = 'PAGE SETUP';
	}

	if(  (Main_Choice == 5)  ) {
		Set_Global_Str('FCS_1','/H=PRNFORM^FMTCODEHDRFTR/M=INSERT_F_CODES /MC=5/SC=1');
		Set_Global_Str('XFCS_1','define Header');
		Set_Global_Str('FCS_2','/M=INSERT_F_CODES /MC=5/SC=2');
		Set_Global_Str('XFCS_2','define Footer');
		Set_Global_Str('FCS_3','/M=INSERT_F_CODES /MC=5/SC=3');
		Set_Global_Str('XFCS_3','Suppress header');
		Set_Global_Str('FCS_4','/M=INSERT_F_CODES /MC=5/SC=4');
		Set_Global_Str('XFCS_4','sUpress footer');
		Set_Global_Str('FCS_5','/M=INSERT_F_CODES /MC=5/SC=5');
		Set_Global_Str('XFCS_5','header Margin');
		Set_Global_Str('FCS_6','/M=INSERT_F_CODES /MC=5/SC=6');
		Set_Global_Str('XFCS_6','footer mArgin');
		Sub_Count = 6;
		Sub_Title = 'HEADERS//FOOTERS';
	}

	if(  (Main_Choice == 6)  ) {
		Set_Global_Str('FCS_1','/H=PRNFORM^TOFC/M=INSERT_F_CODES /MC=6/SC=1');
		Set_Global_Str('XFCS_1','Define table');
		Set_Global_Str('FCS_2','/M=INSERT_F_CODES /MC=6/SC=2');
		Set_Global_Str('XFCS_2','table Entry');
		Sub_Count = 2;
		Sub_Title = 'TABLE OF CONTENTS';
	}

	RM('USERIN^SUBMENU /GCLR=1/M=XFCS_/G=FCS_/S=1/L=' + Sub_Title + '/#='
		+ Str(Sub_Count) + '/X=' + Parse_Str('/X=',MParm_Str) + '/Y=' +
		Parse_Str('/Y=',MParm_Str));
	if(  (Return_Int < 1)  ) {
		Goto EXIT2;
	}

EXIT:
	return_int = 100;
EXIT2:
	Sub_Count = 7;
	while(  (Sub_Count)  ) {
		Set_Global_Str('FCS_' + Str(Sub_Count),'');
		Set_Global_Str('XFCS_' + Str(Sub_Count),'');
		--Sub_Count;
	}

}

macro INSERT_F_CODES TRANS {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	INSERT_F_CODES

Description: Inserts print formatting codes based on menu choices from
F_CODE_SUBMENU.

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

int Temp_Integer,Main_Choice,Sub_Choice,Jx,Jy,Temp_Refresh,
				Temp_Insert_Mode;
str Temp_String,Temp_String2,Printer_Type[20];
char Code_Delimiter;

	Temp_Refresh = Refresh;
	Refresh = False;
	Temp_Insert_Mode = Insert_Mode;

	Printer_Type = Global_Str('PRINTER_TYPE');
/*
	IF (Str_Char(Printer_Type,1) = '') THEN
		Printer_Type := Parse_Str('PT=',PRINTER_TYPE);
	END;
 */
	Main_Choice = 1;
	Sub_Choice = 1;
	Code_Delimiter = Copy(Global_Str('Format_Code_Delimit') + '.',1,1);
		Main_Choice = Parse_Int('/MC=',MParm_Str);
		Sub_Choice = Parse_Int('/SC=',MParm_Str);

		if(  (Main_Choice == 1)  ) {
			if(  (Sub_Choice == 0)  ) {
				Goto DONT_INSERT_CODE;
			}

			Temp_String = '';
			if(  (Sub_Choice == 1)  ) {
/* Querybox is a general purpose "boxed" prompt. */
				Return_Int = 0;
				RM('QUERYBOX /N=1/P=Enter optional new page number(0=sequential):/H=PRNFORM^FMTCODEMISC/C=5/L=7/W=4' +
									'/T=PAGE BREAK/MIN=0');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				if(  (Return_Int == 0)  ) {
					Temp_String = '';
				} else {
					Temp_String = Str(Return_Int);
				}
			}
			if(  (Sub_Choice == 5)  ) {
				Return_Int = 1;
				RM('QUERYBOX /N=1/P=Enter number of user code(1-10):/H=PRNFORM^FMTCODEMISC/C=5' +
									'/L=7/W=2/T=DEFINE USER CODE/MIN=1/MAX=10');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 6)  ) {
				Return_Int = 1;
				RM('QUERYBOX /N=1/P=Enter number of user code(1-10):/H=PRNFORM^FMTCODEMISC/C=5' +
									'/L=7/W=2/T=DEFINE USER CODE/MIN=1/MAX=10');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			Temp_String = Copy('pbcenpigduud',((Sub_Choice - 1) * 2) + 1,2) +
																															Temp_String;
			Goto INSERT_CODE;
		}

		if(  (Main_Choice == 2)  ) {
			if(  (Sub_Choice == 0)  ) {
				Kill_Box;
				Goto DONT_INSERT_CODE;
			}
			Temp_String = Copy('fntidapa',((Sub_Choice - 1) * 2) + 1,2);
			Goto INSERT_CODE;
		}

		if(  (Main_Choice == 3)  ) {
			if(  (Printer_Type == '')  ) {
				RM('MEERROR^Beeps /C=1');
				Make_Message('Printer type not specified!  Use Install or Print.  Press any key.');
				Goto DONT_INSERT_CODE;
			}

			RM('MEUTIL3^PRINT_CODE_MENU /F=1/H=PRNFORM^FMTCODEATT' + MParm_Str);
			if(  (Return_Int < 1)  ) {
				Return_Int = -2;
				Goto ATTRIBUTE_EXIT;
			}
			Temp_String = Parse_Str('F=',Return_Str);
			if(  (Temp_String == '')  ) {
				RM('MEERROR^MESSAGEBOX /B=2/T=WARNING/M=Print formatter code for: ' +
						Parse_Str('N=',Return_Str) + ' not defined.');
				Goto BACK_UP;
			} else {
				Goto INSERT_CODE;
			}
		}


		if(  (Main_Choice == 4)  ) {
			if(  (Sub_Choice == 0)  ) {
				Kill_Box;
				Goto DONT_INSERT_CODE;
			}

			Temp_String = '';
			if(  (Sub_Choice == 1)  ) {
				Return_Int = 1;
				RM('QUERYBOX /N=1/P=Enter line spacing:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=2' +
									'/T=LINE SPACING/MIN=1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 2)  ) {
				Return_Int = 66;
				RM('QUERYBOX /N=1/P=Enter page length:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=3' +
									'/T=PAGE LENGTH/MIN=1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 3)  ) {
				Return_Int = 80;
				RM('QUERYBOX /N=1/P=Enter right margin:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=3' +
									'/T=RIGHT MARGIN/MIN=2/MAX=254');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 4)  ) {
				Return_Int = 0;
				RM('QUERYBOX /N=1/P=Enter left margin:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=2' +
									'/T=LEFT MARGIN/MIN=1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 5)  ) {
				Return_Int = 5;
				RM('QUERYBOX /N=1/P=Enter top margin:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=2' +
									'/T=TOP MARGIN/MIN=-1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 6)  ) {
				Return_Int = 60;
				RM('QUERYBOX /N=1/P=Enter bottom margin:/H=PRNFORM^FMTCODEPGSTUP/C=31/L=7/W=3' +
									'/T=BOTTOM MARGIN/MIN=1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			if(  (Sub_Choice == 7)  ) {
				Put_Box(31,7,67,10,0,M_B_Color,'FORM FEEDS',True);
				Write('Form feeds at page breaks:',32,8,0,M_B_Color);
				RM('USERIN^XMENU /T=0/X=59/Y=8/S=2/M=On(PRNFORM^FMTCODEPGSTUP)oFf()');
				Temp_Integer = Return_Int;
				Kill_Box;
				if(  (Temp_Integer < 1)  ) {
					Goto BACK_UP;
				}
				Temp_String = Copy('10',Temp_Integer,1);
			}

			Temp_String = Copy('lsplrmlmtmbmff',((Sub_Choice - 1) * 2) + 1,2) + Temp_String;
			Goto INSERT_CODE;
		}

		if(  (Main_Choice == 5)  ) {
			if(  (Sub_Choice == 0)  ) {
				Kill_Box;
				Goto DONT_INSERT_CODE;
			}
			Temp_String = '';
			if(  (Sub_Choice == 1)  ) {
				Put_Box(42,7,76,10,0,M_B_Color,'DEFINE HEADER',True);
				Write('Which header?',43,8,0,M_B_Color);
				RM('USERIN^XMENU /T=0/X=57/Y=8/S=1/M=Primary(PRNFORM^FMTCODEHDRFTR)Secondary()');
				Temp_Integer = Return_Int;
				Kill_Box;
				if(  (Temp_Integer < 1)  ) {
					Goto BACK_UP;
				}
				Temp_String = Copy('12',Temp_Integer,1);
			}

			if(  (Sub_Choice == 2)  ) {
				Put_Box(42,7,76,10,0,M_B_Color,'DEFINE FOOTER',True);
				Write('Which footer?',43,8,0,M_B_Color);
				RM('USERIN^XMENU /T=0/X=57/Y=8/S=1/M=Primary(PRNFORM^FMTCODEHDRFTR)Secondary()');
				Temp_Integer = Return_Int;
				Kill_Box;
				if(  (Temp_Integer < 1)  ) {
					Goto BACK_UP;
				}
				Temp_String = Copy('12',Temp_Integer,1);
			}

			if(  (Sub_Choice == 5)  ) {
				Return_Int = 2;
				RM('QUERYBOX /N=1/P=Enter header margin:/H=PRNFORM^FMTCODEHDRFTR/C=42/L=7/W=2' +
									'/T=HEADER MARGIN/MIN=-1');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}

			if(  (Sub_Choice == 6)  ) {
				Return_Int = 64;
				RM('QUERYBOX /N=1/P=Enter footer margin:/H=PRNFORM^FMTCODEHDRFTR/C=42/L=7/W=3' +
									'/T=FOOTER MARGIN/MIN=2');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			Temp_String = Copy('dhdfshsfhmfm',((Sub_Choice - 1) * 2) + 1,2) + Temp_String;
			Goto INSERT_CODE;
		}

		if(  (Main_Choice == 6)  ) {
			if(  (Sub_Choice == 0)  ) {
				Kill_Box;
				Goto DONT_INSERT_CODE;
			}

			Temp_String = '';
			if(  (Sub_Choice == 1)  ) {
				Set_Global_Int('IINT_1',75);
				Set_Global_Str('IPARM_1','/QK=1/T=Right margin for page numbers                     /TP=1/C=1/L=1/W=3/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_2','/T=Yes/F=No');
				Set_Global_Str('IPARM_2','/QK=1/T=Use periods between table entry and page number   /TP=5/C=1/L=2/W=3/H=PRNFORM^TOFC');
				Set_Global_Int('IINT_2',0);

				Set_Global_Str('ISTR_3','/T=Yes/F=No');
				Set_Global_Str('IPARM_3','/QK=9/T=Include headers in table of contents              /TP=5/C=1/L=3/W=3/H=PRNFORM^TOFC');
				Set_Global_Int('IINT_3',1);
				Set_Global_Str('ISTR_4','/T=Yes/F=No');
				Set_Global_Str('IPARM_4','/QK=9/T=Include footers in table of contents              /TP=5/C=1/L=4/W=3/H=PRNFORM^TOFC');
				Set_Global_Int('IINT_4',1);

				Set_Global_Int('IINT_5',0);
				Set_Global_Str('IPARM_5','/QK=1/T=Amount of indenting from left margin for level 1  /TP=1/C=1/L=6/W=2/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_6','Plain(PRNFORM^TOFC)Numeric()Alpha()Roman numeral()');
				Set_Global_Int('IINT_6',1);
				Set_Global_Str('IPARM_6','/QK=7/T=Level 1 table style                               /TP=3/C=1/L=7/W=13/H=PRNFORM^TOFC');

				Set_Global_Int('IINT_7',5);
				Set_Global_Str('IPARM_7','/T=Amount of indenting from level 1 for level 2      /TP=1/C=1/L=9/W=2/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_8','Plain(PRNFORM^TOFC)Numeric()Alpha()Roman numeral()');
				Set_Global_Int('IINT_8',1);
				Set_Global_Str('IPARM_8','/QK=7/T=Level 2 table style                               /TP=3/C=1/L=10/W=13/H=PRNFORM^TOFC');

				Set_Global_Int('IINT_9',5);
				Set_Global_Str('IPARM_9','/T=Amount of indenting from level 2 for level 3      /TP=1/C=1/L=12/W=2/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_10','Plain(PRNFORM^TOFC)Numeric()Alpha()Roman numeral()');
				Set_Global_Int('IINT_10',1);
				Set_Global_Str('IPARM_10','/QK=7/T=Level 3 table style                               /TP=3/C=1/L=13/W=13/H=PRNFORM^TOFC');

				Set_Global_Int('IINT_11',5);
				Set_Global_Str('IPARM_11','/T=Amount of indenting from level 3 for level 4      /TP=1/C=1/L=15/W=2/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_12','Plain(PRNFORM^TOFC)Numeric()Alpha()Roman numeral()');
				Set_Global_Int('IINT_12',1);
				Set_Global_Str('IPARM_12','/QK=7/T=Level 4 table style                               /TP=3/C=1/L=16/W=13/H=PRNFORM^TOFC');

				Set_Global_Int('IINT_13',5);
				Set_Global_Str('IPARM_13','/T=Amount of indenting from level 4 for level 5      /TP=1/C=1/L=18/W=2/H=PRNFORM^TOFC');
				Set_Global_Str('ISTR_14','Plain(PRNFORM^TOFC)Numeric()Alpha()Roman numeral()');
				Set_Global_Int('IINT_14',1);
				Set_Global_Str('IPARM_14','/QK=7/T=Level 5 table style                               /TP=3/C=1/L=19/W=13/H=PRNFORM^TOFC');


				RM( 'USERIN^DATA_IN /A=2/#=14/S=1/X=3/T=DEFINE TABLE OF CONTENTS/X=17/Y=4');

				if(  (Return_Int == FALSE)  ) {
					Goto BACK_UP;
				}
				Temp_String = ' ' + Str(Global_Int('IINT_1')) +
				' ' + Str(Global_Int('IINT_2')) +
				' ' + Str(Not(Global_Int('IINT_3'))) +
				' ' + Str(Not(Global_Int('IINT_4'))) +
				' ' + Str(Global_Int('IINT_5')) +
				' ' + Str(Global_Int('IINT_6') - 1) +
				' ' + Str(Global_Int('IINT_7')) +
				' ' + Str(Global_Int('IINT_8') - 1) +
				' ' + Str(Global_Int('IINT_9')) +
				' ' + Str(Global_Int('IINT_10') - 1) +
				' ' + Str(Global_Int('IINT_11')) +
				' ' + Str(Global_Int('IINT_12') - 1) +
				' ' + Str(Global_Int('IINT_13')) +
				' ' + Str(Global_Int('IINT_14') - 1);

			}

			if(  (Sub_Choice == 2)  ) {
				Return_Int = 1;
				RM('QUERYBOX /N=1/P=Enter table indent level:/H=PRNFORM^TOFC/C=50/L=7/W=1' +
									'/T=TABLE ENTRY/MIN=1/MAX=5');
				if(  (Return_Str == 'FALSE')  ) {
					Goto BACK_UP;
				}
				Temp_String = Str(Return_Int);
			}
			Temp_String = Copy('tcte',((Sub_Choice - 1) * 2) + 1,2) + Temp_String;
			Goto INSERT_CODE;
		}

		GOTO DONT_INSERT_CODE;

BACK_UP:
	Return_Int = -2;
	Goto BACK_UP_EXIT;

INSERT_CODE:
	Insert_Mode = True;
	Text(Code_Delimiter + Temp_String);
DONT_INSERT_CODE:
	Return_Int = 1;
BACK_UP_EXIT:
	Goto END_OF_MAC;

END_OF_MAC:
	return_int = 100;
ATTRIBUTE_EXIT:
	Refresh = Temp_Refresh;
	Insert_Mode = Temp_Insert_Mode;
}


macro INDXMAC {
/*******************************************************************************
																MULTI-EDIT MACRO

Name:	INDXMAC

Description: Converts a single line Column or Stream block into an index entry

							 (C) Copyright 1991 by American Cybernetics, Inc.
*******************************************************************************/

 int STR_LEN;
 if ((block_stat < 1) || (Block_Line1 != Block_Line2)) {
		 make_message('No single line Column or Stream block marked.');
		 goto end_of_mac;
 } else {
	PUSH_UNDO;
	INSERT_MODE = TRUE;
	GOTO_COL(BLOCK_COL1);
	COPY_BLOCK;
	GOTO_COL(BLOCK_COL2 + 1);
	STR_LEN = (BLOCK_COL2 - BLOCK_COL1) + 1;
	TEXT( '.X[' );
	GOTO_COL(BLOCK_COL2 + STR_LEN + 4);
	TEXT(']');
	BLOCK_OFF;
	LEFT;
	POP_UNDO;
 }
END_OF_MAC:
}
