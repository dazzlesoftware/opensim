YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](2);destination=new integer[](2);source.obj=xmrFixedArrayAllocI(2);destination.obj=xmrFixedArrayAllocC(2);xmrFixedArraySetI(source.obj,0,65);xmrFixedArraySetI(source.obj,1,66);llSleep(0.1);xmrArrayCopy(source,0,destination,0,2);after=1;}}
