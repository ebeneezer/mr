/*---------------01-13-93 03:39pm-------------------
sprintf
					Similar to the C sprintf function.
          Formats the conrol_string by inserting the passed
          parameters.  Returns the resulting string
          in output_string.
					Supports the following control codes:

					%d			decimal
					%u      unsigned decimal
					%x      hex
					%c      character
					%s      string
					%f			float
					%%			prints the % char

					Supports the width and precision modifiers.
					For example:  %4.3d
--------------------------------------------------*/
void sprintf(str &output_string, str control_string, ... )

#DEFINE numeric_chars "0123456789"
#DEFINE c_dec_int 100
#DEFINE c_un_dec_int 117
#DEFINE c_hex_int 120
#DEFINE c_char_int 99
#DEFINE c_string 115
#DEFINE c_percent 37
#DEFINE c_float 102

{
	int ix,pc = 0,pt,ip = 0, justify_left,width, precision;
	str tstr,tstr2;

  while( ip < svl( control_string ) )
	{
    ip = xpos( "%", control_string, ip + 1 );
		if ( ip == 0 )
			break;
		ix = ip;
		++ip;
		width = 0xffff;
		precision = 0xffff;
		tstr = "";
    if((ip <= svl(control_string)) && (str_char(control_string,ip) == "-"))
		{
			justify_left = TRUE;
			++ip;
		}
		else
		{
			justify_left = FALSE;
		}
    while((ip <= svl(control_string)) && xpos(str_char( control_string, ip ),numeric_chars,1))
		{
      tstr += str_char(control_string,ip++);
		}
		if(svl(tstr) != 0)
		{
			val(width,tstr);
		}
    if((ip <= svl(control_string)) && (str_char(control_string,ip) == "."))
		{
			++ip;
			tstr = "";
      while((ip <= svl(control_string)) && xpos(str_char( control_string, ip ),numeric_chars,1))
			{
        tstr += str_char(control_string,ip++);
			}
			if(svl(tstr) != 0)
			{
				val(precision,tstr);
			}
		}
    if(ip <= svl(control_string))
		{
			tstr = "";

      switch ( ascii( str_char(control_string,ip)) )
			{
				case c_dec_int :
				case c_un_dec_int :
					++pc;
          if( get_param_type(pc) != 2 )
            goto error_exit;
					tstr = str( get_param_int(pc));
					call do_precision;
					break;
				case c_hex_int :
					++pc;
          if( get_param_type(pc) != 2 )
            goto error_exit;
					tstr = hex_str( get_param_int(pc));
					call do_precision;
					break;
				case c_char_int :
					++pc;
          if( get_param_type(pc) != 2 )
            goto error_exit;
					tstr = char( get_param_int(pc));
					call do_prec_str;
					break;
				case c_string :
					++pc;
          if( get_param_type(pc) != 1 )
            goto error_exit;
					tstr = get_param_str(pc);
					call do_prec_str;
					break;
				case c_percent :
					tstr = "%";
					break;
				case c_float :
					{
						int w = width, p = precision;
            if( get_param_type(pc) != 5 )
              goto error_exit;

						if ( w == 0xffff )
						{
							w = 10;
						}
						if ( p == 0xffff )
						{
							p = 10;
						}
						++pc;
            tstr = rstr( get_param_real(pc), w, p );
						break;
					}
			}
			if(width != 0xffff)
			{
				if(width > svl(tstr))
				{
					tstr2 = "";
					pad_str( tstr2, width, ' ' );
					if(justify_left)
					{
						tstr = tstr + copy(tstr2,1,width - svl(tstr));
					}
					else
					{
						tstr = copy(tstr2,1,width - svl(tstr)) + tstr;
					}
				}
			}
      control_string = str_ins(tstr,  str_del(control_string,ix, ip - ix + 1), ix );
			ip = ix + svl(tstr);
		}
	}
  output_string = control_string;
  return();

do_precision:
	if((precision > svl(tstr)) && (precision != 0xffff))
	{
		tstr2 = "";
		pad_str( tstr2, precision - svl(tstr), '0');
		tstr = tstr2 + tstr;
	}
	ret;

do_prec_str:
	if(precision != 0xffff)
	{
		tstr = copy(tstr,1,precision);
	}
	ret;

error_exit:
    rm("MESSAGEBOX /T=SPRINTF ERROR!!!/B=2/M=Parameter " + str(pc) +
        " does not match the control string type!");
}


void messagef( str control_string, ... )
{
  sprintf( control_string, control_string, optional_params );
  make_message( control_string );
}

