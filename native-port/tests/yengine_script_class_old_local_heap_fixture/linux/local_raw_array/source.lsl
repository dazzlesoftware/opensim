YOption allowall;
integer[] input; object nil; integer before; integer resumed; integer cleared; integer marker;
default{state_entry(){input=new integer[](3);integer[] alias=input;object raw=alias.obj;before=xmrHeapUsed();llSleep(0.1);resumed=xmrHeapUsed();raw=nil;cleared=xmrHeapUsed();marker=(alias==input);}}
