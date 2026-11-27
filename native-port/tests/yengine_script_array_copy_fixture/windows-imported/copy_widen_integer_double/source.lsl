YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](3);destination=new integer[](3);source.obj=xmrFixedArrayAllocI(3);destination.obj=xmrFixedArrayAllocF(3);xmrFixedArraySetI(source.obj,0,11);xmrFixedArraySetI(source.obj,1,-22);xmrFixedArraySetI(source.obj,2,33);llSleep(0.1);xmrArrayCopy(source,0,destination,0,3);after=1;}}
