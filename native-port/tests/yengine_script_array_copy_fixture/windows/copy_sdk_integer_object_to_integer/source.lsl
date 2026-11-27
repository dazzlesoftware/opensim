YOption allowall;
integer[] source; integer[] destination; integer after;
default {state_entry(){source=new integer[](1);destination=new integer[](1);source.obj=xmrFixedArrayAllocO(1);destination.obj=xmrFixedArrayAllocI(1);xmrFixedArraySetO(source.obj,0,[11][0]);llSleep(0.1);xmrArrayCopy(source,0,destination,0,1);after=1;}}
