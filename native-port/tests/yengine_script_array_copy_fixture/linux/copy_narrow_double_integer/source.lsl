YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](2);destination=new integer[](2);source.obj=xmrFixedArrayAllocF(2);destination.obj=xmrFixedArrayAllocI(2);xmrFixedArraySetF(source.obj,0,1.25);xmrFixedArraySetF(source.obj,1,2.5);llSleep(0.1);xmrArrayCopy(source,0,destination,0,2);after=1;}}
