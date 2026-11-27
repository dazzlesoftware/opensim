YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](3);destination=new integer[](3);source.obj=xmrFixedArrayAllocO(3);destination.obj=xmrFixedArrayAllocI(3);xmrFixedArraySetO(source.obj,0,11);xmrFixedArraySetO(source.obj,1,"wrong");xmrFixedArraySetO(source.obj,2,33);xmrFixedArraySetI(destination.obj,0,101);xmrFixedArraySetI(destination.obj,1,102);xmrFixedArraySetI(destination.obj,2,103);llSleep(0.1);xmrArrayCopy(source,0,destination,0,3);after=1;}}
