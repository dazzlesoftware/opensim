YOption allowall;
float[] input; float[] alias; integer index_count; integer right_count; integer order;
float result_input; float result_alias; integer after;
integer Index(){index_count++;order=order*10+1;return 0;}
float Right(){right_count++;order=order*10+2;input=new float[](1);input[0]=100.25;llSleep(0.1);return 7.125;}
default {state_entry(){input=new float[](1);input[0]=11.5;alias=input;input[Index()]+=Right();result_input=input[0];result_alias=alias[0];after=1;}}
