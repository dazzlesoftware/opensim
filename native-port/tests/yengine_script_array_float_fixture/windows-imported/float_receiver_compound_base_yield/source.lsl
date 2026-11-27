YOption allowall;
float[] input; float[] alias; integer base_count; integer index_count; integer right_count; integer order;
float result_input; float result_alias; integer after;
float[] Base(){base_count++;order=order*10+3;input=new float[](1);input[0]=100.25;llSleep(0.1);return input;}
integer Index(){index_count++;order=order*10+1;return 0;}
float Right(){right_count++;order=order*10+2;return 7.125;}
default {state_entry(){input=new float[](1);input[0]=11.5;alias=input;Base()[Index()]+=Right();result_input=input[0];result_alias=alias[0];after=1;}}
