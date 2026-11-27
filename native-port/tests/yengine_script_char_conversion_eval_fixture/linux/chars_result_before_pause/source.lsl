YOption allowall;
char[] input; string text; object received; object fresh; object literal; integer literal_equal; integer same_string; integer c1; integer after;
integer Use(string value,integer ignored){text=value;received=(object)value;return 1;}
integer Pause(){c1++;input[0]=(char)90;llSleep(0.1);return 7;}
default{state_entry(){input=new char[](1);input[0]=(char)65;literal="A";after=Use(xmrChars2String(input,0,1),Pause());fresh=xmrChars2String(input,0,1);literal_equal=(received==literal);same_string=(received==(object)text);}}
