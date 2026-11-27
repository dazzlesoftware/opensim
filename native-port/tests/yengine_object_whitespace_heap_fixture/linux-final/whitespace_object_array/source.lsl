YOption allowall;
object raw; object alias; integer baseline; integer allocated; integer mutated; integer aliased; integer after;
default{state_entry(){baseline=xmrHeapUsed();raw=xmrFixedArrayAllocO(1);allocated=xmrHeapUsed();xmrFixedArraySetO(raw,0,(object)"   ");mutated=xmrHeapUsed();alias=raw;aliased=xmrHeapUsed();llSleep(0.1);after=xmrHeapUsed();}}
