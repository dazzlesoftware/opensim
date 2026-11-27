YOption allowall;
integer[] input; object nil; integer before; integer resumed; integer cleared; integer marker;
default{state_entry(){input=new integer[](3);integer[] alias=input;string text="xyz";before=xmrHeapUsed();llSleep(0.1);resumed=xmrHeapUsed();text="";cleared=xmrHeapUsed();marker=(alias==input);}}
