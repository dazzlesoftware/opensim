YOption allowall;
integer[] input; object backing; integer after;
default{state_entry(){input=new integer[](3);input.obj=xmrFixedArrayAllocO(3);backing=input.obj;llSleep(0.1);after=1;}}
