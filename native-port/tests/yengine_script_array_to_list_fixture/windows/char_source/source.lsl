YOption allowall;
char[] input; list output; integer after;
default{state_entry(){input=new char[](2);input[0]=(char)65;input[1]=(char)90;output=xmrArray2List(input,0,0);llSleep(0.1);after=1;}}
