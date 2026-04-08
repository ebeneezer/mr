macro_file CMAC;
/********************************MULTI-EDIT MACRO*******************************

Name: CMAC

Description: Support for the CMAC macro language

CMA_IND			- Performs smart indent for the CMAC macro language.
CMAMTCH			- Performs construct matching for the CMAC macro language.
CMASETX			- Initializes stuff for CMAC macro language editing.
CMABUILDCMT	- Builds a comment block for CMAC.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/

// **********************************************************************
// You can easily customize to the most common C styles by
// changing the value of the define c_style (defined after this comment).
// After you recompile, you will need to either exit Multi-Edit and restart,
// or set the global string "!CMA.Tmplt0" to NIL ("")
//
// cma_style == 0  if (x) {
//                   statement;
//                 }
//
//           == 1  if (x)
//                 {
//                   statement;
//                 }
//
//           == 2  if (x)
//                 { statement;
//                 }
// **********************************************************************

#define cma_style 1


// **********************************************************************
// You can easily customize to the most common C styles by
// changing the value of the define c_style (defined after this comment).
//
// c_style == 0    if (x) {
//                   statement;
//                 }
//
//         == 1    if (x)
//                 {
//                   statement;
//                 }
//
//         == 2    if (x)
//                 { statement;
//                 }
// **********************************************************************

#define c_style 1

macro CMA_IND {
/********************************MULTI-EDIT MACRO*******************************

Name: CMA_IND

Description: Performs smart indent for CMAC macro language.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	rm( 'C^C_IND' );
}

macro CMAMTCH {
/********************************MULTI-EDIT MACRO*******************************

Name: CMAMTCH

Description: Performs construct matching for CMAC macro language.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	rm( 'C^CMTCH ' + mparm_str );
}

macro CMASETX {
/********************************MULTI-EDIT MACRO*******************************

Name: CMASETX

Description: Initializes stuff for CMAC macro language editing.

							 (C) Copyright 1992 by American Cybernetics, Inc.
*******************************************************************************/
	if ( !Global_Int("@DA_AB_MATCH")  ) {
		key_to_window( <)>, 'c^c_close_paren' );
		key_to_window( <}>, 'c^c_close_brace' );
	}

  if ( ""==Global_Str("!CMA.Tmplt0") )
		Set_Global_Str("!CMA.Tmplt0", "ÓC=0ÓM=1"+
		"#define "+
		"/"+
		"/*˚CMABuildCMT"+           /* ÓC=0 case sensitive */
		"#include ƒ"+
		"break;"+
		"case ƒ :"+
		"continue;"+
		"do Å¸ while ( ƒ );"+
		"default:"+
		"else"+
		"else if ( ƒ ) Å¸"+
		"eilse f ( ƒ ) Å¸"+
		"for ( ƒ ) Å¸"+
		"if ( ƒ ) Å¸"+
		"goto ƒ;"+
		"switch ( ƒ ) Å®¸≠case  :"+
		"while ( ƒ ) Å¸"+
		"piarse_nt( \"ƒ\", mparm_str );" +
		"psarse_tr( \"ƒ\", mparm_str );" +
		"{®Æ}≠" +
		"macro ƒ Å¸"+
  	"macro_file ƒ;"+
  	"mfacro_ile ƒ;"+
		"(Ì)"
		);
	switch ( cma_style ) {
		case 2 :
		case 1 :
							set_global_str("!CMA.Tmplt0", global_str("!CMA.Tmplt0") +
											"ÅÆÒ{ÒÆ}" );
							break;
		default:
							set_global_str("!CMA.Tmplt0", global_str("!CMA.Tmplt0") +
											"ÅÒ{ÒÆ}" );
							break;
	}
}

/****************************Multi-Edit Macro********************************

 NAME:         CMABuildCmt

 DESCRIPTION:  Builds a comment block for CMAC.  The type of comment block
							 depends on the cursor location.  If the cursor is on column
							 1, the line before the word "macro", then a block like this
							 one will be built.

 PARAMETERS:   None.

 RETURNS:      None.

 ---------------------------------------------------------------------------
 MODIFICATIONS
 ---------------------------------------------------------------------------
 041293[scm]	 Function comments now avail for macro,void,str,int,real

*****************************09-23-91 01:39pm*******************************/
macro CMABuildCmt	trans2 {
	int ic, jx, jk,
			t_ins = insert_mode;

	insert_mode = TRUE;
	push_undo;
	refresh = false;
	mark_pos;
	left;
	if( cur_char == '*' ) {
		left;
	}
	ic = c_col;
	first_word;
	if(ic != c_col ) {
		goto_mark;
		goto exit;
	}
	if( c_col == 1 ) {
		down;
		// (3-31-93)[SCM]  we now have several types of macro functions!
		str scmbff;
		str cmtheading;
		switch ( lower(get_word(' |9|255')) )
		{
			case 'macro' :
				cmtheading = '***************************Multi-Edit Macro********************************';
				goto docmt;
				break;
			case 'void'  :
				cmtheading = '******************* Multi-Edit VOID Macro Function ************************';

				goto docmt;
				break;
			case 'str'   :
				cmtheading = '****************** Multi-Edit STRING Macro Function ***********************';
				goto docmt;
				break;
			case 'int'   :
				cmtheading = '***************** Multi-Edit INTEGER Macro Function ***********************';
				goto docmt;
				break;
			case 'real'  :
				cmtheading = '******************* Multi-Edit REAL Macro Function ************************';
docmt:
				word_right;
				return_str = get_word(' {/|9|255'); // org: ' {/|9|255;'
				scmbff = return_str;
				if ( str_char(scmbff,length(return_str)) != ';' )
				{
					up;
					goto_mark;
					text(cmtheading);
					cr; cr; goto_col(1);
					text(' NAME:         ' + return_str );
					cr; cr; goto_col(1);
					text(' DESCRIPTION:  '); mark_pos;
					cr; cr; goto_col(1);
					text(' PARAMETERS:   ');
					cr; cr; goto_col(1);
					text(' RETURNS:      ');
					cr; cr; goto_col(1);
					text('*****************************' + date + ' ' + str_del( time, 6,3 ) + '*******************************/');
					goto_mark;
				}
				else {
					goto do_c;
				}
				break; // end case 'macro'
			default :
				goto do_c;
		} // end switch

	}
	else {
do_c:
		goto_mark;
		jx = 0;
		while(  (c_col < 79) & (jx < 50)  ) {
			text('-');
			++jx;
		}
		jk = ic + (jx / 2) - 8;
		if(  jk < (ic + 2)  ) {
			jk = ic + 2;
		}
		goto_col( jk );
		insert_mode = false;
		text( date + ' ' + str_del( time, 6, 3) );
		insert_mode = true;
		eol;

		rm('C_IND');
		Mark_Pos;
		rm('C_IND');

		jk = 0;
		while(  (jk < jx)  ) {
			text('-');
			++jk;
		}
		text('*/');
		Goto_Mark;

	}
exit:
	pop_undo;
	insert_mode = t_ins;

}
