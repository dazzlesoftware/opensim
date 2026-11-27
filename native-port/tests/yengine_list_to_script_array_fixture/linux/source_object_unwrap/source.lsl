YOption allowall;
integer[] input; object backing; integer after;
default{state_entry(){input=new integer[](3);input.obj=xmrFixedArrayAllocO(3);backing=input.obj;llSleep(0.1);xmrList2Array([1,2.5,"x"],0,input,0,3);after=1;}}
