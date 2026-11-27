YOption allowall;
float[] source; float[] destination; integer after;
default {state_entry(){source=new float[](3);destination=new float[](3);source[0]=1.2345678901234567;source[1]=-2.5;source[2]=0.125;llSleep(0.1);xmrArrayCopy(source,0,destination,0,3);after=1;}}
