YOption allowall;
integer[] source; integer[][] destination; integer after;
default {state_entry(){source=new integer[](1);source[0]=11;destination=new integer[][](1);llSleep(0.1);xmrArrayCopy(source,0,destination,0,1);after=1;}}
