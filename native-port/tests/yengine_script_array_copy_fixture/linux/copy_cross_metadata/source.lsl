YOption allowall;
float[] source; integer[] destination; integer after;
default {state_entry(){source=new float[](2);source[0]=1.25;destination=new integer[](2);llSleep(0.1);xmrArrayCopy(source,0,destination,0,2);after=1;}}
