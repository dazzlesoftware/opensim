YOption allowall;
integer[] integers; float[] input; float[] cast; object wrapped; integer same; integer after; string integer_type; string float_type;
default {state_entry(){integers=new integer[](1);integers[0]=9;input=new float[](2);input[0]=0.125;input[1]=-0.25;
 wrapped=input;cast=(float[])wrapped;same=(input==cast);integer_type=xmrTypeName((object)integers);float_type=xmrTypeName(wrapped);
 llSleep(0.1);after=(input==cast)&&(integers[0]==9)&&(input[1]==-0.25);integers=(integer[])wrapped;}}
