YOption allowall;
integer[] input; object backing; integer after;
default{state_entry(){input=new integer[](3);backing=input.obj;llSleep(0.1);xmrList2Array([11,22],0,input,1,2);after=1;}}
