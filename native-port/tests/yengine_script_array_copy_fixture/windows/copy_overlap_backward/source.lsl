YOption allowall;
integer[] source; integer[] alias; integer after;
default {state_entry(){source=new integer[](4);source[0]=1;source[1]=2;source[2]=3;source[3]=4;alias=source;llSleep(0.1);xmrArrayCopy(source,1,alias,0,3);after=1;}}
