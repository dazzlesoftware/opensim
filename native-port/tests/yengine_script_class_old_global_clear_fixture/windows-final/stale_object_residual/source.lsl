YOption allowall;
integer[] input; object raw; integer marker;
default{state_entry(){input=new integer[](3);raw=xmrFixedArrayAllocO(1);xmrFixedArraySetO(raw,0,(object)"abc");marker=7;llSleep(0.1);}}
