YOption allowall;
char[] input; object backing; integer after;
default{state_entry(){input=new char[](3);backing=input.obj;llSleep(0.1);xmrList2Array([65,66],0,input,0,2);after=1;}}
