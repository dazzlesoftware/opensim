YOption allowall;
integer[] numbers; char[] input; integer result; integer after;
default{state_entry(){numbers=new integer[](2);numbers[1]=17;input=new char[](2);input[0]=(char)66;input[1]=(char)0;llSleep(0.1);result=numbers[1]+(integer)input[0];after=1;}}
