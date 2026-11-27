YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](2);source[0]=11;source[1]=22;destination=new integer[](2);llSleep(0.1);xmrArrayCopy(source,2,destination,2,0);after=1;}}
