YOption allowall;
integer[] input; integer[] alias; integer index_count; integer right_count; integer order;
integer result_input; integer result_alias; integer after;
integer Right(){right_count++;order=order*10+2;input=new integer[](2);input.len0=10;llSleep(0.1);return 4;}
default{state_entry(){input=new integer[](3);alias=input;input.len0+=Right();result_input=input.len0;result_alias=alias.len0;after=1;}}
