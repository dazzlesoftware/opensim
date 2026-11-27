YOption allowall;
integer[] input; object backing; integer after;
default{state_entry(){input=new integer[](3);backing=input.obj;llSleep(0.1);xmrList2Array([11,"bad",33],0,input,0,3);after=1;}}
