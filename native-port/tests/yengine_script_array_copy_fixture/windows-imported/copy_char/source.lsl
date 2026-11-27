YOption allowall;
char[] source; char[] destination; integer after;
default {state_entry(){source=new char[](3);destination=new char[](3);source[0]=(char)65;source[1]=(char)66;source[2]=(char)67;llSleep(0.1);xmrArrayCopy(source,0,destination,0,3);after=1;}}
