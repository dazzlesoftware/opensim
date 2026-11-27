YOption allowall;
integer[] input; list output; integer after;
default{state_entry(){input=new integer[](2);input[0]=7;input[1]=-9;output=xmrArray2List(input,0,2);llSleep(0.1);after=1;}}
