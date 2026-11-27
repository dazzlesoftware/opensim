YOption allowall;
integer[] input; integer[] alias; integer base_count; integer index_count; integer right_count; integer order; integer result_input; integer result_alias; integer old; integer after;
integer[] Base(){base_count++;order=order*10+3;return input;}
integer Index(){index_count++;order=order*10+1;return 0;}
integer Right(){right_count++;order=order*10+2;return 7;}
default{state_entry(){input=new integer[](1);input[0]=11;alias=input;old=Base()[Index()]++;result_input=input[0];result_alias=alias[0];llSleep(0.1);after=1;}}
