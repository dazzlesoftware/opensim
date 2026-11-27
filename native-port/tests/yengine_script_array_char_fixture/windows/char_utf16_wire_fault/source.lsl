YOption allowall;
char[] input; string text; integer after;
default{state_entry(){input=new char[](4);input[0]=(char)55357;input[1]=(char)56832;input[2]=(char)0;input[3]=(char)55296;text=xmrChars2String(input,0,4);llSleep(0.1);after=1;}}
