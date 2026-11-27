YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](2);destination=new integer[](2);source.obj=xmrFixedArrayAllocC(2);destination.obj=xmrFixedArrayAllocI(2);xmrFixedArraySetC(source.obj,0,(char)65);xmrFixedArraySetC(source.obj,1,(char)66);llSleep(0.1);xmrArrayCopy(source,0,destination,0,2);after=1;}}
