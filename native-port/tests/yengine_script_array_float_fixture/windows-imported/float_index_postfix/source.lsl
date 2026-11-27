YOption allowall;
float[] input; float[] alias; integer cursor; float old; float result_input; float result_alias; integer after;
default {state_entry(){input=new float[](2);input[0]=11.5;input[1]=23.25;alias=input;old=input[cursor++]++;
 result_input=input[0];result_alias=alias[1];llSleep(0.1);after=1;}}
