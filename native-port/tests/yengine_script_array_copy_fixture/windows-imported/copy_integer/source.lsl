YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](3);destination=new integer[](3);source[0]=11;source[1]=22;source[2]=33;llSleep(0.1);xmrArrayCopy(source,0,destination,0,3);after=1;}}
