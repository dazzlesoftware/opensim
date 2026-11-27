YOption allowall;
integer[] input; integer[] alias; integer index_count; integer right_count; integer order;
integer result_input; integer result_alias; integer after;
integer cursor;integer old;
default{state_entry(){input=new integer[](2);input[0]=11;input[1]=23;alias=input;old=input[cursor++]++;result_input=input[0];result_alias=alias[1];llSleep(0.1);after=1;}}
