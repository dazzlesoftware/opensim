YOption allowall;
object boxed; string typed; list items; integer baseline; integer objectheap; integer stringheap; integer listheap; integer after;
default{state_entry(){baseline=xmrHeapUsed();boxed=(object)"   ";objectheap=xmrHeapUsed();typed="   ";stringheap=xmrHeapUsed();items=["   "];listheap=xmrHeapUsed();llSleep(0.1);after=xmrHeapUsed();}}
