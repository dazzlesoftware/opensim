YOption allowall;
float[] input; list output; integer after;
default{state_entry(){input=new float[](2);input[0]=11.5;input[1]=2.25;output=xmrArray2List(input,0,2);llSleep(0.1);after=1;}}
